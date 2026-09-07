/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "disk_formatter.h"

#include "linux/file_operations_linux.h"

#include <iostream>
#include <filesystem>
#include <fstream>
#include <array>
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <cstddef>
#include <utility>
#include <tuple>
#include <vector>
#include <memory>
#include <algorithm>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

namespace fs = std::filesystem;
using namespace rpi_imager;

namespace {

// Per-process scratch directory.
//
// These tests used to write to fixed /tmp paths (/tmp/test_disk.img and
// friends). That collides whenever two runs overlap -- ctest -j, or a
// developer running the binary while CI runs it -- and leaves debris behind
// when a case fails early. Now that the binary is registered with CTest and
// runs unattended, neither is acceptable.
class ScratchDir {
 public:
  ScratchDir() {
    base_ = fs::temp_directory_path() /
            ("rpi-imager-disk-formatter-" + std::to_string(getpid()));
    std::error_code ec;
    fs::remove_all(base_, ec);
    fs::create_directories(base_, ec);
  }

  ~ScratchDir() {
    std::error_code ec;
    fs::remove_all(base_, ec);
  }

  ScratchDir(const ScratchDir&) = delete;
  ScratchDir& operator=(const ScratchDir&) = delete;

  std::string file(const char* name) const { return (base_ / name).string(); }

 private:
  fs::path base_;
};

ScratchDir& scratch() {
  static ScratchDir dir;
  return dir;
}

// A device that answers a format entirely from memory.
//
// Every existing case here formats a real file that succeeds, so nothing had
// ever driven DiskFormatter's error handling: the whole of ConvertFileError
// and the failure branch of all seven writes were unexecuted. This stands in
// for the device so a failure can be injected at a chosen step, without going
// near a real block device.
//
// Deriving from LinuxFileOperations rather than FileOperations avoids
// implementing the other two dozen pure virtuals; DiskFormatter only ever
// calls these four, and each is overridden, so the base's file descriptor is
// never opened.
class ScriptedDevice : public LinuxFileOperations {
 public:
  explicit ScriptedDevice(std::uint64_t size_bytes) : size_bytes_(size_bytes) {}

  // Which write to fail, counting from 1. Zero fails none.
  void FailWrite(int index, FileError error) {
    fail_write_ = index;
    write_error_ = error;
  }
  void FailOpen(FileError error) { open_result_ = error; }
  void FailCreate(FileError error) { create_result_ = error; }
  void FailGetSize(FileError error) { size_result_ = error; }

  // Every write the formatter made: where it went, how long it was, and its
  // first sector. Only the head is kept -- a FAT copy for a large device runs
  // to hundreds of megabytes, and the fields a test wants are all in sector 0.
  struct Write {
    std::uint64_t offset;
    std::size_t size;
    std::array<std::uint8_t, 512> head;
  };

  int writes() const { return writes_; }
  const std::vector<Write>& writeLog() const { return write_log_; }

  // The write that landed exactly at `offset`, or nullptr if there was none.
  const Write* writeAt(std::uint64_t offset) const {
    for (const auto& w : write_log_)
      if (w.offset == offset) return &w;
    return nullptr;
  }

  FileError OpenDevice(const std::string&) override { return open_result_; }

  FileError CreateTestFile(const std::string&, std::uint64_t size) override {
    if (create_result_ == FileError::kSuccess) size_bytes_ = size;
    return create_result_;
  }

  FileError GetSize(std::uint64_t& size) override {
    size = size_bytes_;
    return size_result_;
  }

  FileError WriteAtOffset(std::uint64_t offset, const std::uint8_t* data,
                          std::size_t size) override {
    ++writes_;
    if (writes_ == fail_write_) return write_error_;
    Write w{offset, size, {}};
    std::copy_n(data, std::min(size, w.head.size()), w.head.begin());
    write_log_.push_back(w);
    return FileError::kSuccess;
  }

  FileError Close() override { return FileError::kSuccess; }
  bool IsOpen() const override { return true; }
  FileError ForceSync() override { return FileError::kSuccess; }
  FileError Flush() override { return FileError::kSuccess; }

 private:
  std::uint64_t size_bytes_;
  FileError open_result_ = FileError::kSuccess;
  FileError create_result_ = FileError::kSuccess;
  FileError size_result_ = FileError::kSuccess;
  FileError write_error_ = FileError::kWriteError;
  int fail_write_ = 0;
  int writes_ = 0;
  std::vector<Write> write_log_;
};

// Read a little-endian 32-bit field out of a captured sector.
std::uint32_t le32(const std::array<std::uint8_t, 512>& sector, std::size_t at) {
  std::uint32_t v = 0;
  for (std::size_t i = 0; i < 4; ++i)
    v |= static_cast<std::uint32_t>(sector[at + i]) << (8 * i);
  return v;
}

// The MBR's single partition entry lives at byte 446; within it first_lba is
// 8 bytes in and num_sectors 12, per MbrPartitionEntry.
constexpr std::size_t kPartitionEntry = 446;
constexpr std::size_t kFirstLbaField = kPartitionEntry + 8;
constexpr std::size_t kNumSectorsField = kPartitionEntry + 12;

// A CHS address as MBR packs it: the cylinder's top two bits are carried in
// the top of the sector byte, and sector numbers count from 1.
struct Chs {
  unsigned cylinder;
  unsigned head;
  unsigned sector;
};

Chs readChs(const std::array<std::uint8_t, 512>& mbr, std::size_t at) {
  const unsigned head = mbr[at];
  const unsigned sector_byte = mbr[at + 1];
  const unsigned cylinder_byte = mbr[at + 2];
  return Chs{((sector_byte & 0xC0u) << 2) | cylinder_byte, head,
             sector_byte & 0x3Fu};
}

}  // namespace

class DiskFormatterTest {
 public:
  static bool RunAllTests() {
    std::cout << "Running DiskFormatter tests...\n";
    
    bool all_passed = true;
    all_passed &= TestBasicFormatting();
    all_passed &= TestMbrStructure();
    all_passed &= TestFat32Structure();
    all_passed &= TestSystemToolValidation();
    all_passed &= TestWriteFailureIsReported();
    all_passed &= TestDeviceErrorsReachTheUser();
    all_passed &= TestPartitionTableAndFilesystemAgree();
    all_passed &= TestAcceptedDevicesGetAValidFat32();
    all_passed &= TestFatIsWrittenInBoundedPieces();
    
    if (all_passed) {
      std::cout << "All tests passed!\n";
    } else {
      std::cout << "Some tests failed!\n";
    }
    
    return all_passed;
  }

 private:
  static bool TestBasicFormatting() {
    std::cout << "Testing basic formatting...\n";
    
    const std::string test_file = scratch().file("basic.img");
    const std::uint64_t disk_size = 64 * 1024 * 1024;  // 64MB
    
    // Clean up any existing test file
    fs::remove(test_file);
    
    DiskFormatter formatter;
    auto result = formatter.FormatFile(test_file, disk_size);
    
    if (!result) {
      std::cout << "❌ Failed to format file\n";
      return false;
    }
    
    // Check file was created with correct size
    if (!fs::exists(test_file)) {
      std::cout << "❌ Test file was not created\n";
      return false;
    }
    
    if (fs::file_size(test_file) != disk_size) {
      std::cout << "❌ Test file has incorrect size\n";
      return false;
    }
    
    std::cout << "✅ Basic formatting test passed\n";
    return true;
  }
  
  static bool TestMbrStructure() {
    std::cout << "Testing MBR structure...\n";
    
    const std::string test_file = scratch().file("mbr.img");
    const std::uint64_t disk_size = 64 * 1024 * 1024;  // 64MB
    
    fs::remove(test_file);
    
    DiskFormatter formatter;
    auto result = formatter.FormatFile(test_file, disk_size);
    
    if (!result) {
      std::cout << "❌ Failed to format file\n";
      return false;
    }
    
    // Read and validate MBR
    std::ifstream file(test_file, std::ios::binary);
    if (!file) {
      std::cout << "❌ Cannot open test file for reading\n";
      return false;
    }
    
    std::array<std::uint8_t, 512> mbr{};
    file.read(reinterpret_cast<char*>(mbr.data()), 512);
    
    // Check MBR signature
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
      std::cout << "❌ Invalid MBR signature\n";
      return false;
    }
    
    // Check partition entry (starts at offset 446)
    const auto* partition = reinterpret_cast<const MbrPartitionEntry*>(mbr.data() + 446);
    
    if (partition->status != 0x80) {
      std::cout << "❌ Partition not marked as bootable\n";
      return false;
    }
    
    if (partition->partition_type != 0x0C) {  // FAT32 LBA
      std::cout << "❌ Wrong partition type: " << static_cast<int>(partition->partition_type) << "\n";
      return false;
    }
    
    std::uint32_t first_lba = partition->first_lba;
    if (first_lba != 8192) {  // Should start at 4MB
      std::cout << "❌ Wrong partition start sector: " << first_lba << "\n";
      return false;
    }
    
    std::cout << "✅ MBR structure test passed\n";
    return true;
  }
  
  static bool TestFat32Structure() {
    std::cout << "Testing FAT32 structure...\n";
    
    const std::string test_file = scratch().file("fat32.img");
    const std::uint64_t disk_size = 64 * 1024 * 1024;  // 64MB
    
    fs::remove(test_file);
    
    DiskFormatter formatter;
    auto result = formatter.FormatFile(test_file, disk_size);
    
    if (!result) {
      std::cout << "❌ Failed to format file\n";
      return false;
    }
    
    // Read FAT32 boot sector (at partition start: sector 8192)
    std::ifstream file(test_file, std::ios::binary);
    if (!file) {
      std::cout << "❌ Cannot open test file for reading\n";
      return false;
    }
    
    file.seekg(8192 * 512);  // Seek to partition start
    std::array<std::uint8_t, 512> boot_sector{};
    file.read(reinterpret_cast<char*>(boot_sector.data()), 512);
    
    const auto* fat32_boot = reinterpret_cast<const Fat32BootSector*>(boot_sector.data());
    
    // Check boot signature (stored in little-endian)
    std::uint16_t signature = fat32_boot->signature;
    if (signature != 0xAA55) {  // Little-endian: 0x55 0xAA
      std::cout << "❌ Invalid FAT32 boot signature: 0x" << std::hex << signature << std::dec << "\n";
      return false;
    }
    
    // Check bytes per sector (stored in little-endian)
    std::uint16_t bytes_per_sector = fat32_boot->bytes_per_sector;
    if (bytes_per_sector != 512) {
      std::cout << "❌ Wrong bytes per sector: " << bytes_per_sector << "\n";
      return false;
    }
    
    // Check filesystem type
    std::string fs_type(fat32_boot->fs_type.data(), 8);
    if (fs_type.substr(0, 5) != "FAT32") {
      std::cout << "❌ Wrong filesystem type: " << fs_type << "\n";
      return false;
    }
    
    // Check jump instruction
    if (fat32_boot->jump_instruction[0] != 0xEB) {
      std::cout << "❌ Invalid jump instruction\n";
      return false;
    }
    
    std::cout << "✅ FAT32 structure test passed\n";
    return true;
  }
  
  // Run a command with separate argv (no shell interpretation).
  // Returns the exit status, or -1 on fork/exec failure.
  static int runCommand(const char *path, const std::vector<const char*> &argv) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
      // Redirect stdout/stderr to /dev/null
      int devnull = open("/dev/null", O_WRONLY);
      if (devnull >= 0) { dup2(devnull, 1); dup2(devnull, 2); close(devnull); }
      execv(path, const_cast<char *const *>(argv.data()));
      _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  }

  // As runCommand(), but hands back what the tool printed. Some tools answer
  // by describing what they found rather than by their exit status -- file(1)
  // succeeds on anything readable, so its verdict is in its output.
  static int runCommandCapture(const char *path, const std::vector<const char*> &argv,
                               std::string &output) {
    int fds[2];
    if (pipe(fds) != 0) return -1;

    pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return -1; }
    if (pid == 0) {
      close(fds[0]);
      dup2(fds[1], 1);
      dup2(fds[1], 2);
      close(fds[1]);
      execv(path, const_cast<char *const *>(argv.data()));
      _exit(127);
    }

    close(fds[1]);
    char buf[4096];
    ssize_t n;
    while ((n = read(fds[0], buf, sizeof(buf))) > 0)
      output.append(buf, static_cast<size_t>(n));
    close(fds[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  }

  // Run a privileged command: directly when we are already root, via sudo -n
  // otherwise.
  //
  // Going straight to the binary when root matters for more than tidiness. In
  // a namespaced container sudo drops the ambient capability set, so
  // `sudo losetup` fails with EPERM in environments where plain `losetup`
  // succeeds -- which would silently cost us the whole mount check. The -n on
  // the fallback keeps an unattended run from stopping at a password prompt.
  static int runPrivileged(const char *absPath, std::vector<const char*> args,
                           std::string *out = nullptr) {
    std::vector<const char*> argv;
    const char *binary;
    if (geteuid() == 0) {
      binary = absPath;
      argv.push_back(absPath);
    } else {
      binary = "/usr/bin/sudo";
      argv.push_back("sudo");
      argv.push_back("-n");
      argv.push_back(absPath);
    }
    argv.insert(argv.end(), args.begin(), args.end());
    argv.push_back(nullptr);
    return out ? runCapture(binary, argv, out) : runCommand(binary, argv);
  }

  // As runCommand, but captures the child's stdout into *out.
  static int runCapture(const char *path, const std::vector<const char*> &argv,
                        std::string *out) {
    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); return -1; }
    if (pid == 0) {
      close(pipefd[0]);
      dup2(pipefd[1], 1);
      close(pipefd[1]);
      int devnull = open("/dev/null", O_WRONLY);
      if (devnull >= 0) { dup2(devnull, 2); close(devnull); }
      execv(path, const_cast<char *const *>(argv.data()));
      _exit(127);
    }
    close(pipefd[1]);
    char buf[256] = {};
    ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
    close(pipefd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (out && n > 0) out->assign(buf, static_cast<std::size_t>(n));
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  }

  // Validate that a string looks like a /dev/loop device path.
  static bool isValidLoopDevice(const char *s) {
    // Must match /dev/loop[0-9]+
    if (strncmp(s, "/dev/loop", 9) != 0) return false;
    const char *p = s + 9;
    if (*p == '\0') return false;
    while (*p) { if (*p < '0' || *p > '9') return false; p++; }
    return true;
  }

  static bool TestSystemToolValidation() {
    std::cout << "Testing with system tools...\n";

    const std::string test_file = scratch().file("system.img");
    const std::uint64_t disk_size = 64 * 1024 * 1024;  // 64MB

    fs::remove(test_file);

    DiskFormatter formatter;
    auto result = formatter.FormatFile(test_file, disk_size);

    if (!result) {
      std::cout << "Failed to format file\n";
      return false;
    }

    bool all_passed = true;

    // Test with fdisk to check partition table (safe: test_file is a hardcoded constant)
    //
    // A rejected partition table used to be reported as "tool may not be
    // available" and ignored. execv() answers 127 when the binary is not
    // there, which tells the two apart.
    {
      std::vector<const char*> argv = {"fdisk", "-l", test_file.c_str(), nullptr};
      std::string listing;
      const int rc = runCommandCapture("/usr/sbin/fdisk", argv, listing);
      if (rc == 127) {
        std::cout << "fdisk is not installed, partition table not checked\n";
      } else if (rc != 0) {
        std::cout << "fdisk could not read the image (exit " << rc << ")\n";
        all_passed = false;
      } else if (listing.find("FAT32") == std::string::npos) {
        // The exit status alone proves nothing: fdisk -l answers 0 for an
        // image of zeroes with no partition table at all. What it prints is
        // the verdict, and a FAT32 entry is what this formatter is for.
        std::cout << "fdisk lists no FAT32 partition:\n" << listing;
        all_passed = false;
      } else {
        std::cout << "fdisk validation passed\n";
      }
    }

    // Test with file command to detect filesystem
    //
    // Its exit status says whether it could read the file, not whether the
    // file is what we meant to write -- file(1) succeeds on a directory of
    // zeroes just as happily. The verdict is in what it prints, so that is
    // what gets checked.
    {
      std::vector<const char*> argv = {"file", test_file.c_str(), nullptr};
      std::string described;
      const int rc = runCommandCapture("/usr/bin/file", argv, described);
      if (rc == 127) {
        std::cout << "file(1) is not installed, image not identified\n";
      } else if (rc != 0) {
        std::cout << "file(1) could not read the image (exit " << rc << ")\n";
        all_passed = false;
      } else if (described.find("boot sector") == std::string::npos) {
        std::cout << "file(1) does not see a boot sector: " << described;
        all_passed = false;
      } else {
        std::cout << "file command validation passed (" << described.substr(0, 120) << ")\n";
      }
    }

    // Set up a loop device and mount the FAT filesystem we just wrote. This is
    // the only check that proves the image is actually mountable rather than
    // merely structurally plausible, so it is worth running wherever the host
    // permits it -- and worth saying so out loud when it cannot, rather than
    // passing silently as it used to.
    //
    // Needs CAP_SYS_ADMIN. runPrivileged() goes straight to the binary when we
    // are root and falls back to sudo -n otherwise, so an unattended run
    // without the privilege fails fast instead of stopping at a password
    // prompt. -P asks for the partition table to be scanned; without it
    // <loop>p1 never appears and the mount cannot work.
    const std::string mount_point = scratch().file("mount");
    {
      std::error_code ec;
      fs::create_directories(mount_point, ec);
    }

    {
      std::string out;
      int rc = runPrivileged("/usr/sbin/losetup",
                             {"--find", "--show", "-P", test_file.c_str()}, &out);
      out.erase(0, out.find_first_not_of(" \t\r\n"));
      const auto last = out.find_last_not_of(" \t\r\n");
      out.erase(last == std::string::npos ? 0 : last + 1);

      if (rc != 0 || out.empty()) {
        std::cout << "Loop/mount test SKIPPED (could not attach a loop device; "
                     "needs CAP_SYS_ADMIN or passwordless sudo)\n";
      } else if (!isValidLoopDevice(out.c_str())) {
        std::cout << "Loop/mount test SKIPPED (losetup returned an unexpected path)\n";
      } else {
        const std::string loop_device = out;
        const std::string loop_part = loop_device + "p1";

        if (runPrivileged("/usr/bin/mount",
                          {"-t", "vfat", loop_part.c_str(), mount_point.c_str()}) == 0) {
          std::cout << "Mount test passed\n";

          const std::string touch_path = mount_point + "/test.txt";
          if (runPrivileged("/usr/bin/touch", {touch_path.c_str()}) == 0) {
            std::cout << "File creation test passed\n";
            runPrivileged("/usr/bin/rm", {touch_path.c_str()});
          } else {
            std::cout << "File creation test failed\n";
            all_passed = false;
          }

          runPrivileged("/usr/bin/umount", {mount_point.c_str()});
        } else {
          std::cout << "Mount test SKIPPED (loop device attached but mount was refused)\n";
        }

        runPrivileged("/usr/sbin/losetup", {"-d", loop_device.c_str()});
      }
    }

    // Verify the filesystem with an independent checker.
    //
    // This used to report a rejected filesystem as "skipped (tool may not be
    // available)" and leave all_passed alone, so the strongest check of what
    // the formatter produces could not fail the test -- a corrupt FAT32 and a
    // missing fsck.fat looked identical. The two are now told apart, and only
    // the genuinely missing tool is a skip.
    {
      const char* fsck = "/usr/sbin/fsck.fat";
      if (!std::filesystem::exists(fsck)) {
        std::cout << "fsck.fat is not installed, filesystem not independently checked\n";
      } else {
        // fsck.fat has to be given the filesystem, not the disk holding it.
        // Pointed at the image it reads the MBR as a boot sector and answers
        // "Logical sector size is zero" -- which is what it had always been
        // doing, since the result was discarded and reported as a skip.
        //
        // The partition is copied out rather than loop-mounted so this needs
        // no privileges, and the offset is read from the image's own
        // partition table rather than assumed.
        std::vector<std::uint8_t> mbr(512);
        std::ifstream in(test_file, std::ios::binary);
        in.read(reinterpret_cast<char*>(mbr.data()), 512);
        const bool signature_ok = in.gcount() == 512 && mbr[510] == 0x55 && mbr[511] == 0xAA;
        std::uint32_t first_lba = 0;
        for (int i = 0; i < 4; ++i)
          first_lba |= static_cast<std::uint32_t>(mbr[0x1BE + 8 + i]) << (8 * i);

        if (!signature_ok || first_lba == 0) {
          std::cout << "could not read a partition offset from the image\n";
          all_passed = false;
        } else {
          const std::string part_file = scratch().file("partition.img");
          std::ifstream src(test_file, std::ios::binary);
          std::ofstream dst(part_file, std::ios::binary | std::ios::trunc);
          src.seekg(static_cast<std::streamoff>(first_lba) * 512);
          dst << src.rdbuf();
          dst.close();

          // -n answers no to every question, so it reports without repairing:
          // a filesystem it wanted to fix is one this test should fail.
          std::vector<const char*> argv = {"fsck.fat", "-n", "-v", part_file.c_str(), nullptr};
          const int rc = runCommand(fsck, argv);
          if (rc == 0) {
            std::cout << "fsck.fat validation passed\n";
          } else {
            std::cout << "fsck.fat rejected the filesystem (exit " << rc << ")\n";
            all_passed = false;
          }
        }
      }
    }

    std::cout << "System tool validation completed\n";
    return all_passed;
  }


  // A format lays down seven things in order. If any one of them fails the
  // whole format has to fail: a card carrying a partition table but no FAT,
  // or one FAT copy of two, mounts on some hosts and corrupts on others. The
  // user would be told the erase completed.
  //
  // The step names are here so a failure says which write was refused rather
  // than just "write 5". If a step is added to WriteFat32 and its result is
  // not checked, the count assertion below catches it.
  static bool TestWriteFailureIsReported() {
    std::cout << "Testing that a device refusing a write fails the format...\n";

    struct Step {
      int write_index;
      const char* what;
    };
    static constexpr Step kSteps[] = {
      {1, "the partition table"},
      {2, "the boot sector"},
      {3, "the FSInfo sector"},
      {4, "the backup boot sector"},
      {5, "the first FAT copy"},
      {6, "the second FAT copy"},
      {7, "the root directory"},
    };
    static constexpr int kExpectedWrites =
        static_cast<int>(std::size(kSteps));
    const std::uint64_t device_size = 64 * 1024 * 1024;

    bool all_passed = true;

    // First, a device that refuses nothing, to pin the number of writes the
    // table above is describing.
    {
      auto device = std::make_unique<ScriptedDevice>(device_size);
      auto* raw = device.get();
      DiskFormatter formatter(std::move(device));
      auto result = formatter.FormatDrive("/dev/fake");
      if (!result) {
        std::cout << "❌ a device that refuses nothing failed to format\n";
        all_passed = false;
      }
      if (raw->writes() != kExpectedWrites) {
        std::cout << "❌ a clean format made " << raw->writes()
                  << " writes, not the " << kExpectedWrites
                  << " the step table describes -- if a format step was added,"
                     " add it to kSteps and check its result\n";
        all_passed = false;
      }
    }

    for (const auto& step : kSteps) {
      auto device = std::make_unique<ScriptedDevice>(device_size);
      auto* raw = device.get();
      device->FailWrite(step.write_index, FileError::kWriteError);
      DiskFormatter formatter(std::move(device));
      auto result = formatter.FormatDrive("/dev/fake");

      if (result) {
        std::cout << "❌ the device refused to write " << step.what
                  << " and the format reported success\n";
        all_passed = false;
        continue;
      }
      if (result.error() != FormatError::kFileWriteError) {
        std::cout << "❌ a refused write of " << step.what << " reported error "
                  << static_cast<int>(result.error()) << ", not kFileWriteError\n";
        all_passed = false;
      }
      // Nothing may be written after the failure: the formatter has to give up
      // there rather than carry on laying down the rest of the filesystem.
      if (raw->writes() != step.write_index) {
        std::cout << "❌ the device refused to write " << step.what
                  << " and the formatter carried on: " << raw->writes()
                  << " writes attempted, expected to stop at "
                  << step.write_index << "\n";
        all_passed = false;
      }
    }

    if (all_passed) std::cout << "✅ every refused write fails the format\n";
    return all_passed;
  }

  // Each FormatError becomes a different sentence in the UI --
  // DriveFormatThread::formatErrorToString turns them into "Error opening
  // device", "Error seeking", "Formatting cancelled" and so on. So the mapping
  // from the device's error to that decides which of those a user reads. None
  // of it had ever been executed.
  static bool TestDeviceErrorsReachTheUser() {
    std::cout << "Testing which error a failing device produces...\n";

    struct Case {
      FileError from;
      FormatError to;
      const char* why;
    };
    static constexpr Case kCases[] = {
      {FileError::kOpenError,  FormatError::kFileOpenError,  "cannot open"},
      {FileError::kWriteError, FormatError::kFileWriteError, "write refused"},
      {FileError::kReadError,  FormatError::kFileOpenError,  "read refused"},
      {FileError::kSeekError,  FormatError::kFileSeekError,  "cannot seek"},
      {FileError::kSizeError,  FormatError::kFileOpenError,  "size unknown"},
      {FileError::kCloseError, FormatError::kFileOpenError,  "close failed"},
      {FileError::kLockError,  FormatError::kFileOpenError,  "already in use"},
      {FileError::kSyncError,  FormatError::kFileWriteError, "sync failed"},
      {FileError::kFlushError, FormatError::kFileWriteError, "flush failed"},
      {FileError::kTimeout,    FormatError::kFileWriteError, "device stopped responding"},
      {FileError::kCancelled,  FormatError::kCancelled,      "user cancelled"},
    };

    bool all_passed = true;
    for (const auto& c : kCases) {
      // Through the open, which is the first thing a format does.
      {
        auto device = std::make_unique<ScriptedDevice>(64 * 1024 * 1024);
        device->FailOpen(c.from);
        DiskFormatter formatter(std::move(device));
        auto result = formatter.FormatDrive("/dev/fake");
        if (result) {
          std::cout << "❌ opening a device that reported " << c.why
                    << " was treated as success\n";
          all_passed = false;
        } else if (result.error() != c.to) {
          std::cout << "❌ a device that reported " << c.why
                    << " on open produced error " << static_cast<int>(result.error())
                    << ", expected " << static_cast<int>(c.to) << "\n";
          all_passed = false;
        }
      }
      // And through a write, which reaches the same mapping from deeper in.
      {
        auto device = std::make_unique<ScriptedDevice>(64 * 1024 * 1024);
        device->FailWrite(1, c.from);
        DiskFormatter formatter(std::move(device));
        auto result = formatter.FormatDrive("/dev/fake");
        if (result) {
          std::cout << "❌ a write that reported " << c.why
                    << " was treated as success\n";
          all_passed = false;
        } else if (result.error() != c.to) {
          std::cout << "❌ a write that reported " << c.why
                    << " produced error " << static_cast<int>(result.error())
                    << ", expected " << static_cast<int>(c.to) << "\n";
          all_passed = false;
        }
      }
    }

    // A device whose size cannot be read is a distinct step from opening it.
    {
      auto device = std::make_unique<ScriptedDevice>(64 * 1024 * 1024);
      device->FailGetSize(FileError::kSizeError);
      DiskFormatter formatter(std::move(device));
      auto result = formatter.FormatDrive("/dev/fake");
      if (result || result.error() != FormatError::kFileOpenError) {
        std::cout << "❌ a device whose size could not be read did not report "
                     "kFileOpenError\n";
        all_passed = false;
      }
    }

    // FormatFile reaches the same writes as FormatDrive but has its own
    // returns on the way, so a refused write has to fail it too.
    {
      auto device = std::make_unique<ScriptedDevice>(64 * 1024 * 1024);
      device->FailWrite(1, FileError::kWriteError);
      DiskFormatter formatter(std::move(device));
      auto result = formatter.FormatFile("unused.img", 64 * 1024 * 1024);
      if (result || result.error() != FormatError::kFileWriteError) {
        std::cout << "❌ FormatFile reported success when the partition table "
                     "could not be written\n";
        all_passed = false;
      }
    }

    // FormatFile's own first step, which has its own error return.
    {
      auto device = std::make_unique<ScriptedDevice>(64 * 1024 * 1024);
      device->FailCreate(FileError::kOpenError);
      DiskFormatter formatter(std::move(device));
      auto result = formatter.FormatFile("unused.img", 64 * 1024 * 1024);
      if (result || result.error() != FormatError::kFileOpenError) {
        std::cout << "❌ a file that could not be created did not report "
                     "kFileOpenError\n";
        all_passed = false;
      }
    }

    if (all_passed) std::cout << "✅ device errors map to the intended report\n";
    return all_passed;
  }

  // MBR cannot describe more than 2^32 sectors, so a device larger than 2 TiB
  // has to be capped. WriteMbr caps; FormatDrive and FormatFile computed the
  // partition size for the filesystem separately, by truncating the same
  // 64-bit sector count into 32 bits. The two then disagree:
  //
  //   3 TiB: partition table says 4294959103 sectors, FAT32 says 2147475456
  //   4 TiB: partition table says 4294959103 sectors, FAT32 says 4294959104
  //
  // At 3 and 5 TiB the filesystem describes half the partition, so the user
  // loses a terabyte of a drive that reported its size correctly. At 2 and
  // 4 TiB it is worse in kind: the filesystem claims one sector MORE than the
  // partition holds, putting its last cluster outside the partition.
  //
  // The two numbers are written to different sectors, so this checks both: the
  // partition entry's num_sectors in the MBR, and total_sectors_32 in the FAT32
  // boot sector.
  static bool TestPartitionTableAndFilesystemAgree() {
    std::cout << "Testing that the partition table and the filesystem in it "
                 "describe the same partition...\n";

    constexpr std::uint64_t kTiB = 1024ULL * 1024 * 1024 * 1024;
    struct Case {
      const char* name;
      std::uint64_t size_bytes;
    };
    const Case kCases[] = {
      {"64 MB",  64ULL * 1024 * 1024},
      {"32 GB",  32ULL * 1024 * 1024 * 1024},
      {"2 TiB",  2 * kTiB},   // exactly on the MBR limit
      {"3 TiB",  3 * kTiB},
      {"4 TiB",  4 * kTiB},
      {"5 TiB",  5 * kTiB},
      {"8 TiB",  8 * kTiB},
    };

    constexpr std::uint32_t kStartSector = 8192;  // DiskFormatter's 4 MB offset
    bool all_passed = true;

    for (const auto& c : kCases) {
      auto device = std::make_unique<ScriptedDevice>(c.size_bytes);
      auto* raw = device.get();
      // Stop after the boot sector. Both numbers under test have been written
      // by then, and the FAT copies for a 2 TiB partition are half a gigabyte
      // each -- this test does not need them laid down to read the geometry.
      device->FailWrite(3, FileError::kWriteError);
      DiskFormatter formatter(std::move(device));
      formatter.FormatDrive("/dev/fake");

      const auto* mbr = raw->writeAt(0);
      const auto* boot = raw->writeAt(std::uint64_t{kStartSector} * 512);
      if (mbr == nullptr || boot == nullptr) {
        std::cout << "❌ " << c.name
                  << ": formatting wrote no partition table or no boot sector\n";
        all_passed = false;
        continue;
      }

      const std::uint32_t table_sectors = le32(mbr->head, kNumSectorsField);
      const std::uint32_t table_start = le32(mbr->head, kFirstLbaField);
      const std::uint32_t fs_sectors =
          le32(boot->head, offsetof(Fat32BootSector, total_sectors_32));

      if (table_start != kStartSector) {
        std::cout << "❌ " << c.name << ": partition table starts the partition at "
                  << table_start << ", expected " << kStartSector << "\n";
        all_passed = false;
      }
      if (table_sectors != fs_sectors) {
        std::cout << "❌ " << c.name << ": partition table describes "
                  << table_sectors << " sectors but the filesystem inside it "
                     "describes " << fs_sectors << "\n";
        all_passed = false;
      }
      // And whichever they agree on must actually be on the device.
      const std::uint64_t device_sectors = c.size_bytes / 512;
      const std::uint64_t claimed_end =
          std::uint64_t{table_start} + table_sectors;
      if (claimed_end > device_sectors) {
        std::cout << "❌ " << c.name << ": the partition ends at sector "
                  << claimed_end << ", past the end of a device of "
                  << device_sectors << " sectors\n";
        all_passed = false;
      }
      if (std::uint64_t{table_start} + fs_sectors > device_sectors) {
        std::cout << "❌ " << c.name << ": the filesystem ends at sector "
                  << (std::uint64_t{table_start} + fs_sectors)
                  << ", past the end of a device of " << device_sectors
                  << " sectors\n";
        all_passed = false;
      }

      // The CHS end address, which some firmware still reads. Computing it as
      // start + count - 1 in 32-bit arithmetic wrapped on a large device --
      // 8192 + 4294967295 - 1 came out as 8190 -- and described a partition
      // ending before it began, with a sector number of 0 that CHS cannot
      // express at all.
      const Chs first = readChs(mbr->head, kPartitionEntry + 1);
      const Chs last = readChs(mbr->head, kPartitionEntry + 5);
      if (last.sector < 1 || last.sector > 63) {
        std::cout << "❌ " << c.name << ": the partition's last CHS sector is "
                  << last.sector << ", outside the 1-63 CHS allows\n";
        all_passed = false;
      }
      const auto chs_order = [](const Chs& a) {
        return std::make_tuple(a.cylinder, a.head, a.sector);
      };
      if (chs_order(last) < chs_order(first)) {
        std::cout << "❌ " << c.name << ": the partition's CHS end ("
                  << last.cylinder << "/" << last.head << "/" << last.sector
                  << ") is before its CHS start (" << first.cylinder << "/"
                  << first.head << "/" << first.sector << ")\n";
        all_passed = false;
      }

      // Agreement alone is not enough: two numbers that agree on a tiny
      // partition would satisfy every check above while throwing the card
      // away. The partition has to cover everything after the start offset,
      // or -- where the device is larger than a 32-bit sector count can
      // describe -- as much of it as MBR can reach.
      const std::uint64_t reachable =
          std::min<std::uint64_t>(device_sectors - kStartSector, 0xFFFFFFFFULL);
      if (table_sectors != reachable) {
        std::cout << "❌ " << c.name << ": the partition covers "
                  << table_sectors << " sectors, but " << reachable
                  << " of the device are reachable through an MBR\n";
        all_passed = false;
      }
    }

    // Below the 4 MB start offset plus a usable minimum there is nothing to
    // format, and both entry points have to say so rather than write a table
    // over a card that cannot hold it. FormatFile had no such check.
    const std::uint64_t kTooSmall[] = {0, 64 * 1024, 4 * 1024 * 1024,
                                       5 * 1024 * 1024 - 1};
    for (std::uint64_t size : kTooSmall) {
      {
        auto device = std::make_unique<ScriptedDevice>(size);
        auto* raw = device.get();
        DiskFormatter formatter(std::move(device));
        auto result = formatter.FormatDrive("/dev/fake");
        if (result || result.error() != FormatError::kInsufficientSpace) {
          std::cout << "❌ FormatDrive on a " << size
                    << " byte device did not report insufficient space\n";
          all_passed = false;
        }
        if (raw->writes() != 0) {
          std::cout << "❌ FormatDrive wrote to a " << size
                    << " byte device before refusing it\n";
          all_passed = false;
        }
      }
      {
        auto device = std::make_unique<ScriptedDevice>(size);
        auto* raw = device.get();
        DiskFormatter formatter(std::move(device));
        auto result = formatter.FormatFile("unused.img", size);
        if (result || result.error() != FormatError::kInsufficientSpace) {
          std::cout << "❌ FormatFile of a " << size
                    << " byte file did not report insufficient space\n";
          all_passed = false;
        }
        if (raw->writes() != 0) {
          std::cout << "❌ FormatFile wrote to a " << size
                    << " byte file before refusing it\n";
          all_passed = false;
        }
      }
    }

    if (all_passed)
      std::cout << "✅ the partition table and the filesystem agree at every size\n";
    return all_passed;
  }

  // FAT32 is defined by how many clusters the volume has, not by what its boot
  // sector claims. Below 65525 the specification says the volume is FAT16, and
  // a driver that enforces that will reject or misread one labelled FAT32.
  // Linux's vfat driver does not enforce it -- which is why the 64 MB image the
  // cases above format mounted cleanly, passed fsck.fat, and still held only
  // 60,944 clusters. It was not a filesystem Windows would accept.
  //
  // So the rule is: every device the formatter accepts must come out a valid
  // FAT32, and any device it cannot manage that for must be refused. There is
  // no third outcome. The cluster count is recomputed here from the fields
  // written to the boot sector, the way a driver would, rather than by asking
  // the formatter what it meant to do.
  static bool TestAcceptedDevicesGetAValidFat32() {
    std::cout << "Testing that an accepted device gets a real FAT32...\n";

    constexpr std::uint32_t kMinimumClusters = 65525;
    constexpr std::uint32_t kMaximumClusters = 268435444;  // FAT32's ceiling
    constexpr std::uint64_t kMiB = 1024ULL * 1024;
    constexpr std::uint64_t kGiB = 1024ULL * kMiB;
    constexpr std::uint64_t kStartSector = 8192;

    const std::uint64_t kSizes[] = {
      64 * 1024, 1 * kMiB, 4 * kMiB, 5 * kMiB, 16 * kMiB, 32 * kMiB,
      36 * kMiB, 37 * kMiB, 40 * kMiB, 48 * kMiB, 64 * kMiB, 96 * kMiB,
      128 * kMiB, 256 * kMiB, 260 * kMiB, 512 * kMiB,
      1 * kGiB, 2 * kGiB, 4 * kGiB, 8 * kGiB, 16 * kGiB, 32 * kGiB,
      64 * kGiB, 128 * kGiB, 256 * kGiB, 1024 * kGiB, 2048 * kGiB,
    };

    bool all_passed = true;
    int accepted = 0;
    int refused = 0;

    for (std::uint64_t size : kSizes) {
      auto device = std::make_unique<ScriptedDevice>(size);
      auto* raw = device.get();
      // Stop after the boot sector: every field read below is in it, and the
      // FAT copies for a large card run to hundreds of megabytes.
      device->FailWrite(3, FileError::kWriteError);
      DiskFormatter formatter(std::move(device));
      auto result = formatter.FormatDrive("/dev/fake");

      const auto* boot = raw->writeAt(kStartSector * 512);
      if (!result && result.error() == FormatError::kInsufficientSpace) {
        ++refused;
        if (boot != nullptr) {
          std::cout << "❌ " << (size / kMiB)
                    << " MB was refused but a boot sector was written anyway\n";
          all_passed = false;
        }
        continue;
      }
      if (boot == nullptr) {
        std::cout << "❌ " << (size / kMiB)
                  << " MB was neither refused nor given a boot sector\n";
        all_passed = false;
        continue;
      }
      ++accepted;

      // Read the volume back the way a FAT32 driver computes its cluster
      // count: total sectors, less the reserved region and both FATs, divided
      // by the cluster size.
      const std::uint32_t total_sectors =
          le32(boot->head, offsetof(Fat32BootSector, total_sectors_32));
      const std::uint32_t sectors_per_fat =
          le32(boot->head, offsetof(Fat32BootSector, sectors_per_fat_32));
      const unsigned sectors_per_cluster =
          boot->head[offsetof(Fat32BootSector, sectors_per_cluster)];
      const unsigned num_fats = boot->head[offsetof(Fat32BootSector, num_fats)];
      const unsigned reserved =
          boot->head[offsetof(Fat32BootSector, reserved_sectors)] |
          (boot->head[offsetof(Fat32BootSector, reserved_sectors) + 1] << 8);

      if (sectors_per_cluster == 0 || num_fats == 0 || reserved == 0) {
        std::cout << "❌ " << (size / kMiB) << " MB: boot sector describes "
                  << sectors_per_cluster << " sectors per cluster, " << num_fats
                  << " FATs, " << reserved << " reserved sectors\n";
        all_passed = false;
        continue;
      }
      // A cluster size has to be a power of two for FAT32.
      if ((sectors_per_cluster & (sectors_per_cluster - 1)) != 0) {
        std::cout << "❌ " << (size / kMiB) << " MB: " << sectors_per_cluster
                  << " sectors per cluster is not a power of two\n";
        all_passed = false;
      }

      const std::uint64_t overhead =
          reserved + std::uint64_t{num_fats} * sectors_per_fat;
      if (overhead >= total_sectors) {
        std::cout << "❌ " << (size / kMiB) << " MB: the reserved region and "
                     "FATs (" << overhead << " sectors) fill the whole "
                  << total_sectors << " sector partition\n";
        all_passed = false;
        continue;
      }
      const std::uint64_t clusters =
          (total_sectors - overhead) / sectors_per_cluster;

      if (clusters < kMinimumClusters) {
        std::cout << "❌ " << (size / kMiB) << " MB was accepted but holds only "
                  << clusters << " clusters, under the " << kMinimumClusters
                  << " FAT32 requires -- what was written is not a FAT32\n";
        all_passed = false;
      }
      if (clusters > kMaximumClusters) {
        std::cout << "❌ " << (size / kMiB) << " MB holds " << clusters
                  << " clusters, over the " << kMaximumClusters
                  << " FAT32 can address\n";
        all_passed = false;
      }
      // The FAT has to be long enough to hold an entry for every cluster it
      // claims, or the last clusters have nowhere to record their chain.
      const std::uint64_t fat_capacity =
          std::uint64_t{sectors_per_fat} * 512 / 4;
      if (fat_capacity < clusters + 2) {
        std::cout << "❌ " << (size / kMiB) << " MB: each FAT holds "
                  << fat_capacity << " entries but the volume has " << clusters
                  << " clusters to track\n";
        all_passed = false;
      }
    }

    // Both outcomes have to actually occur, or this test is only exercising
    // one of them.
    if (accepted == 0 || refused == 0) {
      std::cout << "❌ the sizes tried produced " << accepted << " accepted and "
                << refused << " refused; both are meant to be covered\n";
      all_passed = false;
    }

    if (all_passed)
      std::cout << "✅ every accepted device gets a valid FAT32 (" << accepted
                << " accepted, " << refused << " refused)\n";
    return all_passed;
  }

  // The FAT is written in bounded pieces, so peak memory does not scale with
  // the card.
  //
  // Each FAT copy for a 2 TiB card is half a gigabyte. It used to be
  // assembled in one allocation of that size and zeroed before a byte reached
  // the card, so formatting a large drive needed that much RAM up front; on a
  // Pi that does not have it the allocation failed and the only thing it could
  // be reported as was a device write error. What matters to a user is that
  // the FAT still lands correctly, so this checks the pieces tile each copy
  // exactly -- no gap, no overlap, nothing past the end -- that none exceeds
  // the bound, and that the three entries a FAT starts with are at the start
  // of each copy and nowhere else.
  static bool TestFatIsWrittenInBoundedPieces() {
    std::cout << "Testing that the FAT is written in bounded pieces...\n";

    constexpr std::uint64_t kStartSector = 8192;
    constexpr std::size_t kMaxPiece = 1024 * 1024;  // the 1 MB chunk
    constexpr std::uint64_t kGiB = 1024ULL * 1024 * 1024;

    struct Case {
      const char* name;
      std::uint64_t size_bytes;
    };
    const Case kCases[] = {
      {"64 MB", 64ULL * 1024 * 1024},   // one piece per copy
      {"32 GB", 32 * kGiB},             // a handful of pieces
      {"2 TiB", 2048 * kGiB},           // the largest an MBR can describe
    };

    bool all_passed = true;

    for (const auto& c : kCases) {
      auto device = std::make_unique<ScriptedDevice>(c.size_bytes);
      auto* raw = device.get();
      DiskFormatter formatter(std::move(device));
      auto result = formatter.FormatDrive("/dev/fake");
      if (!result) {
        std::cout << "❌ " << c.name << ": the format failed\n";
        all_passed = false;
        continue;
      }

      const auto* boot = raw->writeAt(kStartSector * 512);
      if (boot == nullptr) {
        std::cout << "❌ " << c.name << ": no boot sector was written\n";
        all_passed = false;
        continue;
      }
      const std::uint32_t sectors_per_fat =
          le32(boot->head, offsetof(Fat32BootSector, sectors_per_fat_32));
      const unsigned num_fats = boot->head[offsetof(Fat32BootSector, num_fats)];
      const unsigned reserved =
          boot->head[offsetof(Fat32BootSector, reserved_sectors)] |
          (boot->head[offsetof(Fat32BootSector, reserved_sectors) + 1] << 8);

      for (unsigned fat = 0; fat < num_fats; ++fat) {
        const std::uint64_t start =
            (kStartSector + reserved +
             std::uint64_t{fat} * sectors_per_fat) * 512;
        const std::uint64_t length = std::uint64_t{sectors_per_fat} * 512;

        // Every write that landed inside this copy, in the order it was made.
        std::vector<const ScriptedDevice::Write*> pieces;
        for (const auto& w : raw->writeLog())
          if (w.offset >= start && w.offset < start + length)
            pieces.push_back(&w);

        if (pieces.empty()) {
          std::cout << "❌ " << c.name << ": FAT copy " << fat
                    << " was never written\n";
          all_passed = false;
          continue;
        }

        std::uint64_t cursor = start;
        bool tiled = true;
        for (std::size_t i = 0; i < pieces.size(); ++i) {
          const auto* w = pieces[i];
          if (w->offset != cursor) {
            std::cout << "❌ " << c.name << ": FAT copy " << fat << " piece "
                      << i << " starts at " << w->offset << ", expected "
                      << cursor << " -- the pieces leave a gap or overlap\n";
            all_passed = false;
            tiled = false;
            break;
          }
          if (w->size > kMaxPiece) {
            std::cout << "❌ " << c.name << ": FAT copy " << fat
                      << " was written in a piece of " << w->size
                      << " bytes, over the " << kMaxPiece << " byte bound\n";
            all_passed = false;
          }
          // The three entries belong at the start of the copy and nowhere
          // else; every later piece begins as zeros.
          if (i == 0) {
            if (le32(w->head, 0) != 0x0FFFFFF8u ||
                le32(w->head, 4) != 0x0FFFFFFFu ||
                le32(w->head, 8) != 0x0FFFFFFFu) {
              std::cout << "❌ " << c.name << ": FAT copy " << fat
                        << " does not start with the three reserved entries\n";
              all_passed = false;
            }
          } else if (le32(w->head, 0) != 0 || le32(w->head, 4) != 0) {
            std::cout << "❌ " << c.name << ": FAT copy " << fat << " piece "
                      << i << " does not begin as zeros\n";
            all_passed = false;
          }
          cursor += w->size;
        }

        if (tiled && cursor != start + length) {
          std::cout << "❌ " << c.name << ": FAT copy " << fat
                    << " was written up to " << cursor << ", but the boot "
                       "sector says it runs to " << (start + length) << "\n";
          all_passed = false;
        }
      }

      // And a large card really is written in more than one piece, or the
      // bound above is not being exercised at all.
      if (c.size_bytes >= 32 * kGiB) {
        const std::uint64_t fat_bytes = std::uint64_t{sectors_per_fat} * 512;
        if (fat_bytes <= kMaxPiece) {
          std::cout << "❌ " << c.name << ": each FAT is only " << fat_bytes
                    << " bytes, so this size does not exercise chunking\n";
          all_passed = false;
        }
      }
    }

    if (all_passed)
      std::cout << "✅ the FAT is written in bounded pieces that tile it exactly\n";
    return all_passed;
  }
};

int main() {
  try {
    bool success = DiskFormatterTest::RunAllTests();
    return success ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "Test failed with exception: " << e.what() << "\n";
    return 1;
  }
} 
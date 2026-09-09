#ifndef RPI_TEST_LOOP_DEVICE_H
#define RPI_TEST_LOOP_DEVICE_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * A loopback block device backed by a scratch image.
 *
 * Some of what the writer does only happens against a block device: O_DIRECT
 * is set for a block device path and nothing else, the async queue depth
 * comes back as 1 for a regular file, and the kernel's rule that a direct
 * write must be a whole number of sectors is not enforced anywhere a scratch
 * file can reach. A caller that forgets to pad its last block therefore looks
 * perfectly correct until it meets a real card.
 *
 * Yields an empty path where the host will not give one out -- no
 * CAP_SYS_ADMIN and no passwordless sudo, which is the normal case in CI and
 * in containers. Callers skip rather than fail.
 */

#include <algorithm>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace rpi_test {

// Run a command, capturing stdout. Returns its exit status, or -1.
inline int runCapture(const char* path, const std::vector<const char*>& argv,
                      std::string* out)
{
    int pipefd[2];
    if (::pipe(pipefd) != 0)
        return -1;

    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        ::close(pipefd[0]);
        ::dup2(pipefd[1], STDOUT_FILENO);
        ::close(pipefd[1]);
        int devnull = ::open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            ::dup2(devnull, STDERR_FILENO);
            ::close(devnull);
        }
        ::execv(path, const_cast<char* const*>(argv.data()));
        ::_exit(127);
    }

    ::close(pipefd[1]);
    char buf[256] = {};
    ssize_t n = ::read(pipefd[0], buf, sizeof(buf) - 1);
    ::close(pipefd[0]);
    int status = 0;
    ::waitpid(pid, &status, 0);
    if (out && n > 0)
        out->assign(buf, static_cast<std::size_t>(n));
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

class LoopDevice {
public:
    explicit LoopDevice(const std::string& backingFile)
    {
        std::string out;
        // -P so the partition table inside the image is scanned; several of
        // the things worth testing live in partitions, not at raw offset 0.
        if (losetup({"--find", "--show", "-P", backingFile.c_str()}, &out) != 0)
            return;

        // Trim, then insist on exactly /dev/loop<digits> before this string is
        // ever handed to a detach command.
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
            out.pop_back();
        if (!isLoopPath(out))
            return;

        device_ = out;

        // losetup creates /dev/loopN as root:disk, mode 0660. A developer is
        // normally not in the disk group -- and should not have to be, since
        // that is a standing grant of raw access to every disk on the machine
        // -- so without this the test attaches a device it then cannot open.
        // Scoped to the device this object just created, and gone again when
        // it is detached.
        if (::geteuid() != 0) {
            std::string ignored;
            chownDevice(device_, &ignored);
        }
    }

    ~LoopDevice()
    {
        if (device_.empty())
            return;
        std::string ignored;
        losetup({"-d", device_.c_str()}, &ignored);
    }

    LoopDevice(const LoopDevice&) = delete;
    LoopDevice& operator=(const LoopDevice&) = delete;

    bool valid() const { return !device_.empty(); }
    const std::string& path() const { return device_; }

private:
    // Run losetup, directly when already root and via sudo -n otherwise.
    // Going straight to losetup matters for rootful CI containers and VMs,
    // where sudo is frequently not installed at all; -n on the fallback means
    // a host that would prompt for a password fails immediately instead of
    // hanging the suite.
    static int losetup(std::vector<const char*> args, std::string* out)
    {
        std::vector<const char*> argv;
        const char* binary = nullptr;
        if (::geteuid() == 0) {
            binary = "/usr/sbin/losetup";
            argv.push_back("losetup");
        } else {
            binary = "/usr/bin/sudo";
            argv.insert(argv.end(), {"sudo", "-n", "losetup"});
        }
        argv.insert(argv.end(), args.begin(), args.end());
        argv.push_back(nullptr);
        return runCapture(binary, argv, out);
    }

    // Hand the node to the user running the suite. chown to this uid rather
    // than chmod 0666: the node only needs to be reachable by the process
    // under test, and making a block device world-writable for the duration
    // -- even a synthetic one -- is a wider grant than the job needs.
    static int chownDevice(const std::string& device, std::string* out)
    {
        const std::string uid = std::to_string(::geteuid());
        std::vector<const char*> argv = {"sudo", "-n", "chown", uid.c_str(),
                                         device.c_str(), nullptr};
        return runCapture("/usr/bin/sudo", argv, out);
    }

    static bool isLoopPath(const std::string& s)
    {
        if (s.rfind("/dev/loop", 0) != 0)
            return false;
        const std::string digits = s.substr(9);
        if (digits.empty())
            return false;
        return std::all_of(digits.begin(), digits.end(),
                           [](unsigned char c) { return c >= '0' && c <= '9'; });
    }

    std::string device_;
};

} // namespace rpi_test

#endif // RPI_TEST_LOOP_DEVICE_H

// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "linux_shared_memory.h"

#include <sys/mman.h>
#include <unistd.h>
#include <utility>

namespace rpi_imager::privileged::wire {

namespace {

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

int memfdCreate(const char* name) {
#if defined(__linux__)
    return ::memfd_create(name, MFD_CLOEXEC);
#else
    (void)name;
    return -1;
#endif
}

} // namespace

LinuxSharedMemory::~LinuxSharedMemory() {
    release();
}

LinuxSharedMemory::LinuxSharedMemory(LinuxSharedMemory&& other) noexcept
    : fd_(std::exchange(other.fd_, -1)),
      base_(std::exchange(other.base_, nullptr)),
      size_(std::exchange(other.size_, 0)) {}

LinuxSharedMemory& LinuxSharedMemory::operator=(LinuxSharedMemory&& other) noexcept {
    if (this != &other) {
        release();
        fd_ = std::exchange(other.fd_, -1);
        base_ = std::exchange(other.base_, nullptr);
        size_ = std::exchange(other.size_, 0);
    }
    return *this;
}

bool LinuxSharedMemory::createOwned(std::size_t size_bytes) {
    release();
    if (size_bytes == 0) {
        return false;
    }

    const int fd = memfdCreate("rpi-imager-bulk");
    if (fd < 0) {
        return false;
    }
    if (::ftruncate(fd, static_cast<off_t>(size_bytes)) != 0) {
        ::close(fd);
        return false;
    }

    void* base = ::mmap(nullptr, size_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) {
        ::close(fd);
        return false;
    }

    fd_ = fd;
    base_ = base;
    size_ = size_bytes;
    return true;
}

bool LinuxSharedMemory::adoptFromFd(int fd, std::size_t size_bytes) {
    release();
    if (fd < 0 || size_bytes == 0) {
        return false;
    }

    void* base = ::mmap(nullptr, size_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) {
        ::close(fd);
        return false;
    }

    fd_ = fd;
    base_ = base;
    size_ = size_bytes;
    return true;
}

void LinuxSharedMemory::release() {
    if (base_ != nullptr && base_ != MAP_FAILED) {
        ::munmap(base_, size_);
        base_ = nullptr;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    size_ = 0;
}

} // namespace rpi_imager::privileged::wire

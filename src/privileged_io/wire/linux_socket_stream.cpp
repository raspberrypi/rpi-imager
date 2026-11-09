// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "linux_socket_stream.h"

#include "linux_socket_io.h"

#include <cerrno>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace rpi_imager::privileged::wire {

namespace {

thread_local int t_last_write_error = 0;

bool failWrite() {
    t_last_write_error = errno;
    return false;
}

} // namespace

LinuxSocketStream::LinuxSocketStream(int fd) : fd_(fd) {}

bool LinuxSocketStream::write(const char* data, std::size_t len, int pass_fd) {
    std::lock_guard<std::mutex> lk(write_mutex_);
    if (pass_fd >= 0) {
        // SCM_RIGHTS rides on a sendmsg of the frame itself.
        return sendFrame(fd_, std::string(data, len), pass_fd) || failWrite();
    }
    std::size_t off = 0;
    while (off < len) {
        const ssize_t n = ::write(fd_, data + off, len - off);
        if (n <= 0) {
            return failWrite();
        }
        off += static_cast<std::size_t>(n);
    }
    return true;
}

StreamChunk LinuxSocketStream::read(char* buf, std::size_t cap) {
    StreamChunk out;
    if (cancelled_.load()) {
        return out;
    }
    char cmsg_buf[CMSG_SPACE(sizeof(int))] = {0};
    struct iovec iov {};
    iov.iov_base = buf;
    iov.iov_len = cap;
    struct msghdr msg {};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsg_buf;
    msg.msg_controllen = sizeof(cmsg_buf);

    const ssize_t n = ::recvmsg(fd_, &msg, 0);
    if (n <= 0) {
        return out;
    }
    out.len = static_cast<std::size_t>(n);
    for (struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr;
         cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS &&
            cmsg->cmsg_len == CMSG_LEN(sizeof(int))) {
            std::memcpy(&out.ancillary_fd, CMSG_DATA(cmsg), sizeof(out.ancillary_fd));
        }
    }
    out.ok = true;
    return out;
}

void LinuxSocketStream::cancel() {
    cancelled_.store(true);
    // Unlike closing it, shutdown wakes a recvmsg already blocked on the
    // socket, and every later one returns at once.
    (void)::shutdown(fd_, SHUT_RDWR);
}

int LinuxSocketStream::lastError() const {
    return t_last_write_error;
}

} // namespace rpi_imager::privileged::wire

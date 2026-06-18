// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "linux_socket_io.h"

#include <cerrno>
#include <cstring>

#include <sys/socket.h>
#include <unistd.h>

namespace rpi_imager::privileged::wire {

bool writeAll(int fd, const char* data, std::size_t len) {
    std::size_t off = 0;
    while (off < len) {
        const ssize_t n = ::write(fd, data + off, len - off);
        if (n <= 0) {
            return false;
        }
        off += static_cast<std::size_t>(n);
    }
    return true;
}

bool readAll(int fd, char* data, std::size_t len) {
    std::size_t off = 0;
    while (off < len) {
        const ssize_t n = ::read(fd, data + off, len - off);
        if (n <= 0) {
            return false;
        }
        off += static_cast<std::size_t>(n);
    }
    return true;
}

bool sendFrame(int fd, const std::string& frame, int pass_fd) {
    struct iovec iov {};
    iov.iov_base = const_cast<char*>(frame.data());
    iov.iov_len = frame.size();

    char cmsg_buf[CMSG_SPACE(sizeof(int))] = {0};
    struct msghdr msg {};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    if (pass_fd >= 0) {
        msg.msg_control = cmsg_buf;
        msg.msg_controllen = sizeof(cmsg_buf);
        struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int));
        std::memcpy(CMSG_DATA(cmsg), &pass_fd, sizeof(pass_fd));
    }

    const ssize_t n = ::sendmsg(fd, &msg, 0);
    return n == static_cast<ssize_t>(frame.size());
}

bool recvFrame(int fd, std::string& out_frame, int& out_pass_fd) {
    out_pass_fd = -1;
    out_frame.clear();

    char buf[8192];
    char cmsg_buf[CMSG_SPACE(sizeof(int))] = {0};
    struct iovec iov {};
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);

    struct msghdr msg {};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsg_buf;
    msg.msg_controllen = sizeof(cmsg_buf);

    const ssize_t n = ::recvmsg(fd, &msg, 0);
    if (n <= 0) {
        return false;
    }

    for (struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr;
         cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS &&
            cmsg->cmsg_len == CMSG_LEN(sizeof(int))) {
            std::memcpy(&out_pass_fd, CMSG_DATA(cmsg), sizeof(out_pass_fd));
        }
    }

    out_frame.append(buf, static_cast<std::size_t>(n));
    return true;
}

} // namespace rpi_imager::privileged::wire

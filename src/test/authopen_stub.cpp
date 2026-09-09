// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

// Stands in for /usr/libexec/authopen: opens a file and hands the descriptor
// back over stdout via SCM_RIGHTS. Lets macfile_test drive openViaHelper's
// receive, waitpid and cleanup paths without an authorisation prompt.
//
//   authopen_stub <mode> <path>
//     fd            open path, send it, exit 0
//     nofd          send a byte carrying no descriptor
//     fail          send nothing, exit 3
//     fd-then-fail  send the descriptor, then exit 3
//     silent        send nothing, exit 0

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace {

bool sendDescriptor(int socket, int fd)
{
    char byte = 'x';
    struct iovec io = {&byte, 1};
    char control[CMSG_SPACE(sizeof(int))] = {};

    struct msghdr msg = {};
    msg.msg_iov = &io;
    msg.msg_iovlen = 1;

    if (fd >= 0) {
        msg.msg_control = control;
        msg.msg_controllen = sizeof(control);
        struct cmsghdr *c = CMSG_FIRSTHDR(&msg);
        c->cmsg_level = SOL_SOCKET;
        c->cmsg_type = SCM_RIGHTS;
        c->cmsg_len = CMSG_LEN(sizeof(int));
        std::memcpy(CMSG_DATA(c), &fd, sizeof(int));
    }
    return sendmsg(socket, &msg, 0) > 0;
}

}  // namespace

int main(int argc, char **argv)
{
    if (argc < 2)
        return 2;
    const std::string mode = argv[1];

    // Drain stdin, as authopen does with the external form, so the parent's
    // write does not sit in a full pipe or take SIGPIPE.
    char scratch[256];
    while (::read(STDIN_FILENO, scratch, sizeof(scratch)) > 0) {}

    if (mode == "fail")
        return 3;
    if (mode == "silent")
        return 0;

    if (mode == "nofd") {
        sendDescriptor(STDOUT_FILENO, -1);
        return 0;
    }

    if (argc < 3)
        return 2;
    int fd = ::open(argv[2], O_RDWR);
    if (fd < 0)
        return 4;
    if (!sendDescriptor(STDOUT_FILENO, fd))
        return 5;
    ::close(fd);

    return mode == "fd-then-fail" ? 3 : 0;
}

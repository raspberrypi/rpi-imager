/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include "macfile.h"
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <security/Authorization.h>
#include <QDebug>
#include <QCoreApplication>

namespace rpi_imager::mac {

int openViaHelper(const char *program, const char *const argv[],
                  const QByteArray &stdinData)
{
    // Unchecked before, which left the descriptors below uninitialised on a
    // machine that had run out.
    int sock[2];
    int stdinpipe[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sock) != 0)
        return -1;
    if (::pipe(stdinpipe) != 0)
    {
        ::close(sock[0]);
        ::close(sock[1]);
        return -1;
    }

    pid_t pid = ::fork();
    if (pid < 0)
    {
        ::close(sock[0]);
        ::close(sock[1]);
        ::close(stdinpipe[0]);
        ::close(stdinpipe[1]);
        return -1;
    }
    if (pid == 0)
    {
        // child
        ::close(sock[0]);
        ::close(stdinpipe[1]);
        ::dup2(sock[1], STDOUT_FILENO);
        ::dup2(stdinpipe[0], STDIN_FILENO);
        ::execv(program, const_cast<char *const *>(argv));
        ::_exit(-1);
    }

    ::close(sock[1]);
    ::close(stdinpipe[0]);
    if (!stdinData.isEmpty())
        (void)::write(stdinpipe[1], stdinData.constData(), stdinData.size());
    ::close(stdinpipe[1]);

    const size_t bufSize = CMSG_SPACE(sizeof(int));
    char buf[bufSize];
    struct iovec io_vec[1];
    io_vec[0].iov_base = buf;
    io_vec[0].iov_len = bufSize;
    const size_t cmsgSize = CMSG_SPACE(sizeof(int));
    char cmsg[cmsgSize];

    struct msghdr msg = {};
    msg.msg_iov = io_vec;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsg;
    msg.msg_controllen = cmsgSize;

    ssize_t size;
    do {
        size = ::recvmsg(sock[0], &msg, 0);
    } while (size == -1 && errno == EINTR);

    int fd = -1;
    if (size > 0)
    {
        struct cmsghdr *chdr = CMSG_FIRSTHDR(&msg);
        if (chdr && chdr->cmsg_type == SCM_RIGHTS)
            fd = *((int *)(CMSG_DATA(chdr)));
        else
            qDebug() << "helper sent data but no descriptor";
    }
    ::close(sock[0]);  // Leaked on every path before, success included.

    pid_t wpid;
    int status;
    do {
        wpid = ::waitpid(pid, &status, 0);
    } while (wpid == -1 && errno == EINTR);

    // A descriptor arriving from a helper that then failed is not one to use,
    // and WEXITSTATUS is meaningless unless the child actually exited.
    const bool ok = wpid != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (!ok)
    {
        qDebug() << "helper failed:" << program << "status" << status;
        if (fd >= 0)
            ::close(fd);
        return -1;
    }
    return fd;
}

}  // namespace rpi_imager::mac

MacFile::MacFile(QObject *parent)
    : QFile(parent)
{

}

/* Prevent that Qt thinks /dev/rdisk does not permit seeks because it does not report size */
bool MacFile::isSequential() const
{
    return false;
}

MacFile::authOpenResult MacFile::authOpen(const QByteArray &filename)
{
    int fd = -1;

    QByteArray right = "sys.openfile.readwrite."+filename;
    AuthorizationItem item = {right, 0, nullptr, 0};
    AuthorizationRights rights = {1, &item};
    AuthorizationFlags flags = kAuthorizationFlagInteractionAllowed |
            kAuthorizationFlagExtendRights |
            kAuthorizationFlagPreAuthorize;
    
    // Create authorization environment with custom prompt
    // This provides better context in the authorization dialog
    QString promptText = QCoreApplication::translate("MacFile", "Raspberry Pi Imager needs to access the disk to write the image.");
    QByteArray promptBytes = promptText.toUtf8();
    const char *promptKey = "prompt";
    AuthorizationItem envItems[] = {
        { promptKey, (size_t)promptBytes.length(), (void*)promptBytes.constData(), 0 }
    };
    AuthorizationEnvironment env = { 1, envItems };
    
    AuthorizationRef authRef;
    if (AuthorizationCreate(&rights, &env, flags, &authRef) != 0)
        return authOpenCancelled;

    AuthorizationExternalForm externalForm;
    if (AuthorizationMakeExternalForm(authRef, &externalForm) != 0)
    {
        AuthorizationFree(authRef, 0);
        return authOpenError;
    }

    const char *cmd = "/usr/libexec/authopen";
    QByteArray mode = QByteArray::number(O_RDWR);
    const char *argv[] = { cmd, "-stdoutpipe", "-extauth", "-o", mode.data(),
                           filename.data(), nullptr };
    fd = rpi_imager::mac::openViaHelper(
        cmd, argv,
        QByteArray(reinterpret_cast<const char *>(externalForm.bytes),
                   sizeof(externalForm.bytes)));

    AuthorizationFree(authRef, 0);

    if (fd < 0)
        return authOpenError;

    return open(fd, QIODevice::ReadWrite | QIODevice::ExistingOnly | QIODevice::Unbuffered, QFileDevice::AutoCloseHandle) ? authOpenSuccess : authOpenError;
}

bool MacFile::forceSync()
{
    if (!isOpen()) {
        qDebug() << "Warning: Cannot sync closed file";
        return false;
    }
    
    // Flush Qt's internal buffers first
    if (!flush()) {
        qDebug() << "Warning: flush() failed during forceSync:" << errorString();
        return false;
    }
    
    // Force filesystem sync using fsync
    if (::fsync(handle()) != 0) {
        qDebug() << "Warning: fsync() failed during forceSync, errno:" << errno;
        return false;
    }
    
    return true;
}

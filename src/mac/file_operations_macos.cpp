/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "file_operations_macos.h"
#include "macfile.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <iostream>
#include <QDebug>

namespace rpi_imager
{

    MacOSFileOperations::MacOSFileOperations() : fd_(-1) {}

    MacOSFileOperations::~MacOSFileOperations()
    {
        Close();
    }

    // Map a few common errno values to cross-platform details
    DetailedError MacOSFileOperations::MapErrnoDetail(int e) {
        switch (e) {
        case ENOSPC:  return DetailedError::kNoSpace;          // no space left
        case EROFS:   return DetailedError::kWriteProtected;    // read-only FS
        case EBUSY:   return DetailedError::kBusy;              // device/file busy
        case EACCES:
        case EPERM:   return DetailedError::kAccessDenied;      // permissions
        case EINVAL:  return DetailedError::kInvalidParameter;  // bad arg/offset
        case EIO:
        case ENXIO:   return DetailedError::kIoDevice;          // I/O error
        default:      return DetailedError::kNoDetails;
        }
    }

    // Helpers to record/clear error state
    #define SET_LAST_ERR_FROM_ERRNO(_coarse_) do { \
        int _e = errno; \
            last_error_ = { (_coarse_), MapErrnoDetail(_e), _e}; \
        } while(0)

    #define SET_LAST_ERR(_coarse_, _detail_) do { \
            last_error_ = { (_coarse_), (_detail_), 0 }; \
        } while(0)

    #define CLEAR_LAST_ERR() do { last_error_ = {}; } while(0)

    FileError MacOSFileOperations::OpenDevice(const std::string &path)
    {
        std::cout << "Opening macOS device: " << path << std::endl;

        // For raw device access on macOS, we need to use the authorization mechanism
        // Similar to how MacFile::authOpen() works
        if (path.find("/dev/") == 0)
        {
            std::cout << "Device path detected, using macOS authorization..." << std::endl;

            // Create a MacFile instance to handle authorization
            MacFile macfile;
            QByteArray devicePath = QByteArray::fromStdString(path);

            auto authResult = macfile.authOpen(devicePath);
            if (authResult == MacFile::authOpenCancelled)
            {
                std::cout << "Authorization cancelled by user" << std::endl;
                SET_LAST_ERR(FileError::kOpenError, DetailedError::kAccessDenied);
                return FileError::kOpenError;
            }
            else if (authResult == MacFile::authOpenError)
            {
                std::cout << "Authorization failed" << std::endl;
                SET_LAST_ERR(FileError::kOpenError, DetailedError::kNoDetails);
                return FileError::kOpenError;
            }

            // Get the file descriptor from the authorized MacFile
            fd_ = macfile.handle();
            if (fd_ < 0)
            {
                SET_LAST_ERR_FROM_ERRNO(FileError::kOpenError);
                std::cout << "Failed to get file descriptor from MacFile" << std::endl;
                return FileError::kOpenError;
            }

            // Duplicate the file descriptor so we can manage it independently
            int duplicated_fd = dup(fd_);
            if (duplicated_fd < 0)
            {
                SET_LAST_ERR_FROM_ERRNO(FileError::kOpenError);
                std::cout << "Failed to duplicate file descriptor" << std::endl;
                return FileError::kOpenError;
            }

            // Close the MacFile (this will close the original fd)
            macfile.close();

            // Use our duplicated fd
            fd_ = duplicated_fd;
            current_path_ = path;

            std::cout << "Successfully opened device with authorization, fd=" << fd_ << std::endl;
            CLEAR_LAST_ERR();
            return FileError::kSuccess;
        }

        // For regular files, use standard POSIX open
        return OpenInternal(path.c_str(), O_RDWR | O_SYNC);
    }

    FileError MacOSFileOperations::CreateTestFile(const std::string &path, std::uint64_t size)
    {
        // First create/truncate the file
        FileError result = OpenInternal(path.c_str(),
                                        O_CREAT | O_RDWR | O_TRUNC,
                                        S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (result != FileError::kSuccess)
        {
            // last error is alread set by OpenInternal
            return result;
        }

        // Set the file size
        if (ftruncate(fd_, static_cast<off_t>(size)) != 0)
        {
            SET_LAST_ERR_FROM_ERRNO(FileError::kSizeError);
            Close();
            return FileError::kSizeError;
        }

        CLEAR_LAST_ERR();
        return FileError::kSuccess;
    }

    FileError MacOSFileOperations::WriteAtOffset(
        std::uint64_t offset,
        const std::uint8_t *data,
        std::size_t size)
    {

        if (!IsOpen())
        {
            last_error_ = {FileError::kOpenError, DetailedError::kNoDetails, 0};
            std::cout << "WriteAtOffset: Device not open" << std::endl;
            return FileError::kOpenError;
        }

        if (lseek(fd_, static_cast<off_t>(offset), SEEK_SET) == -1)
        {
            SET_LAST_ERR_FROM_ERRNO(FileError::kSeekError);
            std::cout << "WriteAtOffset: lseek failed, offset=" << offset << ", errno=" << errno << std::endl;
            return FileError::kSeekError;
        }

        std::size_t bytes_written = 0;
        while (bytes_written < size)
        {
            ssize_t result = write(fd_, data + bytes_written, size - bytes_written);
            if (result <= 0)
            {
                SET_LAST_ERR_FROM_ERRNO(FileError::kWriteError);
                std::cout << "WriteAtOffset: write failed, bytes_written=" << bytes_written << ", errno=" << errno << std::endl;
                return FileError::kWriteError;
            }
            bytes_written += static_cast<std::size_t>(result);
        }

        std::cout << "WriteAtOffset: Successfully wrote " << bytes_written << " bytes at offset " << offset << std::endl;
        CLEAR_LAST_ERR();
        return FileError::kSuccess;
    }

    FileError MacOSFileOperations::GetSize(std::uint64_t &size)
    {
        if (!IsOpen())
        {
            last_error_ = {FileError::kOpenError, DetailedError::kNoDetails, 0};
            return FileError::kOpenError;
        }

        struct stat st;
        if (fstat(fd_, &st) != 0)
        {
            SET_LAST_ERR(FileError::kSizeError, DetailedError::kNoDetails);
            return FileError::kSizeError;
        }

        size = static_cast<std::uint64_t>(st.st_size);
        CLEAR_LAST_ERR();
        return FileError::kSuccess;
    }

    FileError MacOSFileOperations::Close()
    {
        if (fd_ >= 0)
        {
            if (close(fd_) != 0)
            {
                fd_ = -1;
                SET_LAST_ERR_FROM_ERRNO(FileError::kCloseError);
                return FileError::kCloseError;
            }
            fd_ = -1;
        }
        current_path_.clear();
        CLEAR_LAST_ERR();
        return FileError::kSuccess;
    }

    bool MacOSFileOperations::IsOpen() const
    {
        return fd_ >= 0;
    }

    FileError MacOSFileOperations::OpenInternal(const char *path, int flags, mode_t mode)
    {
        // Close any existing file
        Close();

        fd_ = open(path, flags, mode);
        if (fd_ < 0)
        {
            SET_LAST_ERR(FileError::kOpenError, DetailedError::kNoDetails);
            std::cout << "OpenInternal: Failed to open " << path << ", errno=" << errno << std::endl;
            return FileError::kOpenError;
        }

        current_path_ = path;
        std::cout << "OpenInternal: Successfully opened " << path << ", fd=" << fd_ << std::endl;
        CLEAR_LAST_ERR();
        return FileError::kSuccess;
    }

    // Streaming I/O operations
    FileError MacOSFileOperations::WriteSequential(const std::uint8_t *data, std::size_t size)
    {
        if (!IsOpen())
        {
            last_error_ = {FileError::kOpenError, DetailedError::kNoDetails, 0};
            return FileError::kOpenError;
        }

        std::size_t bytes_written = 0;
        while (bytes_written < size)
        {
            ssize_t result = write(fd_, data + bytes_written, size - bytes_written);
            if (result <= 0)
            {
                if (result == 0 || errno != EINTR)
                {
                    SET_LAST_ERR_FROM_ERRNO(FileError::kWriteError);
                    return FileError::kWriteError;
                }
                // EINTR - retry the write
                continue;
            }
            bytes_written += static_cast<std::size_t>(result);
        }

        CLEAR_LAST_ERR();
        return FileError::kSuccess;
    }

    FileError MacOSFileOperations::ReadSequential(std::uint8_t *data, std::size_t size, std::size_t &bytes_read)
    {
        if (!IsOpen())
        {
            last_error_ = {FileError::kOpenError, DetailedError::kNoDetails, 0};
            return FileError::kOpenError;
        }

        ssize_t result = read(fd_, data, size);
        if (result < 0)
        {
            bytes_read = 0;
            SET_LAST_ERR_FROM_ERRNO(FileError::kReadError);
            return FileError::kReadError;
        }

        bytes_read = static_cast<std::size_t>(result);
        CLEAR_LAST_ERR();
        return FileError::kSuccess;
    }

    FileError MacOSFileOperations::Seek(std::uint64_t position)
    {
        if (!IsOpen())
        {
            last_error_ = {FileError::kOpenError, DetailedError::kNoDetails, 0};
            return FileError::kOpenError;
        }

        if (lseek(fd_, static_cast<off_t>(position), SEEK_SET) == -1)
        {
            SET_LAST_ERR_FROM_ERRNO(FileError::kSeekError);
            return FileError::kSeekError;
        }

        CLEAR_LAST_ERR();
        return FileError::kSuccess;
    }

    std::uint64_t MacOSFileOperations::Tell() const
    {
        if (!IsOpen())
        {
            return 0;
        }

        off_t pos = lseek(fd_, 0, SEEK_CUR);
        return (pos == -1) ? 0 : static_cast<std::uint64_t>(pos);
    }

    FileError MacOSFileOperations::ForceSync()
    {
        if (!IsOpen())
        {
            last_error_ = {FileError::kOpenError, DetailedError::kNoDetails, 0};
            return FileError::kOpenError;
        }

        // Force filesystem sync using fsync - same logic as MacFile::forceSync()
        if (::fsync(fd_) != 0)
        {
            SET_LAST_ERR_FROM_ERRNO(FileError::kSyncError);
            return FileError::kSyncError;
        }

        CLEAR_LAST_ERR();
        return FileError::kSuccess;
    }

    FileError MacOSFileOperations::Flush()
    {
        if (!IsOpen())
        {
            last_error_ = {FileError::kOpenError, DetailedError::kNoDetails, 0};
            return FileError::kOpenError;
        }

        // On macOS, use fsync for both flush and sync operations
        if (::fsync(fd_) != 0)
        {
            SET_LAST_ERR_FROM_ERRNO(FileError::kFlushError);
            return FileError::kFlushError;
        }

        CLEAR_LAST_ERR();
        return FileError::kSuccess;
    }

    int MacOSFileOperations::GetHandle() const
    {
        return fd_;
    }

    // Platform-specific factory function implementation
    std::unique_ptr<FileOperations> CreatePlatformFileOperations()
    {
        return std::make_unique<MacOSFileOperations>();
    }

} // namespace rpi_imager

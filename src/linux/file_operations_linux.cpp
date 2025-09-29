/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "file_operations_linux.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

namespace rpi_imager
{

    LinuxFileOperations::LinuxFileOperations() : fd_(-1) {}

    LinuxFileOperations::~LinuxFileOperations()
    {
        Close();
    }

    // Map a few common errno values to cross-platform details
    DetailedError LinuxFileOperations::MapErrnoDetail(int e) {
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

    FileError LinuxFileOperations::OpenDevice(const std::string &path)
    {
        return OpenInternal(path.c_str(), O_RDWR | O_SYNC);
    }

    FileError LinuxFileOperations::CreateTestFile(const std::string &path, std::uint64_t size)
    {
        // First create/truncate the file
        FileError result = OpenInternal(path.c_str(),
                                        O_CREAT | O_RDWR | O_TRUNC,
                                        S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (result != FileError::kSuccess)
        {
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

    FileError LinuxFileOperations::WriteAtOffset(
        std::uint64_t offset,
        const std::uint8_t *data,
        std::size_t size)
    {

        if (!IsOpen())
        {
            last_error_ = {FileError::kOpenError, DetailedError::kNoDetails, 0};
            return FileError::kOpenError;
        }

        if (lseek(fd_, static_cast<off_t>(offset), SEEK_SET) == -1)
        {
            SET_LAST_ERR_FROM_ERRNO(FileError::kSeekError);
            return FileError::kSeekError;
        }

        std::size_t bytes_written = 0;
        while (bytes_written < size)
        {
            ssize_t result = write(fd_, data + bytes_written, size - bytes_written);
            if (result <= 0)
            {
                SET_LAST_ERR_FROM_ERRNO(FileError::kWriteError);
                return FileError::kWriteError;
            }
            bytes_written += static_cast<std::size_t>(result);
        }

        CLEAR_LAST_ERR();
        return FileError::kSuccess;
    }

    FileError LinuxFileOperations::GetSize(std::uint64_t &size)
    {
        if (!IsOpen())
        {
            last_error_ = {FileError::kOpenError, DetailedError::kNoDetails, 0};
            return FileError::kOpenError;
        }

        struct stat st;
        if (fstat(fd_, &st) != 0)
        {
            SET_LAST_ERR_FROM_ERRNO(FileError::kSizeError);
            return FileError::kSizeError;
        }

        size = static_cast<std::uint64_t>(st.st_size);
        CLEAR_LAST_ERR();
        return FileError::kSuccess;
    }

    FileError LinuxFileOperations::Close()
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

    bool LinuxFileOperations::IsOpen() const
    {
        return fd_ >= 0;
    }

    FileError LinuxFileOperations::OpenInternal(const char *path, int flags, mode_t mode)
    {
        // Close any existing file
        Close();

        fd_ = open(path, flags, mode);
        if (fd_ < 0)
        {
            SET_LAST_ERR_FROM_ERRNO(FileError::kOpenError);
            return FileError::kOpenError;
        }

        current_path_ = path;
        CLEAR_LAST_ERR();
        return FileError::kSuccess;
    }

    // Streaming I/O operations
    FileError LinuxFileOperations::WriteSequential(const std::uint8_t *data, std::size_t size)
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

    FileError LinuxFileOperations::ReadSequential(std::uint8_t *data, std::size_t size, std::size_t &bytes_read)
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

    FileError LinuxFileOperations::Seek(std::uint64_t position)
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

    std::uint64_t LinuxFileOperations::Tell() const
    {
        if (!IsOpen())
        {
            return 0;
        }

        off_t pos = lseek(fd_, 0, SEEK_CUR);
        return (pos == -1) ? 0 : static_cast<std::uint64_t>(pos);
    }

    FileError LinuxFileOperations::ForceSync()
    {
        if (!IsOpen())
        {
            last_error_ = {FileError::kOpenError, DetailedError::kNoDetails, 0};
            return FileError::kOpenError;
        }

        // Force filesystem sync using fsync - same logic as LinuxFile::forceSync()
        if (::fsync(fd_) != 0)
        {
            SET_LAST_ERR_FROM_ERRNO(FileError::kSyncError);
            return FileError::kSyncError;
        }

        CLEAR_LAST_ERR();
        return FileError::kSuccess;
    }

    FileError LinuxFileOperations::Flush()
    {
        if (!IsOpen())
        {
            last_error_ = {FileError::kOpenError, DetailedError::kNoDetails, 0};
            return FileError::kOpenError;
        }

        // On Linux, fsync handles both buffer flush and disk sync
        // For streaming operations, we can use fdatasync for better performance
        if (::fdatasync(fd_) != 0)
        {
            SET_LAST_ERR_FROM_ERRNO(FileError::kFlushError);
            return FileError::kFlushError;
        }

        CLEAR_LAST_ERR();
        return FileError::kSuccess;
    }

    int LinuxFileOperations::GetHandle() const
    {
        return fd_;
    }

    // Platform-specific factory function implementation
    std::unique_ptr<FileOperations> CreatePlatformFileOperations()
    {
        return std::make_unique<LinuxFileOperations>();
    }

} // namespace rpi_imager

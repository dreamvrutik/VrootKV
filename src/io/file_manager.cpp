/**
 * @file file_manager.cpp
 * @author Vrutik Halani
 * @brief Implements the IFileManager interface for file I/O operations.
 *
 * This file contains the implementation of the IFileManager interface, which
 * provides a platform-independent way to perform file I/O operations. The
 * implementation uses platform-specific APIs for file I/O, and C++17
 * <filesystem> for path operations.
 */

#include "VrootKV/io/file_manager.h"

#include <filesystem>
#include <vector>

// Platform-specific includes for low-level file I/O.
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace VrootKV::io {
namespace { // Use an anonymous namespace to hide implementation details.

#ifdef _WIN32

// Helper to convert UTF-8 std::string to UTF-16 std::wstring for Windows API.
std::wstring StringToWString(const std::string& s) {
    if (s.empty()) {
        return L"";
    }
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &s, (int)s.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &s, (int)s.size(), &wstrTo, size_needed);
    return wstrTo;
}

// Concrete implementation of IWritableFile for Windows.
class WindowsWritableFile final : public IWritableFile {
public:
    explicit WindowsWritableFile(HANDLE handle) : handle_(handle) {}

    ~WindowsWritableFile() override {
        if (handle_!= INVALID_HANDLE_VALUE) {
            Close();
        }
    }

    bool Write(std::string_view data) override {
        if (handle_ == INVALID_HANDLE_VALUE) return false;
        DWORD bytes_written;
        return WriteFile(handle_, data.data(), static_cast<DWORD>(data.size()), &bytes_written, nullptr) &&
                bytes_written == data.size();
    }

    bool Flush() override {
        // On Windows, WriteFile goes to the OS buffer. FlushFileBuffers is the
        // equivalent of fsync, so Flush() is a no-op here.
        return handle_!= INVALID_HANDLE_VALUE;
    }

    bool Sync() override {
        if (handle_ == INVALID_HANDLE_VALUE) return false;
        return FlushFileBuffers(handle_);
    }

    bool Close() override {
        if (handle_ == INVALID_HANDLE_VALUE) return true;
        bool success = CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
        return success;
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

// Concrete implementation of IReadableFile for Windows.
class WindowsReadableFile final : public IReadableFile {
public:
    explicit WindowsReadableFile(HANDLE handle) : handle_(handle) {}

    ~WindowsReadableFile() override {
        if (handle_!= INVALID_HANDLE_VALUE) {
            Close();
        }
    }

    size_t Read(size_t n, std::string* result) override {
        if (handle_ == INVALID_HANDLE_VALUE ||!result) return 0;
        
        std::vector<char> buffer(n);
        DWORD bytes_read = 0;
        if (!ReadFile(handle_, buffer.data(), static_cast<DWORD>(n), &bytes_read, nullptr)) {
            return 0;
        }
        
        result->assign(buffer.data(), bytes_read);
        return bytes_read;
    }

    bool Close() override {
        if (handle_ == INVALID_HANDLE_VALUE) return true;
        bool success = CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
        return success;
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

#else // POSIX implementation

// Concrete implementation of IWritableFile for POSIX-like systems.
class PosixWritableFile final : public IWritableFile {
public:
    explicit PosixWritableFile(int fd) : fd_(fd) {}

    ~PosixWritableFile() override {
        if (fd_!= -1) {
            Close();
        }
    }

    bool Write(std::string_view data) override {
        if (fd_ == -1) return false;
        ssize_t bytes_written = write(fd_, data.data(), data.size());
        return bytes_written == static_cast<ssize_t>(data.size());
    }

    bool Flush() override {
        // write() already flushes to the OS buffer cache. The real durability
        // is handled by Sync(), so this can be a no-op.
        return fd_!= -1;
    }

    bool Sync() override {
        if (fd_ == -1) return false;
        // fsync is the call that guarantees durability to the physical device.
        return fsync(fd_) == 0;
    }

    bool Close() override {
        if (fd_ == -1) return true;
        int result = close(fd_);
        fd_ = -1;
        return result == 0;
    }

private:
    int fd_;
};

// Concrete implementation of IReadableFile for POSIX-like systems.
class PosixReadableFile final : public IReadableFile {
public:
    explicit PosixReadableFile(int fd) : fd_(fd) {}

    ~PosixReadableFile() override {
        if (fd_!= -1) {
            Close();
        }
    }

    size_t Read(size_t n, std::string* result) override {
        if (fd_ == -1 ||!result) return 0;
        
        std::vector<char> buffer(n);
        ssize_t bytes_read = read(fd_, buffer.data(), n);
        if (bytes_read < 0) {
            return 0; // Error
        }
        
        result->assign(buffer.data(), bytes_read);
        return bytes_read;
    }

    bool Close() override {
        if (fd_ == -1) return true;
        int result = close(fd_);
        fd_ = -1;
        return result == 0;
    }

private:
    int fd_;
};

#endif

// Concrete implementation of IFileManager using C++17 <filesystem> for
// path operations and platform-specific APIs for file I/O.
class DefaultFileManager final : public IFileManager {
public:
    bool NewWritableFile(const std::string& fname, std::unique_ptr<IWritableFile>& result) override {
#ifdef _WIN32
        std::wstring wfname = StringToWString(fname);
        HANDLE handle = CreateFileW(wfname.c_str(),
                                    GENERIC_WRITE,
                                    0, // No sharing
                                    nullptr,
                                    CREATE_ALWAYS, // Truncates if exists
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            return false;
        }
        result = std::make_unique<WindowsWritableFile>(handle);
#else
        int fd = open(fname.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            return false;
        }
        result = std::make_unique<PosixWritableFile>(fd);
#endif
        return true;
    }

    bool NewReadableFile(const std::string& fname, std::unique_ptr<IReadableFile>& result) override {
        if (!FileExists(fname)) {
            return false;
        }
#ifdef _WIN32
        std::wstring wfname = StringToWString(fname);
        HANDLE handle = CreateFileW(wfname.c_str(),
                                    GENERIC_READ,
                                    FILE_SHARE_READ,
                                    nullptr,
                                    OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            return false;
        }
        result = std::make_unique<WindowsReadableFile>(handle);
#else
        int fd = open(fname.c_str(), O_RDONLY);
        if (fd < 0) {
            return false;
        }
        result = std::make_unique<PosixReadableFile>(fd);
#endif
        return true;
    }

    bool FileExists(const std::string& fname) const override {
        return std::filesystem::exists(fname);
    }

    bool DeleteFile(const std::string& fname) override {
        std::error_code ec;
        std::filesystem::remove(fname, ec);
        return!ec;
    }

    bool RenameFile(const std::string& src, const std::string& target) override {
        std::error_code ec;
        std::filesystem::rename(src, target, ec);
        return!ec;
    }
};

} // anonymous namespace

// Implementation of the public factory function.
std::unique_ptr<IFileManager> NewDefaultFileManager() {
    return std::make_unique<DefaultFileManager>();
}

} // namespace VrootKV::io

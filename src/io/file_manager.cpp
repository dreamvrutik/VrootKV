/**
 * @file file_manager.cpp
 * @author Vrutik Halani
 * @brief Cross-platform implementation of the storage engine's file I/O interfaces.
 *
 * Overview
 * --------
 * This file provides the default implementation of the `IFileManager` factory and
 * the platform-specific readable/writable file primitives used by the storage engine.
 * It aims to be:
 *   - **Portable:** Uses Win32 APIs on Windows and POSIX syscalls on Unix-like systems.
 *   - **Simple & Safe:** Minimal buffering; durable writes via `Sync()`.
 *   - **Correct:** Handles partial writes and EINTR where applicable.
 *
 * Responsibilities
 * ----------------
 * - `IWritableFile`:
 *      * `Write()`  — append bytes to the file (handles partial writes/short writes).
 *      * `Flush()`  — flush user-space buffers (no-op here since we don't buffer).
 *      * `Sync()`   — request durable persistence (`FlushFileBuffers` / `fsync`).
 *      * `Close()`  — close the handle/file descriptor.
 *
 * - `IReadableFile`:
 *      * `Read(n, out)` — read up to `n` bytes, returning the number of bytes read.
 *      * `Close()`      — close the underlying handle/file descriptor.
 *
 * - `IFileManager`:
 *      * `NewWritableFile(name, out)` — create/truncate a writable file.
 *      * `NewReadableFile(name, out)` — open a file for reading.
 *      * `FileExists(name)`           — path existence check.
 *      * `DeleteFile(name)`           — unlink/remove a file.
 *      * `RenameFile(src, dst)`       — atomic rename where supported.
 *
 * Notes
 * -----
 * - `Flush()` is intentionally a no-op since we don't keep user-space buffers; all
 *   writes go straight to the OS. Use `Sync()` to ensure data reaches the device.
 * - This implementation is not thread-safe by itself; synchronize access externally
 *   if multiple threads share the same file object.
 */

#include "VrootKV/io/file_manager.h"

#include <filesystem>
#include <vector>
#include <string>
#include <memory>

// Platform-specific includes for low-level file I/O.
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace VrootKV::io {
namespace { // Anonymous namespace for internal helpers and concrete classes.

#ifdef _WIN32
// ============================================================================
// Windows (Win32) implementation
// ============================================================================

/**
 * @brief Convert a UTF-8 encoded std::string to a UTF-16 std::wstring for Win32 APIs.
 * @param s UTF-8 input string
 * @return UTF-16 wide string
 *
 * The function performs a two-pass conversion using `MultiByteToWideChar`.
 */
std::wstring StringToWString(const std::string& s) {
    if (s.empty()) {
        return L"";
    }

    // Determine required wide-character count (no terminator included).
    int wide_len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (wide_len <= 0) {
        return L"";
    }

    std::wstring wstr(static_cast<size_t>(wide_len), L'\0');
    // Convert into the allocated buffer.
    int written = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                                      &wstr[0], wide_len);
    if (written != wide_len) {
        return L"";
    }
    return wstr;
}

/**
 * @brief Win32 `IWritableFile` backed by a HANDLE.
 *
 * Semantics:
 *  - `Write()` loops to handle short writes (should be rare on Win32, but correct).
 *  - `Flush()` is a no-op (no user buffers). Use `Sync()` (FlushFileBuffers) for durability.
 */
class WindowsWritableFile final : public IWritableFile {
public:
    explicit WindowsWritableFile(HANDLE handle) : handle_(handle) {}

    ~WindowsWritableFile() override {
        if (handle_ != INVALID_HANDLE_VALUE) {
            Close();
        }
    }

    /**
     * @brief Append `data` to the file, retrying on short writes.
     * @return true on success; false on failure.
     */
    bool Write(std::string_view data) override {
        if (handle_ == INVALID_HANDLE_VALUE) return false;

        const char* p = data.data();
        size_t remaining = data.size();

        while (remaining > 0) {
            DWORD to_write = static_cast<DWORD>(remaining > static_cast<size_t>(std::numeric_limits<DWORD>::max())
                                                ? std::numeric_limits<DWORD>::max()
                                                : remaining);
            DWORD written = 0;
            if (!WriteFile(handle_, p, to_write, &written, nullptr)) {
                return false;
            }
            if (written == 0) {
                // Should not happen; avoid potential infinite loop.
                return false;
            }
            p += written;
            remaining -= written;
        }
        return true;
    }

    /**
     * @brief Flush user-space buffers (no-op: writes go directly to OS).
     */
    bool Flush() override {
        return handle_ != INVALID_HANDLE_VALUE;
    }

    /**
     * @brief Ensure OS buffers are flushed to the underlying storage device.
     */
    bool Sync() override {
        if (handle_ == INVALID_HANDLE_VALUE) return false;
        return FlushFileBuffers(handle_) != 0;
    }

    /**
     * @brief Close the file handle.
     */
    bool Close() override {
        if (handle_ == INVALID_HANDLE_VALUE) return true;
        BOOL ok = CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
        return ok != 0;
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

/**
 * @brief Win32 `IReadableFile` backed by a HANDLE.
 *
 * Semantics:
 *  - `Read(n, out)` reads up to `n` bytes into `*out` (single read call).
 */
class WindowsReadableFile final : public IReadableFile {
public:
    explicit WindowsReadableFile(HANDLE handle) : handle_(handle) {}

    ~WindowsReadableFile() override {
        if (handle_ != INVALID_HANDLE_VALUE) {
            Close();
        }
    }

    /**
     * @brief Read up to `n` bytes. On success, returns number of bytes read (0 on EOF).
     */
    size_t Read(size_t n, std::string* result) override {
        if (handle_ == INVALID_HANDLE_VALUE || !result) return 0;

        std::vector<char> buf(n);
        DWORD read = 0;
        if (!ReadFile(handle_, buf.data(), static_cast<DWORD>(n), &read, nullptr)) {
            return 0;
        }
        result->assign(buf.data(), buf.data() + read);
        return static_cast<size_t>(read);
    }

    /**
     * @brief Close the file handle.
     */
    bool Close() override {
        if (handle_ == INVALID_HANDLE_VALUE) return true;
        BOOL ok = CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
        return ok != 0;
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

#else
// ============================================================================
// POSIX implementation (Linux, macOS, etc.)
// ============================================================================

/**
 * @brief POSIX `IWritableFile` backed by an fd.
 *
 * Semantics:
 *  - `Write()` loops to handle partial writes and retries on EINTR.
 *  - `Sync()` uses `fsync` to request durable persistence.
 */
class PosixWritableFile final : public IWritableFile {
public:
    explicit PosixWritableFile(int fd) : fd_(fd) {}

    ~PosixWritableFile() override {
        if (fd_ != -1) {
            Close();
        }
    }

    /**
     * @brief Append `data` to the file. Handles short writes and EINTR.
     * @return true on success; false on failure.
     */
    bool Write(std::string_view data) override {
        if (fd_ == -1) return false;

        const char* p = data.data();
        size_t remaining = data.size();

        while (remaining > 0) {
            ssize_t n = ::write(fd_, p, remaining);
            if (n < 0) {
                if (errno == EINTR) continue; // Retry write
                return false;
            }
            if (n == 0) {
                // Should not happen; avoid infinite loop.
                return false;
            }
            p += static_cast<size_t>(n);
            remaining -= static_cast<size_t>(n);
        }
        return true;
    }

    /**
     * @brief Flush user-space buffers (no-op: we don't buffer in user space).
     */
    bool Flush() override {
        return fd_ != -1;
    }

    /**
     * @brief Ensure data is committed to the device (fsync).
     */
    bool Sync() override {
        if (fd_ == -1) return false;
        int rc;
        do {
            rc = ::fsync(fd_);
        } while (rc < 0 && errno == EINTR);
        return rc == 0;
    }

    /**
     * @brief Close the file descriptor.
     */
    bool Close() override {
        if (fd_ == -1) return true;
        int rc;
        do {
            rc = ::close(fd_);
        } while (rc < 0 && errno == EINTR);
        fd_ = -1;
        return rc == 0;
    }

private:
    int fd_;
};

/**
 * @brief POSIX `IReadableFile` backed by an fd.
 *
 * Semantics:
 *  - `Read(n, out)` reads up to `n` bytes into `*out` (single read call).
 */
class PosixReadableFile final : public IReadableFile {
public:
    explicit PosixReadableFile(int fd) : fd_(fd) {}

    ~PosixReadableFile() override {
        if (fd_ != -1) {
            Close();
        }
    }

    /**
     * @brief Read up to `n` bytes. Returns count read (0 on EOF), retries on EINTR.
     */
    size_t Read(size_t n, std::string* result) override {
        if (fd_ == -1 || !result) return 0;

        std::vector<char> buf(n);
        ssize_t r;
        do {
            r = ::read(fd_, buf.data(), n);
        } while (r < 0 && errno == EINTR);

        if (r <= 0) {
            return 0; // 0 = EOF, <0 = error
        }
        result->assign(buf.data(), buf.data() + static_cast<size_t>(r));
        return static_cast<size_t>(r);
    }

    /**
     * @brief Close the file descriptor.
     */
    bool Close() override {
        if (fd_ == -1) return true;
        int rc;
        do {
            rc = ::close(fd_);
        } while (rc < 0 && errno == EINTR);
        fd_ = -1;
        return rc == 0;
    }

private:
    int fd_;
};
#endif // _WIN32

/**
 * @brief Default `IFileManager` using C++17 `<filesystem>` and platform I/O.
 *
 * Path utilities (exists/rename/remove) use `std::filesystem`. The open/close
 * operations use platform-specific primitives (Win32 HANDLEs vs POSIX fds).
 */
class DefaultFileManager final : public IFileManager {
public:
    /**
     * @brief Create/truncate a writable file and return an `IWritableFile`.
     * @param fname Path to open (created if missing, truncated if exists).
     * @param result On success, receives the new writable file.
     * @return true on success; false on failure.
     */
    bool NewWritableFile(const std::string& fname, std::unique_ptr<IWritableFile>& result) override {
#ifdef _WIN32
        std::wstring wfname = StringToWString(fname);
        HANDLE h = CreateFileW(
            wfname.c_str(),
            GENERIC_WRITE,
            0,                  // No sharing
            nullptr,
            CREATE_ALWAYS,      // Create or truncate
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        if (h == INVALID_HANDLE_VALUE) {
            return false;
        }
        result = std::make_unique<WindowsWritableFile>(h);
#else
        // 0644 = rw-r--r--
        int fd = ::open(fname.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            return false;
        }
        result = std::make_unique<PosixWritableFile>(fd);
#endif
        return true;
    }

    /**
     * @brief Open an existing file for reading and return an `IReadableFile`.
     * @param fname Path to open (must exist).
     * @param result On success, receives the new readable file.
     * @return true on success; false if the file does not exist or open fails.
     */
    bool NewReadableFile(const std::string& fname, std::unique_ptr<IReadableFile>& result) override {
        if (!FileExists(fname)) {
            return false;
        }
#ifdef _WIN32
        std::wstring wfname = StringToWString(fname);
        HANDLE h = CreateFileW(
            wfname.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,    // Allow concurrent readers
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        if (h == INVALID_HANDLE_VALUE) {
            return false;
        }
        result = std::make_unique<WindowsReadableFile>(h);
#else
        int fd = ::open(fname.c_str(), O_RDONLY);
        if (fd < 0) {
            return false;
        }
        result = std::make_unique<PosixReadableFile>(fd);
#endif
        return true;
    }

    /**
     * @brief Test whether a file or directory exists at `fname`.
     */
    bool FileExists(const std::string& fname) const override {
        std::error_code ec;
        const bool ok = std::filesystem::exists(fname, ec);
        return ok && !ec;
    }

    /**
     * @brief Delete a file (best effort). Returns true if removal succeeded.
     */
    bool DeleteFile(const std::string& fname) override {
        std::error_code ec;
        std::filesystem::remove(fname, ec);
        return !ec;
    }

    /**
     * @brief Rename/move a file to `target`. Overwrite semantics are platform-dependent.
     */
    bool RenameFile(const std::string& src, const std::string& target) override {
        std::error_code ec;
        std::filesystem::rename(src, target, ec);
        return !ec;
    }
};

} // anonymous namespace

// ----------------------------------------------------------------------------
// Public factory
// ----------------------------------------------------------------------------

/**
 * @brief Create the default `IFileManager` implementation for the current platform.
 */
std::unique_ptr<IFileManager> NewDefaultFileManager() {
    return std::make_unique<DefaultFileManager>();
}

} // namespace VrootKV::io

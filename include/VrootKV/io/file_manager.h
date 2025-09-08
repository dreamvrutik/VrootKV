/**
 * @file file_manager.h
 * @author Vrutik Halani
 * @brief Defines the interfaces for file I/O operations.
 *
 * This file contains the definitions for the interfaces that are used for file
 * I/O operations. This includes interfaces for writable files, readable files,
 * and a file manager that can be used to create, delete, and manipulate files.
 */

#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace VrootKV::io {

/**
 * @brief An interface for a file that can be written to sequentially.
 *
 * Implementations of this class are responsible for writing data to a file
 * and ensuring its durability on the storage medium.
 */
class IWritableFile {
public:
    virtual ~IWritableFile() = default;

    /**
     * @brief Appends data to the end of the file.
     * @param data The data to append.
     * @return True on success, false on failure.
     */
    virtual bool Write(std::string_view data) = 0;

    /**
     * @brief Flushes the file's buffered data to the operating system.
     * @return True on success, false on failure.
     */
    virtual bool Flush() = 0;

    /**
     * @brief Ensures that all data written to the file is physically persisted
     * to the storage device. This is a stronger guarantee than Flush().
     * @return True on success, false on failure.
     */
    virtual bool Sync() = 0;

    /**
     * @brief Closes the file, releasing any associated resources.
     * @return True on success, false on failure.
     */
    virtual bool Close() = 0;
};

/**
 * @brief An interface for a file that can be read from sequentially.
 */
class IReadableFile {
public:
    virtual ~IReadableFile() = default;

    /**
     * @brief Reads up to 'n' bytes from the file.
     * @param n The maximum number of bytes to read.
     * @param result A pointer to a string where the read data will be stored.
     * @return The number of bytes read. Returns 0 on EOF or error.
     */
    virtual size_t Read(size_t n, std::string* result) = 0;

    /**
     * @brief Closes the file, releasing any associated resources.
     * @return True on success, false on failure.
     */
    virtual bool Close() = 0;
};

/**
 * @brief An interface for managing file system operations.
 *
 * This class provides an abstraction over the file system, allowing for
 * creation, deletion, and manipulation of files in a platform-independent way.
 */
class IFileManager {
public:
    virtual ~IFileManager() = default;

    /**
     * @brief Creates a new writable file. If the file already exists, its
     * contents are truncated.
     * @param fname The name of the file to create.
     * @param result A unique_ptr to hold the created writable file object.
     * @return True on success, false on failure.
     */
    virtual bool NewWritableFile(const std::string& fname, std::unique_ptr<IWritableFile>& result) = 0;

    /**
     * @brief Opens an existing file for reading.
     * @param fname The name of the file to open.
     * @param result A unique_ptr to hold the created readable file object.
     * @return True on success, false on failure.
     */
    virtual bool NewReadableFile(const std::string& fname, std::unique_ptr<IReadableFile>& result) = 0;

    /**
     * @brief Checks if a file with the given name exists.
     * @param fname The name of the file to check.
     * @return True if the file exists, false otherwise.
     */
    virtual bool FileExists(const std::string& fname) const = 0;

    /**
     * @brief Deletes a file.
     * @param fname The name of the file to delete.
     * @return True on success, false on failure.
     */
    virtual bool DeleteFile(const std::string& fname) = 0;

    /**
     * @brief Renames a file from a source path to a target path.
     * @param src The source file path.
     * @param target The target file path.
     * @return True on success, false on failure.
     */
    virtual bool RenameFile(const std::string& src, const std::string& target) = 0;
};

/**
 * @brief Factory function to create the default file manager for the current platform.
 * @return A unique_ptr to an IFileManager instance.
 */
std::unique_ptr<IFileManager> NewDefaultFileManager();

} // namespace VrootKV::io

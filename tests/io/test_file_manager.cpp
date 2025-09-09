/**
 * @file test_file_manager.cpp
 * @author Vrutik Halani
 * @brief Unit tests for the cross-platform `IFileManager` implementation.
 *
 * What is covered
 * ---------------
 * These tests validate the core responsibilities of the default file manager:
 *   • Path utilities: existence checks, deletion, and renaming.
 *   • Writable files: open → write (including multiple writes) → sync → close.
 *   • Readable files: open → read (all-at-once and chunked) → close.
 *   • Error paths: operating on closed handles and non-existent files.
 *
 * Test layout
 * -----------
 * Each test runs inside a dedicated temporary directory that is created during
 * SetUp() and destroyed in TearDown(). This keeps the filesystem clean and
 * prevents interference between tests.
 */

#include "VrootKV/io/file_manager.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace VrootKV::io {

/**
 * @class FileManagerTest
 * @brief
 *     Test fixture that provisions a temporary directory and a fresh
 *     `IFileManager` instance for each test case.
 *
 * Lifecycle
 * ---------
 * - SetUp(): create a temp directory and construct the default file manager.
 * - TearDown(): recursively remove the temp directory and all children.
 */
class FileManagerTest : public ::testing::Test {
protected:
    /**
     * @brief Create an isolated workspace and the default file manager instance.
     */
    void SetUp() override {
        // Create a (re)usable temporary directory for the test run.
        // Note: Using a fixed leaf keeps the path short and readable; the parent
        // temp directory is OS-provided (e.g., /tmp on POSIX).
        const auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        test_dir_ = std::filesystem::temp_directory_path() / test_info->name();
        std::filesystem::create_directory(test_dir_);

        // Obtain a platform-appropriate file manager via the factory.
        file_manager_ = NewDefaultFileManager();
    }

    /**
     * @brief Clean up the workspace after each test.
     */
    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    /**
     * @brief Helper to construct an absolute path under the test directory.
     * @param filename A relative file name (no directory traversal).
     * @return Normalized absolute path as a std::string.
     */
    std::string TestPath(const std::string& filename) const {
        return (test_dir_ / filename).string();
    }

    std::filesystem::path test_dir_;               ///< Root of the test workspace.
    std::unique_ptr<IFileManager> file_manager_;   ///< System-under-test.
};

/**
 * @test Verify that `FileExists` reflects presence/absence of files.
 * Steps:
 *   1) Confirm non-existence.
 *   2) Create a file via std::ofstream (out-of-band).
 *   3) Confirm existence via the manager.
 */
TEST_F(FileManagerTest, FileExists) {
    const std::string filename = TestPath("test_exists.txt");
    EXPECT_FALSE(file_manager_->FileExists(filename));

    // Create a file manually to check existence.
    std::ofstream(filename) << "hello";
    EXPECT_TRUE(file_manager_->FileExists(filename));
}

/**
 * @test Deleting a present file should succeed and make it disappear.
 */
TEST_F(FileManagerTest, DeleteFile) {
    const std::string filename = TestPath("test_delete.txt");
    std::ofstream(filename) << "delete me";
    ASSERT_TRUE(file_manager_->FileExists(filename));

    EXPECT_TRUE(file_manager_->DeleteFile(filename));
    EXPECT_FALSE(file_manager_->FileExists(filename));
}

/**
 * @test Renaming a file should move it atomically (where supported) and preserve content.
 */
TEST_F(FileManagerTest, RenameFile) {
    const std::string src_name = TestPath("source.txt");
    const std::string target_name = TestPath("target.txt");
    std::ofstream(src_name) << "content";
    ASSERT_TRUE(file_manager_->FileExists(src_name));
    ASSERT_FALSE(file_manager_->FileExists(target_name));

    EXPECT_TRUE(file_manager_->RenameFile(src_name, target_name));

    EXPECT_FALSE(file_manager_->FileExists(src_name));
    EXPECT_TRUE(file_manager_->FileExists(target_name));

    // Verify content was preserved.
    std::ifstream ifs(target_name);
    std::string content;
    ifs >> content;
    EXPECT_EQ(content, "content");
}

/**
 * @test Write/Sync/Close on a writable file should persist bytes to disk.
 * This also validates that multiple sequential `Write` calls append properly.
 */
TEST_F(FileManagerTest, WriteAndSyncFile) {
    const std::string filename = TestPath("test_write.txt");
    std::unique_ptr<IWritableFile> writable_file;

    // Create/truncate the file for writing.
    ASSERT_TRUE(file_manager_->NewWritableFile(filename, writable_file));
    ASSERT_NE(writable_file, nullptr);

    const std::string data1 = "Hello, ";
    const std::string data2 = "World!";

    // Multiple writes are appended back-to-back.
    EXPECT_TRUE(writable_file->Write(data1));
    EXPECT_TRUE(writable_file->Write(data2));

    // Sync ensures durability beyond the OS page cache (fsync/FlushFileBuffers).
    EXPECT_TRUE(writable_file->Sync());
    EXPECT_TRUE(writable_file->Close());

    // Verify the file content matches concatenated writes.
    std::ifstream ifs(filename);
    std::stringstream buffer;
    buffer << ifs.rdbuf();
    EXPECT_EQ(buffer.str(), data1 + data2);
}

/**
 * @test Reading a file fully should return its full length and exact content.
 */
TEST_F(FileManagerTest, ReadFile) {
    const std::string filename = TestPath("test_read.txt");
    const std::string content = "This is the content to be read.";
    std::ofstream(filename) << content;

    std::unique_ptr<IReadableFile> readable_file;
    ASSERT_TRUE(file_manager_->NewReadableFile(filename, readable_file));
    ASSERT_NE(readable_file, nullptr);

    std::string result;
    // Read up to 1024 bytes; should read exactly `content.size()` here.
    size_t bytes_read = readable_file->Read(1024, &result);

    EXPECT_EQ(bytes_read, content.length());
    EXPECT_EQ(result, content);

    EXPECT_TRUE(readable_file->Close());
}

/**
 * @test Chunked reads should progress through the file and hit EOF gracefully.
 * Sequence:
 *   - Read 4 bytes ("1234"), then next 4 ("5678"), then the tail ("90").
 *   - A subsequent read returns 0 to signal EOF.
 */
TEST_F(FileManagerTest, ReadFileInChunks) {
    const std::string filename = TestPath("test_read_chunks.txt");
    const std::string content = "1234567890";
    std::ofstream(filename) << content;

    std::unique_ptr<IReadableFile> readable_file;
    ASSERT_TRUE(file_manager_->NewReadableFile(filename, readable_file));

    std::string chunk1, chunk2, chunk3;
    EXPECT_EQ(readable_file->Read(4, &chunk1), 4);
    EXPECT_EQ(chunk1, "1234");

    EXPECT_EQ(readable_file->Read(4, &chunk2), 4);
    EXPECT_EQ(chunk2, "5678");

    EXPECT_EQ(readable_file->Read(4, &chunk3), 2);
    EXPECT_EQ(chunk3, "90");

    // Subsequent read should return 0 (EOF).
    EXPECT_EQ(readable_file->Read(4, &chunk3), 0);
}

/**
 * @test Opening a non-existent file for reading should fail gracefully.
 */
TEST_F(FileManagerTest, NewReadableFile_NonExistent) {
    const std::string filename = TestPath("non_existent.txt");
    std::unique_ptr<IReadableFile> readable_file;
    EXPECT_FALSE(file_manager_->NewReadableFile(filename, readable_file));
}

/**
 * @test Deleting a non-existent file should be treated as success (idempotent).
 * Rationale: "Ensure absent" semantics are often useful.
 */
TEST_F(FileManagerTest, DeleteFile_NonExistent) {
    const std::string filename = TestPath("non_existent.txt");
    EXPECT_TRUE(file_manager_->DeleteFile(filename));
}

/**
 * @test Renaming a non-existent source should fail and leave the target absent.
 */
TEST_F(FileManagerTest, RenameFile_NonExistent) {
    const std::string src_name = TestPath("non_existent.txt");
    const std::string target_name = TestPath("target.txt");
    EXPECT_FALSE(file_manager_->RenameFile(src_name, target_name));
}

/**
 * @test After closing a writable file, subsequent writes must fail.
 */
TEST_F(FileManagerTest, Write_AfterClose) {
    const std::string filename = TestPath("test_write_after_close.txt");
    std::unique_ptr<IWritableFile> writable_file;
    ASSERT_TRUE(file_manager_->NewWritableFile(filename, writable_file));
    ASSERT_TRUE(writable_file->Close());
    EXPECT_FALSE(writable_file->Write("test"));
}

/**
 * @test After closing a readable file, subsequent reads should return 0 (no data).
 */
TEST_F(FileManagerTest, Read_AfterClose) {
    const std::string filename = TestPath("test_read_after_close.txt");
    std::ofstream(filename) << "test";
    std::unique_ptr<IReadableFile> readable_file;
    ASSERT_TRUE(file_manager_->NewReadableFile(filename, readable_file));
    ASSERT_TRUE(readable_file->Close());
    std::string result;
    EXPECT_EQ(readable_file->Read(4, &result), 0);
}

} // namespace VrootKV::io
#include "VrootKV/io/file_manager.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace VrootKV::io {

// A test fixture for file manager tests. It handles the creation and
// cleanup of a temporary directory for each test case.
class FileManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a unique temporary directory for the test run.
        test_dir_ = std::filesystem::temp_directory_path() / "VrootKV_test";
        std::filesystem::create_directory(test_dir_);
        
        // Use the factory function to get an instance of the default file manager.
        // This decouples the test from the concrete implementation class.
        file_manager_ = NewDefaultFileManager();
    }

    void TearDown() override {
        // Clean up the temporary directory.
        std::filesystem::remove_all(test_dir_);
    }

    // Helper to get a full path within our temporary test directory.
    std::string TestPath(const std::string& filename) const {
        return (test_dir_ / filename).string();
    }

    std::filesystem::path test_dir_;
    std::unique_ptr<IFileManager> file_manager_;
};

TEST_F(FileManagerTest, FileExists) {
    const std::string filename = TestPath("test_exists.txt");
    EXPECT_FALSE(file_manager_->FileExists(filename));

    // Create a file manually to check existence.
    std::ofstream(filename) << "hello";
    EXPECT_TRUE(file_manager_->FileExists(filename));
}

TEST_F(FileManagerTest, DeleteFile) {
    const std::string filename = TestPath("test_delete.txt");
    std::ofstream(filename) << "delete me";
    ASSERT_TRUE(file_manager_->FileExists(filename));

    EXPECT_TRUE(file_manager_->DeleteFile(filename));
    EXPECT_FALSE(file_manager_->FileExists(filename));
}

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

TEST_F(FileManagerTest, WriteAndSyncFile) {
    const std::string filename = TestPath("test_write.txt");
    std::unique_ptr<IWritableFile> writable_file;
    
    ASSERT_TRUE(file_manager_->NewWritableFile(filename, writable_file));
    ASSERT_NE(writable_file, nullptr);

    const std::string data1 = "Hello, ";
    const std::string data2 = "World!";
    
    EXPECT_TRUE(writable_file->Write(data1));
    EXPECT_TRUE(writable_file->Write(data2));
    
    // Sync ensures data is durable.
    EXPECT_TRUE(writable_file->Sync());
    EXPECT_TRUE(writable_file->Close());

    // Verify the file content.
    std::ifstream ifs(filename);
    std::stringstream buffer;
    buffer << ifs.rdbuf();
    EXPECT_EQ(buffer.str(), data1 + data2);
}

TEST_F(FileManagerTest, ReadFile) {
    const std::string filename = TestPath("test_read.txt");
    const std::string content = "This is the content to be read.";
    std::ofstream(filename) << content;

    std::unique_ptr<IReadableFile> readable_file;
    ASSERT_TRUE(file_manager_->NewReadableFile(filename, readable_file));
    ASSERT_NE(readable_file, nullptr);

    std::string result;
    // Read up to 1024 bytes
    size_t bytes_read = readable_file->Read(1024, &result); 

    EXPECT_EQ(bytes_read, content.length());
    EXPECT_EQ(result, content);
    
    EXPECT_TRUE(readable_file->Close());
}

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

} // namespace VrootKV::io
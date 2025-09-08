/**
 * @file test_sstable_end_to_end.cpp
 * @author Vrutik Halani
 * @brief End-to-end tests for the SSTable implementation.
 *
 * This file contains end-to-end tests for the SSTable implementation. The
 * tests create an SSTable, write data to it, and then read the data back.
 */

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <map>
#include <filesystem>

#include "VrootKV/io/sstable_format.h"   // public
#include "src/io/sstable_blocks.h"       // internal (decls), implemented in .cpp
#include "VrootKV/io/file_manager.h"

using namespace VrootKV::io;

class SSTableEndToEndTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "VrootKV_test";
        std::filesystem::create_directory(test_dir_);
        file_manager_ = NewDefaultFileManager();
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    std::string TestPath(const std::string& filename) {
        return (test_dir_ / filename).string();
    }

    std::filesystem::path test_dir_;
    std::unique_ptr<IFileManager> file_manager_;
};

TEST_F(SSTableEndToEndTest, WriteAndRead) {
    // Create a new SSTable file
    const std::string filename = TestPath("test.sstable");
    std::unique_ptr<IWritableFile> file;
    ASSERT_TRUE(file_manager_->NewWritableFile(filename, file));

    // Create a data block builder
    DataBlockBuilder data_block_builder;
    data_block_builder.Add("key1", "value1");
    data_block_builder.Add("key2", "value2");
    std::string data_block = data_block_builder.Finish();

    // Create an index block builder
    IndexBlockBuilder index_block_builder;
    BlockHandle data_block_handle{0, data_block.size()};
    index_block_builder.Add("key1", data_block_handle);
    std::string index_block = index_block_builder.Finish();

    // Create a footer
    SSTableFooter footer;
    footer.index_handle = BlockHandle{data_block.size(), index_block.size()};

    // Write the blocks and footer to the file
    ASSERT_TRUE(file->Write(data_block));
    ASSERT_TRUE(file->Write(index_block));
    std::string footer_buf;
    footer.EncodeTo(footer_buf);
    ASSERT_TRUE(file->Write(footer_buf));
    ASSERT_TRUE(file->Close());

    // Open the SSTable for reading
    std::unique_ptr<IReadableFile> readable_file;
    ASSERT_TRUE(file_manager_->NewReadableFile(filename, readable_file));

    // Read the entire file
    std::string file_contents;
    size_t read_size;
    do {
        std::string buffer;
        read_size = readable_file->Read(1024, &buffer);
        file_contents.append(buffer);
    } while (read_size > 0);

    // Read the footer
    std::string_view file_contents_sv = file_contents;
    std::string_view footer_sv = file_contents_sv.substr(file_contents.size() - SSTableFooter::kEncodedLength);
    SSTableFooter footer_read = SSTableFooter::DecodeFrom(footer_sv);

    // Read the index block
    std::string_view index_block_sv = file_contents_sv.substr(footer_read.index_handle.offset, footer_read.index_handle.size);
    IndexBlockReader index_block_reader(index_block_sv);

    // Find the block handle for key1
    BlockHandle found_handle;
    ASSERT_TRUE(index_block_reader.Find("key1", found_handle));

    // Read the data block
    std::string_view data_block_sv = file_contents_sv.substr(found_handle.offset, found_handle.size);
    DataBlockReader data_block_reader(data_block_sv);

    // Read the value for key1
    std::string value;
    ASSERT_TRUE(data_block_reader.Get("key1", value));
    EXPECT_EQ(value, "value1");
}
/**
 * @file test_sstable_end_to_end.cpp
 * @author Vrutik Halani
 * @brief End-to-end tests for minimal SSTable write/read flow.
 *
 * What this verifies
 * ------------------
 * The test exercises the *happy path* of building an SSTable in-memory and
 * persisting it to disk, then reading it back to confirm structural and
 * functional correctness:
 *   1) Build a **Data Block** containing sorted key–value pairs.
 *   2) Build an **Index Block** mapping the divider key to the data block.
 *   3) Write [Data][Index][Footer] to a file via `IFileManager`.
 *   4) Re-open the file, parse the **Footer**, then the **Index**, route to the
 *      **Data Block**, and finally perform a point lookup (`Get("key1")`).
 *
 * File layout (as written by this test)
 * -------------------------------------
 *   [DataBlock bytes][IndexBlock bytes][Footer (40 bytes)]
 *
 * Notes
 * -----
 * - These tests rely on the internal encoders/decoders in `sstable_blocks.*` and
 *   the public footer format in `sstable_format.h`.
 * - Keys added to builders must be strictly increasing; the test mirrors that.
 */

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <map>
#include <filesystem>

#include "VrootKV/io/sstable_format.h"   // public footer/handle types
#include "src/io/sstable_blocks.h"       // internal builders/readers
#include "VrootKV/io/file_manager.h"     // file I/O abstraction

using namespace VrootKV::io;

/**
 * @class SSTableEndToEndTest
 * @brief
 *     Fixture that provides an isolated temporary directory and a default
 *     file manager instance for each test.
 *
 * Lifecycle
 * ---------
 * - SetUp(): create temp directory and instantiate file manager.
 * - TearDown(): clean up all files written under the temp directory.
 */
class SSTableEndToEndTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        test_dir_ = std::filesystem::temp_directory_path() / test_info->name();
        std::filesystem::create_directory(test_dir_);
        file_manager_ = NewDefaultFileManager();
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    /**
     * @brief Construct an absolute path within the test directory.
     */
    std::string TestPath(const std::string& filename) {
        return (test_dir_ / filename).string();
    }

    std::filesystem::path test_dir_;
    std::unique_ptr<IFileManager> file_manager_;
};

/**
 * @test Build a tiny SSTable, write it to disk, then read and query it back.
 *
 * Steps
 * -----
 * 1) Build a single data block with entries: ("key1","value1"), ("key2","value2").
 * 2) Build an index block with one divider "key1" -> handle of the data block.
 * 3) Synthesize a footer that points to the index block; write all to file.
 * 4) Read back: parse footer, parse index, route to data block, Get("key1").
 */
TEST_F(SSTableEndToEndTest, WriteAndRead) {
    // ---------- 1) Create an SSTable file ----------
    const std::string filename = TestPath("test.sstable");
    std::unique_ptr<IWritableFile> file;
    ASSERT_TRUE(file_manager_->NewWritableFile(filename, file));

    // ---------- 2) Build a data block ----------
    // Keys must be strictly increasing for the block format's prefix sharing.
    DataBlockBuilder data_block_builder;
    data_block_builder.Add("key1", "value1");
    data_block_builder.Add("key2", "value2");
    std::string data_block = data_block_builder.Finish();

    // ---------- 3) Build an index block ----------
    // Divider is typically the smallest key in the target data block.
    IndexBlockBuilder index_block_builder;

    // The data block will be written at file offset 0 with length = data_block.size().
    BlockHandle data_block_handle{0, static_cast<uint64_t>(data_block.size())};
    index_block_builder.Add("key1", data_block_handle);

    std::string index_block = index_block_builder.Finish();

    // ---------- 4) Build the footer ----------
    // Footer refers to the filter (unused here) and the index block.
    // We place the index immediately after the data block in the file.
    SSTableFooter footer;
    footer.index_handle = BlockHandle{
        static_cast<uint64_t>(data_block.size()),
        static_cast<uint64_t>(index_block.size())
    };

    // ---------- 5) Write to disk in the final SSTable order ----------
    ASSERT_TRUE(file->Write(data_block));     // [0 .. data.size)
    ASSERT_TRUE(file->Write(index_block));    // [data.size .. data.size+index.size)
    std::string footer_buf;
    footer.EncodeTo(footer_buf);              // fixed 40 bytes
    ASSERT_TRUE(file->Write(footer_buf));     // last 40 bytes in the file
    ASSERT_TRUE(file->Close());

    // ---------- 6) Read the file back ----------
    std::unique_ptr<IReadableFile> readable_file;
    ASSERT_TRUE(file_manager_->NewReadableFile(filename, readable_file));

    // Read the file in small chunks to emulate streaming access.
    std::string file_contents;
    size_t read_size;
    do {
        std::string buffer;
        read_size = readable_file->Read(1024, &buffer);
        file_contents.append(buffer);
    } while (read_size > 0);

    // ---------- 7) Parse the footer ----------
    // Footer sits at the very end of the file; parse without copying the rest.
    std::string_view file_contents_sv = file_contents;
    ASSERT_GE(file_contents_sv.size(), SSTableFooter::kEncodedLength);
    std::string_view footer_sv =
        file_contents_sv.substr(file_contents_sv.size() - SSTableFooter::kEncodedLength);

    SSTableFooter footer_read = SSTableFooter::DecodeFrom(footer_sv);
    // At this point, footer_sv has been consumed by DecodeFrom and is empty.

    // ---------- 8) Slice the index block and create a reader ----------
    std::string_view index_block_sv =
        file_contents_sv.substr(
            static_cast<size_t>(footer_read.index_handle.offset),
            static_cast<size_t>(footer_read.index_handle.size));

    IndexBlockReader index_block_reader(index_block_sv);

    // Route lookup for "key1" through the index; retrieve its data block handle.
    BlockHandle found_handle;
    ASSERT_TRUE(index_block_reader.Find("key1", found_handle));

    // ---------- 9) Slice the data block and create a reader ----------
    std::string_view data_block_sv =
        file_contents_sv.substr(
            static_cast<size_t>(found_handle.offset),
            static_cast<size_t>(found_handle.size));

    DataBlockReader data_block_reader(data_block_sv);

    // ---------- 10) Perform a point lookup ----------
    std::string value;
    ASSERT_TRUE(data_block_reader.Get("key1", value));
    EXPECT_EQ(value, "value1");
}
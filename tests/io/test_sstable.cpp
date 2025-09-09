/**
 * @file test_sstable.cpp
 * @author Vrutik Halani
 * @brief
 *     Unit tests for SSTable building/reading primitives:
 *       - Format layer: BlockHandle, SSTableFooter
 *       - Block layer:  DataBlockBuilder/Reader, IndexBlockBuilder/Reader
 *
 * What these tests cover
 * ----------------------
 * • **Format round-trips**: Verify fixed-size structs (handle/footer) survive
 *   encode/decode without loss and with correct layout assumptions.
 * • **Data block**: Restart-based prefix-compressed entries can be encoded and
 *   later read with exact-match lookups (including some negative cases).
 * • **Index block**: Binary search over divider keys returns the correct
 *   `BlockHandle` for the rightmost key ≤ search key (including before-first).
 * • **End-to-end**: Minimal multi-block layout that routes through the index to
 *   the appropriate data block and retrieves values.
 * • **Error paths**: Malformed/corrupt inputs raise exceptions.
 */

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <map>

#include "VrootKV/io/sstable_format.h"   // public (BlockHandle, SSTableFooter)
#include "src/io/sstable_blocks.h"       // internal (Data/Index builders + readers)

using namespace VrootKV::io;

/**
 * @test BlockHandle round-trip encoding/decoding.
 *
 * Steps:
 *  1) Encode a handle with known offset/size.
 *  2) Decode from a string_view and confirm values match.
 *  3) Verify the decode consumed exactly kEncodedLength bytes.
 */
TEST(SSTableFormat, BlockHandle_RoundTrip) {
    BlockHandle h{12345, 678};
    std::string buf;
    h.EncodeTo(buf);

    std::string_view sv = buf;
    BlockHandle got = BlockHandle::DecodeFrom(sv);

    EXPECT_EQ(got.offset, 12345u);
    EXPECT_EQ(got.size, 678u);
    EXPECT_TRUE(sv.empty());  // DecodeFrom consumes its bytes
}

/**
 * @test SSTableFooter round-trip encoding/decoding.
 *
 * Notes:
 *  - Footer layout is fixed-size (40 bytes): [filter(16)][index(16)][magic(8)].
 *  - DecodeFrom consumes exactly kEncodedLength bytes from the provided view.
 */
TEST(SSTableFormat, Footer_RoundTrip) {
    SSTableFooter f;
    f.filter_handle = BlockHandle{100, 20};
    f.index_handle  = BlockHandle{200, 30};
    f.magic         = 0xF00DBAADF00DBAADull;

    std::string buf;
    f.EncodeTo(buf);

    std::string_view sv = buf;
    SSTableFooter g = SSTableFooter::DecodeFrom(sv);

    EXPECT_EQ(g.filter_handle.offset, 100u);
    EXPECT_EQ(g.filter_handle.size,   20u);
    EXPECT_EQ(g.index_handle.offset,  200u);
    EXPECT_EQ(g.index_handle.size,    30u);
    EXPECT_EQ(g.magic, 0xF00DBAADF00DBAADull);

    // DecodeFrom consumes; `sv` should be empty if we started at the footer start.
    EXPECT_TRUE(sv.empty());
}

// ---------------------------------------------------------------------------
// Data Block: build -> read all keys (positive/negative lookups)
// ---------------------------------------------------------------------------

/**
 * @test DataBlockBuilder/DataBlockReader round-trip.
 *
 * Build a data block with small restart interval to stress prefix-sharing,
 * then verify all inserted keys return their values and some non-keys fail.
 */
TEST(SSTableBlocks, DataBlockBuilderReader_RoundTrip) {
    DataBlockBuilder b(/*restart_interval=*/2);

    std::vector<std::pair<std::string,std::string>> kv = {
        {"apple",  "A"}, {"apples", "AA"}, {"apply",  "AAA"},
        {"banana", "B"}, {"carrot", "C"},  {"carrots","CC"}
    };

    for (auto& p : kv) {
        b.Add(p.first, p.second);
    }

    std::string block = b.Finish();
    DataBlockReader r(block);

    // Positive lookups: all keys must be found with exact values.
    for (auto& p : kv) {
        std::string v;
        EXPECT_TRUE(r.Get(p.first, v));
        EXPECT_EQ(v, p.second);
    }

    // Negative lookups: non-existing keys must return false.
    std::string v;
    EXPECT_FALSE(r.Get("appl", v));        // prefix that is not a full key
    EXPECT_FALSE(r.Get("blueberry", v));   // between existing keys
    EXPECT_FALSE(r.Get("zzz", v));         // greater than last key
}

// ---------------------------------------------------------------------------
// Index Block: binary search over divider keys
// ---------------------------------------------------------------------------

/**
 * @test IndexBlockBuilder/Reader binary search behavior.
 *
 * The reader should return the handle for the rightmost divider key <= search key.
 * Also validates before-first behavior returns false.
 */
TEST(SSTableBlocks, IndexBlockBuilderReader_Find) {
    IndexBlockBuilder ib;

    BlockHandle h1{  0, 111};
    BlockHandle h2{200, 222};
    BlockHandle h3{500, 333};

    ib.Add("apple",  h1);
    ib.Add("banana", h2);
    ib.Add("carrot", h3);

    std::string idx = ib.Finish();
    IndexBlockReader ir(idx);

    BlockHandle h{};

    // Before-first: no divider key <= "aardvark"
    EXPECT_FALSE(ir.Find("aardvark", h));

    // Range routing: rightmost divider <= search key
    ASSERT_TRUE(ir.Find("apple", h));      EXPECT_EQ(h.offset, h1.offset);
    ASSERT_TRUE(ir.Find("apricot", h));    EXPECT_EQ(h.offset, h1.offset);
    ASSERT_TRUE(ir.Find("banana", h));     EXPECT_EQ(h.offset, h2.offset);
    ASSERT_TRUE(ir.Find("blueberry", h));  EXPECT_EQ(h.offset, h2.offset);
    ASSERT_TRUE(ir.Find("carrot", h));     EXPECT_EQ(h.offset, h3.offset);
    ASSERT_TRUE(ir.Find("zzz", h));        EXPECT_EQ(h.offset, h3.offset);
}

// ---------------------------------------------------------------------------
// End-to-end: 2 data blocks + index + footer
// ---------------------------------------------------------------------------

/**
 * @test End-to-end lookup through index into data blocks.
 *
 * Layout created in-memory:
 *   file = [d1][d2][idx][footer]
 *
 * - d1 contains keys in `kv1` and starts at offset 0.
 * - d2 follows d1.
 * - idx maps "ant" -> d1 and "banana" -> d2.
 * - footer points to `idx` at the end of `file`.
 *
 * Verifies:
 *  - Footer round-trip from the end of the file.
 *  - Index routing returns the correct handle for various search keys.
 *  - Data block lookups return expected values (and negatives fail).
 */
TEST(SSTableBlocks, EndToEnd_Lookup_Through_Index) {
    // Build two data blocks
    DataBlockBuilder b1(2), b2(2);
    std::vector<std::pair<std::string,std::string>> kv1 = {
        {"ant", "1"}, {"apple", "2"}, {"apples", "3"}
    };
    std::vector<std::pair<std::string,std::string>> kv2 = {
        {"banana", "4"}, {"carrot", "5"}, {"date", "6"}
    };

    for (auto& p : kv1) b1.Add(p.first, p.second);
    for (auto& p : kv2) b2.Add(p.first, p.second);

    std::string d1 = b1.Finish();
    std::string d2 = b2.Finish();

    // Build initial index with temporary offsets (fixed after layout).
    IndexBlockBuilder ib;
    BlockHandle h1{0, static_cast<uint64_t>(d1.size())};
    BlockHandle h2{0, static_cast<uint64_t>(d2.size())};
    ib.Add(kv1.front().first, h1);  // "ant" -> d1
    ib.Add(kv2.front().first, h2);  // "banana" -> d2
    std::string idx = ib.Finish();

    // Lay out: [d1][d2][idx][footer]
    std::string file;
    uint64_t off_d1 = 0;
    file.append(d1);

    uint64_t off_d2 = static_cast<uint64_t>(file.size());
    file.append(d2);

    uint64_t off_idx = static_cast<uint64_t>(file.size());
    file.append(idx);

    // Fix up real offsets in handles and rebuild index for final layout.
    h1.offset = off_d1;
    h2.offset = off_d2;

    IndexBlockBuilder ib2;
    ib2.Add(kv1.front().first, h1);
    ib2.Add(kv2.front().first, h2);
    idx = ib2.Finish();

    // Append final index and footer
    off_idx = static_cast<uint64_t>(file.size());
    file.append(idx);

    SSTableFooter footer;
    footer.filter_handle = BlockHandle{0, 0}; // filter not used in this test
    footer.index_handle  = BlockHandle{off_idx, static_cast<uint64_t>(idx.size())};
    footer.magic         = 0xF00DBAADF00DBAADull;

    std::string fbuf;
    footer.EncodeTo(fbuf);
    file.append(fbuf);

    // ---- Read path: decode footer -> load index -> find block -> read data block
    std::string_view sv(file);

    // Footer sits at file end and has fixed size.
    std::string_view footer_view = sv.substr(sv.size() - SSTableFooter::kEncodedLength);
    SSTableFooter got = SSTableFooter::DecodeFrom(footer_view);
    ASSERT_EQ(got.index_handle.offset, off_idx);
    ASSERT_EQ(got.index_handle.size, idx.size());

    // Slice the index block and construct a reader.
    std::string_view idx_view = sv.substr(got.index_handle.offset, got.index_handle.size);
    IndexBlockReader ir(idx_view);

    // Helper to fetch value for a key using index routing + data block lookup.
    auto fetch = [&](const std::string& key) -> std::string {
        BlockHandle h{};
        bool ok = ir.Find(key, h);
        if (!ok) return {};
        std::string_view block = sv.substr(h.offset, h.size);
        DataBlockReader dr(block);
        std::string v;
        if (dr.Get(key, v)) return v;
        return {};
    };

    // Positive lookups (exact matches)
    EXPECT_EQ(fetch("ant"), "1");
    EXPECT_EQ(fetch("apple"), "2");
    EXPECT_EQ(fetch("apples"), "3");
    EXPECT_EQ(fetch("banana"), "4");
    EXPECT_EQ(fetch("carrot"), "5");
    EXPECT_EQ(fetch("date"), "6");

    // Negative lookups travel through index to the right block but fail there.
    EXPECT_EQ(fetch("aaa"), "");        // before first
    EXPECT_EQ(fetch("blueberry"), "");  // between
    EXPECT_EQ(fetch("zzz"), "");        // after last
}

// ---------------------------------------------------------------------------
// Error handling & invariants
// ---------------------------------------------------------------------------

/**
 * @test DataBlockBuilder enforces strictly increasing keys.
 */
TEST(SSTableBlocks, DataBlockBuilder_Add_OutOfOrder) {
    DataBlockBuilder b;
    b.Add("a", "1");
    EXPECT_THROW(b.Add("a", "2"), std::runtime_error);
}

/**
 * @test DataBlockBuilder rejects Add() after Finish().
 */
TEST(SSTableBlocks, DataBlockBuilder_Add_AfterFinish) {
    DataBlockBuilder b;
    b.Add("a", "1");
    b.Finish();
    EXPECT_THROW(b.Add("b", "2"), std::runtime_error);
}

/**
 * @test DataBlockReader: block too small to contain even the trailer.
 */
TEST(SSTableBlocks, DataBlockReader_Corrupt_TooSmall) {
    std::string block = "abc";
    EXPECT_THROW(DataBlockReader r(block), std::runtime_error);
}

/**
 * @test DataBlockReader: corrupted trailer (truncate restart array/count).
 */
TEST(SSTableBlocks, DataBlockReader_Corrupt_BadRestarts) {
    DataBlockBuilder b;
    b.Add("a", "1");
    std::string block = b.Finish();

    // Corrupt by removing 5 bytes from the end (will break restart parsing).
    block.resize(block.size() - 5);
    EXPECT_THROW(DataBlockReader r(block), std::runtime_error);
}

/**
 * @test IndexBlockBuilder enforces strictly increasing keys.
 */
TEST(SSTableBlocks, IndexBlockBuilder_Add_OutOfOrder) {
    IndexBlockBuilder ib;
    BlockHandle h{0, 0};
    ib.Add("b", h);
    EXPECT_THROW(ib.Add("a", h), std::runtime_error);
}

/**
 * @test IndexBlockReader: too small to contain even the count field.
 */
TEST(SSTableBlocks, IndexBlockReader_Corrupt_TooSmall) {
    std::string block = "abc";
    EXPECT_THROW(IndexBlockReader r(block), std::runtime_error);
}

/**
 * @test IndexBlockReader: malformed offsets region triggers corruption errors.
 *
 * Construct a bogus block:
 *   - Put some payload bytes.
 *   - Append one offset (pointing to 0).
 *   - Append an absurdly large "num_offsets" count that cannot fit.
 */
TEST(SSTableBlocks, IndexBlockReader_Corrupt_BadOffsets) {
    std::string block;

    // Fake entry payload (meaningless data to keep the test self-contained).
    block.append("a");

    // Add a fake offset (little-endian 0).
    uint32_t offset = 0;
    block.append(reinterpret_cast<char*>(&offset), sizeof(offset));

    // Add a corrupted number of offsets (too large; will fail size checks).
    uint32_t num_offsets = 1000;
    block.append(reinterpret_cast<char*>(&num_offsets), sizeof(num_offsets));

    EXPECT_THROW(IndexBlockReader r(block), std::runtime_error);
}

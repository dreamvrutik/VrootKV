#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <map>

#include "VrootKV/io/sstable_format.h"   // public
#include "src/io/sstable_blocks.h"       // internal (decls), implemented in .cpp

using namespace VrootKV::io;

TEST(SSTableFormat, BlockHandle_RoundTrip) {
    BlockHandle h{12345, 678};
    std::string buf;
    h.EncodeTo(buf);
    std::string_view sv = buf;
    BlockHandle got = BlockHandle::DecodeFrom(sv);
    EXPECT_EQ(got.offset, 12345u);
    EXPECT_EQ(got.size, 678u);
    EXPECT_TRUE(sv.empty());
}

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
    EXPECT_TRUE(sv.size() >= 0); // DecodeFrom doesn't advance sv (returns struct)
}

// ---------- Data Block: build -> read all keys ----------
TEST(SSTableBlocks, DataBlockBuilderReader_RoundTrip) {
    DataBlockBuilder b(/*restart_interval=*/2);

    std::vector<std::pair<std::string,std::string>> kv = {
        {"apple",  "A"}, {"apples", "AA"}, {"apply",  "AAA"},
        {"banana", "B"}, {"carrot", "C"},  {"carrots","CC"}
    };
    for (auto& p : kv) b.Add(p.first, p.second);
    std::string block = b.Finish();

    DataBlockReader r(block);
    for (auto& p : kv) {
        std::string v;
        EXPECT_TRUE(r.Get(p.first, v));
        EXPECT_EQ(v, p.second);
    }
    // Some negative lookups inside ranges and beyond
    std::string v;
    EXPECT_FALSE(r.Get("appl", v));
    EXPECT_FALSE(r.Get("blueberry", v));
    EXPECT_FALSE(r.Get("zzz", v));
}

// ---------- Index Block: binary search ----------
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

    // Keys map to the rightmost divider <= key
    EXPECT_FALSE(ir.Find("aardvark", h));       // before first divider
    ASSERT_TRUE(ir.Find("apple", h));  EXPECT_EQ(h.offset, h1.offset);
    ASSERT_TRUE(ir.Find("apricot", h)); EXPECT_EQ(h.offset, h1.offset);
    ASSERT_TRUE(ir.Find("banana", h));  EXPECT_EQ(h.offset, h2.offset);
    ASSERT_TRUE(ir.Find("blueberry", h)); EXPECT_EQ(h.offset, h2.offset);
    ASSERT_TRUE(ir.Find("carrot", h));  EXPECT_EQ(h.offset, h3.offset);
    ASSERT_TRUE(ir.Find("zzz", h));      EXPECT_EQ(h.offset, h3.offset);
}

// ---------- End-to-end: 2 data blocks + index + footer ----------
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

    // Index maps "ant" -> d1 and "banana" -> d2
    IndexBlockBuilder ib;
    BlockHandle h1{0, static_cast<uint64_t>(d1.size())};
    BlockHandle h2{0, static_cast<uint64_t>(d2.size())}; // temp offsets; we’ll fix after layout
    ib.Add(kv1.front().first, h1);
    ib.Add(kv2.front().first, h2);
    std::string idx = ib.Finish();

    // Lay out a fake SSTable: [d1][d2][idx][footer]
    std::string file;
    uint64_t off_d1 = 0;
    file.append(d1);
    uint64_t off_d2 = static_cast<uint64_t>(file.size());
    file.append(d2);
    uint64_t off_idx = static_cast<uint64_t>(file.size());
    file.append(idx);

    // Fix up handles with real offsets (sizes already set)
    h1.offset = off_d1; h2.offset = off_d2;
    // Rebuild index with corrected offsets
    IndexBlockBuilder ib2;
    ib2.Add(kv1.front().first, h1);
    ib2.Add(kv2.front().first, h2);
    idx = ib2.Finish();

    // Append final index and footer
    off_idx = static_cast<uint64_t>(file.size());
    file.append(idx);

    SSTableFooter footer;
    footer.filter_handle = BlockHandle{0, 0};              // no filter in this unit test
    footer.index_handle  = BlockHandle{off_idx, static_cast<uint64_t>(idx.size())};
    footer.magic         = 0xF00DBAADF00DBAADull;
    std::string fbuf; footer.EncodeTo(fbuf);
    file.append(fbuf);

    // ---- Read path: decode footer -> load index -> find block -> read data block
    // Footer sits at the end and is fixed-size.
    std::string_view sv(file);
    std::string_view footer_view = sv.substr(sv.size() - SSTableFooter::kEncodedLength);
    SSTableFooter got = SSTableFooter::DecodeFrom(footer_view);
    ASSERT_EQ(got.index_handle.offset, off_idx);
    ASSERT_EQ(got.index_handle.size, idx.size());

    std::string_view idx_view = sv.substr(got.index_handle.offset, got.index_handle.size);
    IndexBlockReader ir(idx_view);

    auto fetch = [&](const std::string& key) -> std::string {
        BlockHandle h{};
        bool ok = ir.Find(key, h);
        if (!ok) return "";
        std::string_view block = sv.substr(h.offset, h.size);
        DataBlockReader dr(block);
        std::string v;
        if (dr.Get(key, v)) return v;
        return "";
    };

    EXPECT_EQ(fetch("ant"), "1");
    EXPECT_EQ(fetch("apple"), "2");
    EXPECT_EQ(fetch("apples"), "3");
    EXPECT_EQ(fetch("banana"), "4");
    EXPECT_EQ(fetch("carrot"), "5");
    EXPECT_EQ(fetch("date"), "6");

    // Negative lookups travel correctly to the closest block and fail there.
    EXPECT_EQ(fetch("aaa"), "");        // before first
    EXPECT_EQ(fetch("blueberry"), "");  // between
    EXPECT_EQ(fetch("zzz"), "");        // after last
}

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <stdexcept>

// Private header per Implementation Guide
#include "src/wal/wal_format.h"

using namespace VrootKV::wal;

static WALRecord Make(uint64_t txn, RecordType t, std::string k, std::string v = {}) {
    WALRecord r;
    r.txn_id = txn;
    r.type = t;
    r.key = std::move(k);
    r.value = std::move(v);
    return r;
}

TEST(WAL, RoundTrip_AllRecordTypes) {
    std::vector<WALRecord> in = {
        Make(1, RecordType::BEGIN_TX,  "", ""),
        Make(1, RecordType::PUT,       "apple",  "red"),
        Make(1, RecordType::DELETE_,   "banana", ""),
        Make(1, RecordType::COMMIT_TX, "", ""),
        Make(2, RecordType::BEGIN_TX,  "", ""),
        Make(2, RecordType::ABORT_TX,  "", "")
    };

    std::string log;
    for (const auto& r : in) log.append(r.SerializeFrame());

    std::vector<WALRecord> out;
    std::string_view sv = log;
    while (!sv.empty()) {
        out.push_back(WALRecord::ParseFrame(sv));
    }

    ASSERT_EQ(out.size(), in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        EXPECT_EQ(out[i].txn_id, in[i].txn_id);
        EXPECT_EQ(static_cast<int>(out[i].type), static_cast<int>(in[i].type));
        EXPECT_EQ(out[i].key, in[i].key);
        EXPECT_EQ(out[i].value, in[i].value);
    }
}

TEST(WAL, Detects_CRC_Corruption) {
    WALRecord r = Make(42, RecordType::PUT, "key", "value");
    std::string frame = r.SerializeFrame();
    ASSERT_GE(frame.size(), 9u);
    // Flip a byte in the payload (after 8-byte header)
    frame[8 + 2] ^= 0x01;

    std::string_view sv = frame;
    EXPECT_THROW({ (void)WALRecord::ParseFrame(sv); }, std::runtime_error);
}

TEST(WAL, Detects_Truncated_Header) {
    std::string bad = "\x01\x00\x00"; // < 8 bytes header
    std::string_view sv = bad;
    EXPECT_THROW({ (void)WALRecord::ParseFrame(sv); }, std::runtime_error);
}

TEST(WAL, Detects_Truncated_Payload) {
    WALRecord r = Make(7, RecordType::PUT, "a", "b");
    std::string frame = r.SerializeFrame();
    // Claim full header, then drop some payload bytes
    frame.resize(8 + 3); // definitely too small for payload
    std::string_view sv = frame;
    EXPECT_THROW({ (void)WALRecord::ParseFrame(sv); }, std::runtime_error);
}

TEST(WAL, Handles_Large_Key_Value) {
    std::string big_key(8192, 'K');
    std::string big_val(16384, 'V');

    WALRecord in = Make(99, RecordType::PUT, big_key, big_val);
    std::string frame = in.SerializeFrame();

    std::string_view sv = frame;
    WALRecord out = WALRecord::ParseFrame(sv);

    EXPECT_EQ(out.txn_id, 99u);
    EXPECT_EQ(out.key.size(), big_key.size());
    EXPECT_EQ(out.value.size(), big_val.size());
    EXPECT_EQ(out.key, big_key);
    EXPECT_EQ(out.value, big_val);
}

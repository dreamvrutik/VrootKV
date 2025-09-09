/**
 * @file test_wal.cpp
 * @author Vrutik Halani
 * @brief Unit tests for the Write-Ahead Log (WAL) framing and integrity checks.
 *
 * What these tests cover
 * ----------------------
 * • **Round-trip serialization** for all record types:
 *      BEGIN_TX, PUT, DELETE_, COMMIT_TX, ABORT_TX
 * • **Integrity enforcement**:
 *      - CRC32 mismatch detection
 *      - Truncated header and payload detection
 * • **Scalability**:
 *      - Large key/value payload support within a single framed record
 *
 * WAL frame format (from wal_format.h)
 * ------------------------------------
 *   [len: u32][crc32: u32][payload bytes, length = len]
 * Payload:
 *   [txn_id: u64][type: u8][key_len: varint32][value_len: varint32][key][value]
 *
 * Notes
 * -----
 * - Tests use the private header per the Implementation Guide: "src/wal/wal_format.h".
 * - `ParseFrame` throws std::runtime_error on corruption or truncation.
 * - All integers are little-endian; CRC32 covers only the payload bytes.
 */

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <stdexcept>

// Private header per Implementation Guide
#include "src/wal/wal_format.h"

using namespace VrootKV::wal;

/**
 * @brief Helper to construct a WALRecord in a concise way for tests.
 *
 * @param txn Transaction ID to set on the record.
 * @param t   RecordType (BEGIN_TX, PUT, DELETE_, COMMIT_TX, ABORT_TX).
 * @param k   Key bytes (may be empty for non-key-bearing record types).
 * @param v   Value bytes (may be empty; must be empty for DELETE_, BEGIN/COMMIT/ABORT).
 */
static WALRecord Make(uint64_t txn, RecordType t, std::string k, std::string v = {}) {
    WALRecord r;
    r.txn_id = txn;
    r.type = t;
    r.key = std::move(k);
    r.value = std::move(v);
    return r;
}

/**
 * @test Round-trip encode → decode for a sequence containing all record types.
 *
 * Procedure:
 *  1) Build a vector of WALRecord instances (covering all types).
 *  2) Serialize each to a framed byte string and concatenate into `log`.
 *  3) Repeatedly parse frames from `log` into `out` until exhausted.
 *  4) Assert 1:1 match on txn_id, type, key, and value fields.
 */
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
    for (const auto& r : in) {
        log.append(r.SerializeFrame());
    }

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

/**
 * @test Corruption detection: CRC32 mismatch triggers a parse error.
 *
 * Method:
 *  - Serialize a valid PUT record.
 *  - Flip a byte inside the payload (not the header) to invalidate the CRC.
 *  - Expect ParseFrame to throw std::runtime_error.
 */
TEST(WAL, Detects_CRC_Corruption) {
    WALRecord r = Make(42, RecordType::PUT, "key", "value");
    std::string frame = r.SerializeFrame();
    ASSERT_GE(frame.size(), 9u);
    // Flip a byte in the payload (after 8-byte header) to break the CRC
    frame[8 + 2] ^= 0x01;

    std::string_view sv = frame;
    EXPECT_THROW({ (void)WALRecord::ParseFrame(sv); }, std::runtime_error);
}

/**
 * @test Truncated header: fewer than 8 bytes (len + crc32) must fail parsing.
 */
TEST(WAL, Detects_Truncated_Header) {
    std::string bad = "\x01\x00\x00"; // < 8 bytes header
    std::string_view sv = bad;
    EXPECT_THROW({ (void)WALRecord::ParseFrame(sv); }, std::runtime_error);
}

/**
 * @test Truncated payload: header claims more bytes than provided → parse error.
 *
 * Method:
 *  - Serialize a valid frame.
 *  - Keep the 8-byte header intact but truncate payload bytes.
 *  - Expect ParseFrame to throw std::runtime_error.
 */
TEST(WAL, Detects_Truncated_Payload) {
    WALRecord r = Make(7, RecordType::PUT, "a", "b");
    std::string frame = r.SerializeFrame();
    // Claim full header, then drop some payload bytes (force truncation)
    frame.resize(8 + 3);
    std::string_view sv = frame;
    EXPECT_THROW({ (void)WALRecord::ParseFrame(sv); }, std::runtime_error);
}

/**
 * @test Large key/value: ensure serializer and parser handle large payloads.
 *
 * Case:
 *  - Key:  8 KiB
 *  - Value: 16 KiB
 * Verifies:
 *  - Exact sizes are preserved.
 *  - Byte-wise equality of key and value.
 */
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

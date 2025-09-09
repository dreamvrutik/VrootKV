/**
 * @file wal_format.h
 * @author Vrutik Halani
 * @brief On-disk format and (de)serialization utilities for the Write-Ahead Log (WAL).
 *
 * Overview
 * --------
 * The WAL is an append-only sequence of **frames**. Each frame contains a single
 * logical record (e.g., BEGIN, PUT, DELETE, COMMIT, ABORT) and is independently
 * checksummed to detect corruption.
 *
 * Frame layout (little-endian)
 * ----------------------------
 *   [len: u32][crc32: u32][payload bytes, length = len]
 *
 * Payload layout
 * --------------
 *   [txn_id: u64][type: u8][key_len: varint32][value_len: varint32][key][value]
 *
 *   • For BEGIN/COMMIT/ABORT: key_len = value_len = 0
 *   • For DELETE: key_len > 0, value_len = 0
 *   • For PUT: key_len > 0, value_len >= 0
 *
 * Integrity
 * ---------
 * The `crc32` covers only the payload bytes in the frame. A mismatch indicates
 * corruption and results in a parse error.
 *
 * Endianness & Encoding
 * ---------------------
 * • All fixed-width integers use **little-endian** byte order.
 * • `varint32` uses 7-bit payload per byte with MSB as a continuation flag.
 *
 * Notes
 * -----
 * • `ParseFrame` consumes bytes from the input `std::string_view` upon success.
 * • Functions throw `std::runtime_error` on truncated input or corruption.
 * • This header intentionally has no dynamic allocations in helpers; the record
 *   serializer uses `std::string` as the output buffer.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <stdexcept>

namespace VrootKV::wal {

// ============================================================================
// Helpers (little-endian I/O, varint32, CRC32)
// ============================================================================
namespace detail {

/**
 * @brief Append a 32-bit integer in little-endian to `dst`.
 */
inline void PutFixed32(std::string& dst, uint32_t v) {
    char buf[4];
    std::memcpy(buf, &v, 4);
    dst.append(buf, 4);
}

/**
 * @brief Append a 64-bit integer in little-endian to `dst`.
 */
inline void PutFixed64(std::string& dst, uint64_t v) {
    char buf[8];
    std::memcpy(buf, &v, 8);
    dst.append(buf, 8);
}

/**
 * @brief Decode a 32-bit little-endian integer from memory.
 * @param p Pointer to at least 4 readable bytes.
 */
inline uint32_t DecodeFixed32(const char* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

/**
 * @brief Decode a 64-bit little-endian integer from memory.
 * @param p Pointer to at least 8 readable bytes.
 */
inline uint64_t DecodeFixed64(const char* p) {
    uint64_t v;
    std::memcpy(&v, p, 8);
    return v;
}

/**
 * @brief Append a varint-encoded 32-bit unsigned integer to `dst`.
 *
 * Encoding:
 *   • 7 data bits per byte (little-endian groups)
 *   • MSB set indicates continuation
 */
inline void PutVarint32(std::string& dst, uint32_t v) {
    unsigned char buf[5];
    int i = 0;
    while (v >= 0x80) {
        buf[i++] = static_cast<unsigned char>(v | 0x80);
        v >>= 7;
    }
    buf[i++] = static_cast<unsigned char>(v);
    dst.append(reinterpret_cast<char*>(buf), i);
}

/**
 * @brief Decode a varint32 from the front of `in`, advancing it on success.
 * @param in  [in/out] Byte stream; advanced past the varint on success.
 * @param out [out]    Decoded value.
 * @return true if decoded; false on truncation or overlong encoding.
 */
inline bool GetVarint32(std::string_view& in, uint32_t& out) {
    uint32_t result = 0;
    int shift = 0;
    size_t i = 0;
    while (i < in.size() && shift <= 28) {
        uint8_t byte = static_cast<uint8_t>(in[i++]);
        result |= static_cast<uint32_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            out = result;
            in.remove_prefix(i);
            return true;
        }
        shift += 7;
    }
    return false;
}

/**
 * @brief Compute CRC32 (IEEE 802.3, polynomial 0xEDB88320) over a byte buffer.
 * @param data Pointer to bytes.
 * @param n    Number of bytes.
 * @return CRC32 checksum.
 *
 * Implementation details:
 *   • Lazy-initialized, static 256-entry lookup table.
 *   • Standard reflected CRC used in many storage systems.
 */
inline uint32_t Crc32(const uint8_t* data, size_t n) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

} // namespace detail

// ============================================================================
// WAL record types & on-disk framing
// ============================================================================

/**
 * @enum RecordType
 * @brief Logical types of WAL records. Serialized as a single byte in payload.
 */
enum class RecordType : uint8_t {
    BEGIN_TX  = 0,  ///< Start of a transaction (key/value empty)
    PUT       = 1,  ///< Upsert of a key/value pair
    DELETE_   = 2,  ///< Deletion by key (value empty)
    COMMIT_TX = 3,  ///< Successful transaction commit (key/value empty)
    ABORT_TX  = 4   ///< Transaction aborted/rolled back (key/value empty)
};

/**
 * @struct WALRecord
 * @brief In-memory representation of a WAL record and (de)serialization helpers.
 *
 * Payload encoding:
 *   [txn_id: u64][type: u8][key_len: varint32][value_len: varint32][key][value]
 */
struct WALRecord {
    uint64_t txn_id = 0;                 ///< Transaction identifier.
    RecordType type = RecordType::BEGIN_TX;
    std::string key;                     ///< Key bytes (may be empty).
    std::string value;                   ///< Value bytes (may be empty).

    /**
     * @brief Serialize only the payload portion (no frame header).
     * @return Byte string containing the payload encoding.
     *
     * Layout:
     *   [txn_id(8)][type(1)][key_len(varint32)][value_len(varint32)][key][value]
     */
    std::string SerializePayload() const {
        std::string out;
        out.reserve(16 + key.size() + value.size());
        detail::PutFixed64(out, txn_id);
        out.push_back(static_cast<char>(type));
        detail::PutVarint32(out, static_cast<uint32_t>(key.size()));
        detail::PutVarint32(out, static_cast<uint32_t>(value.size()));
        out.append(key);
        out.append(value);
        return out;
    }

    /**
     * @brief Serialize the full on-disk frame (header + payload + CRC).
     * @return Byte string ready to append to the WAL file.
     *
     * Frame:
     *   [len: u32][crc32: u32][payload bytes]
     *
     * • `len` is the number of payload bytes.
     * • `crc32` is computed over exactly the payload bytes.
     */
    std::string SerializeFrame() const {
        std::string payload = SerializePayload();
        std::string out;
        out.reserve(8 + payload.size());
        detail::PutFixed32(out, static_cast<uint32_t>(payload.size()));
        const uint32_t crc = detail::Crc32(
            reinterpret_cast<const uint8_t*>(payload.data()),
            payload.size()
        );
        detail::PutFixed32(out, crc);
        out.append(payload);
        return out;
    }

    /**
     * @brief Parse a single framed record from the front of `in`.
     * @param in [in/out] Byte stream containing one or more frames; advanced past
     *                    the parsed frame on success.
     * @return Parsed `WALRecord`.
     *
     * @throws std::runtime_error on truncated header/payload or CRC mismatch.
     *
     * Notes:
     *  • On success, `in` is advanced by 8 + `len` bytes.
     *  • On failure, `in` is left in an unspecified state (caller should abort).
     */
    static WALRecord ParseFrame(std::string_view& in) {
        if (in.size() < 8) {
            throw std::runtime_error("WAL: truncated header");
        }

        const uint32_t len = detail::DecodeFixed32(in.data());
        const uint32_t crc = detail::DecodeFixed32(in.data() + 4);
        in.remove_prefix(8);

        if (in.size() < len) {
            throw std::runtime_error("WAL: truncated payload");
        }

        std::string_view payload = in.substr(0, len);

        const uint32_t got = detail::Crc32(
            reinterpret_cast<const uint8_t*>(payload.data()),
            payload.size()
        );
        if (got != crc) {
            throw std::runtime_error("WAL: CRC mismatch");
        }

        WALRecord r = ParsePayload(payload);
        in.remove_prefix(len);
        return r;
    }

    /**
     * @brief Parse a payload (without frame header) from a byte slice.
     * @param payload Exact payload slice to decode (not consumed on success).
     * @return Parsed `WALRecord`.
     *
     * @throws std::runtime_error on malformed or truncated payload.
     *
     * Implementation details:
     *  • Decodes fixed width fields (txn_id, type), then varint32 lengths,
     *    followed by the key and value bytes.
     */
    static WALRecord ParsePayload(std::string_view payload) {
        if (payload.size() < 9) {
            throw std::runtime_error("WAL: payload too small");
        }

        WALRecord r;
        r.txn_id = detail::DecodeFixed64(payload.data());
        payload.remove_prefix(8);

        r.type = static_cast<RecordType>(static_cast<uint8_t>(payload[0]));
        payload.remove_prefix(1);

        uint32_t klen = 0, vlen = 0;
        if (!detail::GetVarint32(payload, klen)) {
            throw std::runtime_error("WAL: bad key length");
        }
        if (!detail::GetVarint32(payload, vlen)) {
            throw std::runtime_error("WAL: bad value length");
        }
        if (payload.size() < static_cast<size_t>(klen) + vlen) {
            throw std::runtime_error("WAL: truncated kv");
        }

        r.key.assign(payload.substr(0, klen));
        payload.remove_prefix(klen);

        r.value.assign(payload.substr(0, vlen));
        return r;
    }
};

} // namespace VrootKV::wal

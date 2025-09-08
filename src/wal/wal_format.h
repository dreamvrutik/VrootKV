#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <stdexcept>

namespace VrootKV::wal {

// ---------- Helpers (little-endian + varint + CRC32) ----------
namespace detail {

// Put/Get fixed-size little-endian integers
inline void PutFixed32(std::string& dst, uint32_t v) {
    char buf[4];
    std::memcpy(buf, &v, 4);
    dst.append(buf, 4);
}
inline void PutFixed64(std::string& dst, uint64_t v) {
    char buf[8];
    std::memcpy(buf, &v, 8);
    dst.append(buf, 8);
}
inline uint32_t DecodeFixed32(const char* p) {
    uint32_t v; std::memcpy(&v, p, 4); return v;
}
inline uint64_t DecodeFixed64(const char* p) {
    uint64_t v; std::memcpy(&v, p, 8); return v;
}

// Varint32 (compact for lengths)
inline void PutVarint32(std::string& dst, uint32_t v) {
    unsigned char buf[5];
    int i = 0;
    while (v >= 0x80) {
        buf[i++] = static_cast<unsigned char>(v | 0x80); v >>= 7;
    }
    buf[i++] = static_cast<unsigned char>(v);
    dst.append(reinterpret_cast<char*>(buf), i);
}
inline bool GetVarint32(std::string_view& in, uint32_t& out) {
    uint32_t result = 0; int shift = 0; size_t i = 0;
    while (i < in.size() && shift <= 28) {
        uint8_t byte = static_cast<uint8_t>(in[i++]);
        result |= (uint32_t)(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            out = result; in.remove_prefix(i); return true;
        }
        shift += 7;
    }
    return false;
}

// CRC32 (IEEE 802.3 poly 0xEDB88320)
inline uint32_t Crc32(const uint8_t* data, size_t n) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i)
        c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

} // namespace detail

// ---------- WAL Record Types & Layout ----------
// On-disk frame = [len(4)][crc32(4)][payload bytes of length=len]
// payload = [txn_id(8)][type(1)][key_len(varint32)][value_len(varint32)][key bytes][value bytes]
// BEGIN/COMMIT/ABORT may have empty key/value. DELETE has key only (value_len=0). PUT has key+value.
// See System Design doc for header and fields. 
// (length & crc32 are little-endian). 
enum class RecordType : uint8_t {
    BEGIN_TX = 0,
    PUT = 1,
    DELETE_ = 2,
    COMMIT_TX = 3,
    ABORT_TX = 4
};

struct WALRecord {
    uint64_t txn_id = 0;
    RecordType type = RecordType::BEGIN_TX;
    std::string key;
    std::string value;

    // Serialize payload only
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

    // Serialize full on-disk frame (header+payload)
    std::string SerializeFrame() const {
        std::string payload = SerializePayload();
        std::string out;
        out.reserve(8 + payload.size());
        detail::PutFixed32(out, static_cast<uint32_t>(payload.size()));
        uint32_t crc = detail::Crc32(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
        detail::PutFixed32(out, crc);
        out.append(payload);
        return out;
    }

    // Parse one frame from input buffer (throws std::runtime_error on corruption)
    // On success, removes the consumed frame bytes from `in`.
    static WALRecord ParseFrame(std::string_view& in) {
        if (in.size() < 8) throw std::runtime_error("WAL: truncated header");
        uint32_t len = detail::DecodeFixed32(in.data());
        uint32_t crc = detail::DecodeFixed32(in.data()+4);
        in.remove_prefix(8);
        if (in.size() < len) throw std::runtime_error("WAL: truncated payload");
        std::string_view payload = in.substr(0, len);
        uint32_t got = detail::Crc32(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
        if (got != crc) throw std::runtime_error("WAL: CRC mismatch");
        WALRecord r = ParsePayload(payload);
        in.remove_prefix(len);
        return r;
    }

    // Parse payload (internal)
    static WALRecord ParsePayload(std::string_view payload) {
        if (payload.size() < 9) throw std::runtime_error("WAL: payload too small");
        WALRecord r;
        r.txn_id = detail::DecodeFixed64(payload.data());
        payload.remove_prefix(8);
        r.type = static_cast<RecordType>(static_cast<uint8_t>(payload[0]));
        payload.remove_prefix(1);

        uint32_t klen=0, vlen=0;
        if (!detail::GetVarint32(payload, klen)) throw std::runtime_error("WAL: bad key length");
        if (!detail::GetVarint32(payload, vlen)) throw std::runtime_error("WAL: bad value length");
        if (payload.size() < (size_t)klen + vlen) throw std::runtime_error("WAL: truncated kv");
        r.key.assign(payload.substr(0, klen));
        payload.remove_prefix(klen);
        r.value.assign(payload.substr(0, vlen));
        return r;
    }
};

} // namespace VrootKV::wal

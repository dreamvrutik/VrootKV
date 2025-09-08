#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "sstable_blocks.h"       // class declarations
#include "VrootKV/io/sstable_format.h"   // BlockHandle

namespace VrootKV::io {

// --------------------------- local helpers ----------------------------
namespace detail {
inline uint32_t DecodeFixed32(const char* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
inline bool GetVarint32(std::string_view& in, uint32_t& out) {
    uint32_t result=0;
    int shift=0;
    size_t i=0;
    while (i < in.size() && shift <= 28) {
        uint8_t b = static_cast<uint8_t>(in[i++]);
        result |= (b & 0x7F) << shift;
        if ((b & 0x80) == 0) {
            out = result;
            in.remove_prefix(i);
            return true;
        }
        shift += 7;
    }
    return false;
}
} // namespace detail

// ======================= DataBlockReader defs ==========================
DataBlockReader::DataBlockReader(std::string_view block)
    : full_(block) {
    if (block.size() < 4) throw std::runtime_error("DataBlockReader: block too small");
    uint32_t num_restarts = detail::DecodeFixed32(block.data() + block.size() - 4);
    size_t rest_bytes = static_cast<size_t>(num_restarts) * 4;
    if (block.size() < 4 + rest_bytes) throw std::runtime_error("DataBlockReader: corrupt");
    restarts_.assign(num_restarts, 0);
    const char* p = block.data() + block.size() - 4 - rest_bytes;
    for (uint32_t i = 0; i < num_restarts; ++i) restarts_[i] = detail::DecodeFixed32(p + i*4);
    entries_ = block.substr(0, block.size() - 4 - rest_bytes);
}

bool DataBlockReader::Get(std::string_view key, std::string& value_out) const {
    if (restarts_.empty()) return false;

    auto key_at_offset = [&](uint32_t off, std::string& k) -> bool {
        if (off + 12 > entries_.size()) return false;
        const char* q = entries_.data() + off;
        uint32_t shared    = detail::DecodeFixed32(q + 0);
        uint32_t nonshared = detail::DecodeFixed32(q + 4);
        uint32_t vlen      = detail::DecodeFixed32(q + 8);
        if (shared != 0) return false;
        size_t need = 12ull + nonshared + vlen;
        if (off + need > entries_.size()) return false;
        k.assign(q + 12, nonshared);
        return true;
    };

    // Find rightmost restart key <= target
    int lo = 0, hi = static_cast<int>(restarts_.size()) - 1;
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        std::string k;
        if (!key_at_offset(restarts_[mid], k)) return false;
        if (k <= key) lo = mid;
        else hi = mid - 1;
    }

    // Scan within the chosen restart run
    uint32_t off = restarts_[lo];
    std::string prev_key;
    while (off < entries_.size()) {
        if (lo+1 < (int)restarts_.size() && off >= restarts_[lo+1]) break;

        if (off + 12 > entries_.size()) return false;
        const char* p = entries_.data() + off;
        uint32_t shared    = detail::DecodeFixed32(p + 0);
        uint32_t nonshared = detail::DecodeFixed32(p + 4);
        uint32_t vlen      = detail::DecodeFixed32(p + 8);
        size_t need = 12ull + nonshared + vlen;
        if (off + need > entries_.size()) return false;

        std::string cur_key;
        if (shared == 0) {
            cur_key.assign(p + 12, nonshared);
        } else {
            cur_key = prev_key;
            if (shared > cur_key.size()) return false;
            cur_key.resize(shared);
            cur_key.append(p + 12, nonshared);
        }

        if (cur_key == key) {
            value_out.assign(p + 12 + nonshared, vlen);
            return true;
        }
        if (cur_key > key) return false;

        prev_key.swap(cur_key);
        off += static_cast<uint32_t>(need);
    }
    return false;
}

// ======================= IndexBlockReader defs =========================
IndexBlockReader::IndexBlockReader(std::string_view block)
    : full_(block) {
    if (full_.size() < 4) throw std::runtime_error("IndexBlockReader: block too small");
    uint32_t num = detail::DecodeFixed32(full_.data() + full_.size() - 4);
    size_t off_bytes = static_cast<size_t>(num) * 4;
    if (full_.size() < 4 + off_bytes) throw std::runtime_error("IndexBlockReader: corrupt");
    num_ = num;
    const char* p = full_.data() + full_.size() - 4 - off_bytes;
    offsets_.assign(num_, 0);
    for (uint32_t i = 0; i < num_; ++i) offsets_[i] = detail::DecodeFixed32(p + i*4);
    entries_ = full_.substr(0, full_.size() - 4 - off_bytes);
}

bool IndexBlockReader::Find(std::string_view search_key, BlockHandle& handle_out) const {
    if (num_ == 0) return false;

    auto key_at = [&](int idx, std::string& key, BlockHandle* h) -> bool {
        std::string_view sv = entries_.substr(offsets_[idx]);
        uint32_t klen=0; if (!detail::GetVarint32(sv, klen)) return false;
        if (sv.size() < klen + BlockHandle::kEncodedLength) return false;
        key.assign(sv.data(), klen);
        sv.remove_prefix(klen);
        if (h) { *h = BlockHandle::DecodeFrom(sv); }
        return true;
    };

    int lo = 0, hi = static_cast<int>(num_) - 1;
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        std::string mk; if (!key_at(mid, mk, nullptr)) return false;
        if (mk <= search_key) lo = mid;
        else hi = mid - 1;
    }

    std::string key;
    if (!key_at(lo, key, &handle_out)) return false;
    if (key > search_key) return false;
    return true;
}

} // namespace VrootKV::io

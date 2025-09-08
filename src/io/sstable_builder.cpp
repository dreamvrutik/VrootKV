#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "sstable_blocks.h"       // class declarations
#include "VrootKV/io/sstable_format.h"   // BlockHandle, SSTableFooter

namespace VrootKV::io {

// --------------------------- local helpers ----------------------------
namespace detail {
inline void PutFixed32(std::string& dst, uint32_t v) {
    char b[4];
    std::memcpy(b, &v, 4);
    dst.append(b, 4);
}
inline uint32_t DecodeFixed32(const char* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
inline uint32_t SharedPrefix(const std::string& a, const std::string& b) {
    const uint32_t n = static_cast<uint32_t>(std::min(a.size(), b.size()));
    uint32_t i = 0;
    while (i < n && a[i] == b[i]) ++i;
    return i;
}
inline void PutVarint32(std::string& dst, uint32_t v) {
    unsigned char buf[5];
    int i=0;
    while (v >= 0x80) {
        buf[i++] = static_cast<unsigned char>(v | 0x80);
        v >>= 7;
    }
    buf[i++] = static_cast<unsigned char>(v);
    dst.append(reinterpret_cast<char*>(buf), i);
}
} // namespace detail

// ======================= DataBlockBuilder defs =========================
DataBlockBuilder::DataBlockBuilder(int restart_interval)
    : restart_interval_(restart_interval),
    counter_(0),
    finished_(false) {
        restarts_.push_back(0); // first restart at offset 0
}

void DataBlockBuilder::Add(const std::string& key, const std::string& value) {
    if (finished_) throw std::runtime_error("DataBlockBuilder: already finished");
    if (!last_key_.empty() && !(last_key_ < key)) {
        throw std::runtime_error("DataBlockBuilder: keys must be strictly increasing");
    }

    uint32_t shared = 0;
    if (counter_ < restart_interval_) {
        shared = detail::SharedPrefix(last_key_, key);
    } else {
        restarts_.push_back(static_cast<uint32_t>(buffer_.size()));
        counter_ = 0;
        shared = 0;
    }
    const uint32_t non_shared = static_cast<uint32_t>(key.size() - shared);
    const uint32_t value_len  = static_cast<uint32_t>(value.size());

    detail::PutFixed32(buffer_, shared);
    detail::PutFixed32(buffer_, non_shared);
    detail::PutFixed32(buffer_, value_len);

    buffer_.append(key.data() + shared, non_shared);
    buffer_.append(value.data(), value_len);

    last_key_ = key;
    ++counter_;
}

std::string DataBlockBuilder::Finish() {
    if (finished_) return buffer_;
    for (uint32_t off : restarts_) detail::PutFixed32(buffer_, off);
    detail::PutFixed32(buffer_, static_cast<uint32_t>(restarts_.size()));
    finished_ = true;
    return buffer_;
}

size_t DataBlockBuilder::CurrentSize() const {
    return buffer_.size() + (restarts_.size() + 1) * 4;
}

// ======================= IndexBlockBuilder defs ========================
void IndexBlockBuilder::Add(const std::string& divider_key, const BlockHandle& handle) {
    if (!last_key_.empty() && !(last_key_ < divider_key)) {
        throw std::runtime_error("IndexBlockBuilder: keys must be strictly increasing");
    }
    offsets_.push_back(static_cast<uint32_t>(buffer_.size()));
    detail::PutVarint32(buffer_, static_cast<uint32_t>(divider_key.size()));
    buffer_.append(divider_key);
    handle.EncodeTo(buffer_);
    last_key_ = divider_key;
}


std::string IndexBlockBuilder::Finish() {
    for (uint32_t off : offsets_) detail::PutFixed32(buffer_, off);
    detail::PutFixed32(buffer_, static_cast<uint32_t>(offsets_.size()));
    return buffer_;
}

} // namespace VrootKV::io

/**
 * @file sstable_builder.cpp
 * @author Vrutik Halani
 * @brief Implementations of the **builder** primitives for SSTable blocks.
 *
 * Overview
 * --------
 * This file defines the method bodies for:
 *
 *   • DataBlockBuilder — emits a LevelDB-style, restart-based prefix-compressed
 *     **data block** of sorted key–value entries.
 *
 *   • IndexBlockBuilder — emits a compact **index block** of (divider key → BlockHandle)
 *     entries with a trailing offset table for O(log N) lookups.
 *
 * Encodings (summary)
 * -------------------
 * Data block (per entry):
 *     [shared:u32][non_shared:u32][value_len:u32][key_delta bytes][value bytes]
 * Trailer:
 *     [restart_offsets:u32 array][num_restarts:u32]
 *
 * Index block (per entry):
 *     [key_len:varint32][key bytes][BlockHandle(16 bytes)]
 * Trailer:
 *     [entry_offsets:u32 array][num_entries:u32]
 *
 * Invariants
 * ----------
 * • Keys must be **strictly increasing** (lexicographic) when calling Add().
 * • After Finish(), a builder is immutable; further Add() calls are rejected.
 * • All fixed-width integers are serialized in **little-endian** order.
 */

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "sstable_blocks.h"                 // class declarations
#include "VrootKV/io/sstable_format.h"      // BlockHandle (for index entries)

namespace VrootKV::io {

// ============================================================================
// Local helpers (encoding utilities, small pure functions)
// ============================================================================
namespace detail {

/**
 * @brief Append a 32-bit little-endian integer to a byte buffer.
 */
inline void PutFixed32(std::string& dst, uint32_t v) {
    char b[4];
    std::memcpy(b, &v, 4);
    dst.append(b, 4);
}

/**
 * @brief Read a 32-bit little-endian integer from a memory location.
 * @note Caller must guarantee that `p` points to at least 4 readable bytes.
 */
inline uint32_t DecodeFixed32(const char* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

/**
 * @brief Return the length of the common prefix between two strings.
 */
inline uint32_t SharedPrefix(const std::string& a, const std::string& b) {
    const uint32_t n = static_cast<uint32_t>(std::min(a.size(), b.size()));
    uint32_t i = 0;
    while (i < n && a[i] == b[i]) ++i;
    return i;
}

/**
 * @brief Append a Varint32-encoded unsigned integer (little-endian, 7-bit groups).
 *
 * Encoding: low 7 bits per byte; continuation bit (MSB) set on all but last byte.
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
} // namespace detail

// ============================================================================
// DataBlockBuilder — restart-based prefix-compressed data block
// ============================================================================

/**
 * @brief Construct a builder with a chosen restart interval.
 *
 * @param restart_interval Number of entries between restart points (>= 1 recommended).
 *        A larger value tends to improve compression but may increase the per-lookup
 *        linear scan inside a restart run.
 *
 * @post The first restart offset (0) is pre-seeded.
 */
DataBlockBuilder::DataBlockBuilder(int restart_interval)
    : restart_interval_(restart_interval),
      counter_(0),
      finished_(false) {
    // We require the first restart to start at byte offset 0.
    restarts_.push_back(0);
}

/**
 * @brief Append a sorted key–value pair to the data block.
 *
 * Steps:
 *  1) Validate strict key ordering and immutability post-Finish().
 *  2) If we're at the configured restart interval boundary, start a new run
 *     by recording the current buffer offset and resetting the shared prefix.
 *  3) Compute the shared-prefix with the previous key in the current run.
 *  4) Emit the entry header: [shared][non_shared][value_len].
 *  5) Append the key delta (non-shared suffix) and the raw value bytes.
 */
void DataBlockBuilder::Add(const std::string& key, const std::string& value) {
    if (finished_) {
        throw std::runtime_error("DataBlockBuilder: already finished");
    }
    if (!last_key_.empty() && !(last_key_ < key)) {
        throw std::runtime_error("DataBlockBuilder: keys must be strictly increasing");
    }

    uint32_t shared = 0;
    if (counter_ < restart_interval_) {
        // Within a restart run: share prefix with the previous key.
        shared = detail::SharedPrefix(last_key_, key);
    } else {
        // Start a new restart run at the current buffer size.
        restarts_.push_back(static_cast<uint32_t>(buffer_.size()));
        counter_ = 0;
        shared = 0;
    }

    const uint32_t non_shared = static_cast<uint32_t>(key.size() - shared);
    const uint32_t value_len  = static_cast<uint32_t>(value.size());

    // Header: [shared][non_shared][value_len] (all fixed32 LE).
    detail::PutFixed32(buffer_, shared);
    detail::PutFixed32(buffer_, non_shared);
    detail::PutFixed32(buffer_, value_len);

    // Payload: key delta then value.
    buffer_.append(key.data() + shared, non_shared);
    buffer_.append(value.data(), value_len);

    last_key_ = key;
    ++counter_;
}

/**
 * @brief Finalize the data block and return the serialized bytes.
 *
 * Trailer layout:
 *   [restart_offsets: u32 array][num_restarts: u32]
 *
 * @return Serialized block. Multiple calls after the first return the same buffer.
 */
std::string DataBlockBuilder::Finish() {
    if (finished_) return buffer_;

    // Append all restart offsets followed by the count.
    for (uint32_t off : restarts_) {
        detail::PutFixed32(buffer_, off);
    }
    detail::PutFixed32(buffer_, static_cast<uint32_t>(restarts_.size()));

    finished_ = true;
    return buffer_;
}

/**
 * @brief Return the current byte size the block would have if finished now.
 *
 * The estimate includes the entry bytes already written and the eventual
 * trailer size: (restarts_.size() * 4) for the offsets plus 4 for the count.
 */
size_t DataBlockBuilder::CurrentSize() const {
    return buffer_.size() + (restarts_.size() + 1) * 4;
}

// ============================================================================
// IndexBlockBuilder — divider-key to BlockHandle mapping
// ============================================================================

/**
 * @brief Append one index entry mapping a divider key to a data block handle.
 *
 * Encoding:
 *   [key_len:varint32][key bytes][BlockHandle(16 bytes)]
 *
 * @throws std::runtime_error if keys are not strictly increasing.
 */
void IndexBlockBuilder::Add(const std::string& divider_key, const BlockHandle& handle) {
    if (!last_key_.empty() && !(last_key_ < divider_key)) {
        throw std::runtime_error("IndexBlockBuilder: keys must be strictly increasing");
    }

    // Record where this entry begins (for the trailing offset table).
    offsets_.push_back(static_cast<uint32_t>(buffer_.size()));

    // Varint length + key bytes.
    detail::PutVarint32(buffer_, static_cast<uint32_t>(divider_key.size()));
    buffer_.append(divider_key);

    // Fixed-size BlockHandle (16 bytes).
    handle.EncodeTo(buffer_);

    last_key_ = divider_key;
}

/**
 * @brief Finalize the index block and return the serialized bytes.
 *
 * Trailer layout:
 *   [entry_offsets: u32 array][num_entries: u32]
 */
std::string IndexBlockBuilder::Finish() {
    for (uint32_t off : offsets_) {
        detail::PutFixed32(buffer_, off);
    }
    detail::PutFixed32(buffer_, static_cast<uint32_t>(offsets_.size()));
    return buffer_;
}

} // namespace VrootKV::io

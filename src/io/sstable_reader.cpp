/**
 * @file sstable_reader.cpp
 * @author Vrutik Halani
 * @brief Implementations of the **reader** primitives for SSTable blocks.
 *
 * Overview
 * --------
 * This file provides the method bodies for:
 *
 *   • DataBlockReader — parses a restart-based, prefix-compressed **data block**
 *     of sorted key–value entries and supports efficient point lookups.
 *
 *   • IndexBlockReader — parses a compact **index block** that maps divider
 *     keys to `BlockHandle`s and supports O(log N) routing to the correct
 *     data block via binary search.
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
 * Invariants / Assumptions
 * ------------------------
 * • Keys in both blocks are **strictly increasing** (lexicographic).
 * • All fixed-width integers are encoded in **little-endian** order.
 * • Readers validate structural integrity; malformed input throws std::runtime_error.
 */

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "sstable_blocks.h"                 // class declarations
#include "VrootKV/io/sstable_format.h"      // BlockHandle

namespace VrootKV::io {

// ============================================================================
// Local helpers: minimal, dependency-free decoding utilities
// ============================================================================
namespace detail {

/**
 * @brief Decode a 32-bit little-endian integer from a raw memory pointer.
 * @param p Pointer to at least 4 readable bytes.
 * @return Decoded uint32_t.
 *
 * @note Callers ensure bounds safety before invoking this function.
 */
inline uint32_t DecodeFixed32(const char* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

/**
 * @brief Parse a Varint32-encoded unsigned integer from a string_view.
 * @param in  [in/out] Byte stream; advanced past the varint on success.
 * @param out [out]    Decoded value.
 * @return true if a well-formed varint was decoded; false otherwise.
 *
 * Encoding uses 7 data bits per byte; MSB indicates continuation.
 * This function guards against overlong encodings by stopping at 5 bytes.
 */
inline bool GetVarint32(std::string_view& in, uint32_t& out) {
    uint32_t result = 0;
    int shift = 0;
    size_t i = 0;
    while (i < in.size() && shift <= 28) {
        uint8_t b = static_cast<uint8_t>(in[i++]);
        result |= (static_cast<uint32_t>(b & 0x7F) << shift);
        if ((b & 0x80) == 0) {
            out = result;
            in.remove_prefix(i);
            return true;
        }
        shift += 7;
    }
    return false; // Truncated or too long
}

} // namespace detail

// ============================================================================
// DataBlockReader — restart-based prefix-compressed data block
// ============================================================================

/**
 * @brief Construct a reader for a serialized data block.
 * @param block Full block bytes, including trailer: restart offsets + count.
 *
 * Initialization:
 * 1) Validate the trailer length and extract `num_restarts`.
 * 2) Load the restart offsets array and cut `entries_` to exclude the trailer.
 *
 * @throws std::runtime_error on structural corruption (e.g., insufficient length).
 */
DataBlockReader::DataBlockReader(std::string_view block)
    : full_(block) {
    // Minimum trailer is 4 bytes for `num_restarts`.
    if (block.size() < 4) {
        throw std::runtime_error("DataBlockReader: block too small");
    }

    // Read restart count from the last 4 bytes.
    const uint32_t num_restarts = detail::DecodeFixed32(block.data() + block.size() - 4);

    // Guard against absurd counts that would overflow or underflow calculations.
    if (num_restarts > block.size() / 4) {
        throw std::runtime_error("DataBlockReader: corrupt");
    }

    const size_t rest_bytes = static_cast<size_t>(num_restarts) * 4;
    if (block.size() < 4 + rest_bytes) {
        throw std::runtime_error("DataBlockReader: corrupt");
    }

    // Parse restart offsets.
    restarts_.assign(num_restarts, 0);
    const char* p = block.data() + block.size() - 4 - rest_bytes;
    for (uint32_t i = 0; i < num_restarts; ++i) {
        restarts_[i] = detail::DecodeFixed32(p + i * 4);
    }

    // Entries region excludes the trailer (restart array + count).
    entries_ = block.substr(0, block.size() - 4 - rest_bytes);
}

/**
 * @brief Exact-match point lookup within the data block.
 * @param key       Target key to search for.
 * @param value_out On success, receives the corresponding value bytes.
 * @return true if found; false if the key is not in this block.
 *
 * Algorithm (LevelDB-style):
 * 1) Binary search over the restart offsets to find the rightmost restart key <= target.
 * 2) Linear scan within that restart run, reconstructing full keys using prefix sharing,
 *    until the key is found or we pass its sorted position.
 *
 * Robustness:
 * - Validates entry bounds at each step (header size and payload length).
 * - Returns false on benign "not found"; throws only on structural corruption.
 */
bool DataBlockReader::Get(std::string_view key, std::string& value_out) const {
    if (restarts_.empty()) return false;

    // Helper: materialize the full key at a given entry offset that starts a restart.
    auto key_at_offset = [&](uint32_t off, std::string& k) -> bool {
        if (off + 12 > entries_.size()) return false;

        const char* q = entries_.data() + off;
        const uint32_t shared    = detail::DecodeFixed32(q + 0);
        const uint32_t nonshared = detail::DecodeFixed32(q + 4);
        const uint32_t vlen      = detail::DecodeFixed32(q + 8);

        // The first entry in any restart run must have shared == 0.
        if (shared != 0) return false;

        const size_t need = 12ull + nonshared + vlen;
        if (off + need > entries_.size()) return false;

        k.assign(q + 12, nonshared); // full key for restart entry
        return true;
    };

    // --- Phase 1: find restart run using binary search on restart keys.
    int lo = 0;
    int hi = static_cast<int>(restarts_.size()) - 1;

    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        std::string restart_key;
        if (!key_at_offset(restarts_[mid], restart_key)) {
            // Structural issue in block
            return false;
        }
        if (restart_key <= key) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }

    // --- Phase 2: scan entries inside the chosen restart run.
    uint32_t off = restarts_[lo];
    std::string prev_key; // tracks last reconstructed key to build deltas

    while (off < entries_.size()) {
        // If we reached the next restart boundary, stop scanning this run.
        if (lo + 1 < static_cast<int>(restarts_.size()) && off >= restarts_[lo + 1]) {
            break;
        }

        // Validate header existence: we need 3 * 4 bytes for [shared][non_shared][value_len].
        if (off + 12 > entries_.size()) return false;

        const char* p = entries_.data() + off;
        const uint32_t shared    = detail::DecodeFixed32(p + 0);
        const uint32_t nonshared = detail::DecodeFixed32(p + 4);
        const uint32_t vlen      = detail::DecodeFixed32(p + 8);

        const size_t need = 12ull + nonshared + vlen;
        if (off + need > entries_.size()) return false;

        // Reconstruct current full key: either fresh (shared==0) or extend previous.
        std::string cur_key;
        if (shared == 0) {
            cur_key.assign(p + 12, nonshared);
        } else {
            cur_key = prev_key;
            if (shared > cur_key.size()) return false; // malformed shared prefix
            cur_key.resize(shared);
            cur_key.append(p + 12, nonshared);
        }

        // Compare and possibly return value.
        if (cur_key == key) {
            value_out.assign(p + 12 + nonshared, vlen);
            return true;
        }
        if (cur_key > key) {
            // Because keys are sorted, once we've passed the target we can stop.
            return false;
        }

        // Move forward within the run.
        prev_key.swap(cur_key);
        off += static_cast<uint32_t>(need);
    }

    return false;
}

// ============================================================================
// IndexBlockReader — divider-key to BlockHandle routing
// ============================================================================

/**
 * @brief Construct a reader for a serialized index block.
 * @param block Full index block bytes, including trailing [offsets][count].
 *
 * Initialization:
 * 1) Read the entry count from the tail.
 * 2) Validate and load the sorted entry offsets.
 * 3) `entries_` excludes the trailer for clean slicing during lookups.
 *
 * @throws std::runtime_error on structural corruption (bad sizes/offsets).
 */
IndexBlockReader::IndexBlockReader(std::string_view block)
    : full_(block) {
    // Must have at least 4 bytes for the entry count.
    if (full_.size() < 4) {
        throw std::runtime_error("IndexBlockReader: block too small");
    }

    // Read entry count from the last 4 bytes.
    const uint32_t num = detail::DecodeFixed32(full_.data() + full_.size() - 4);

    // Guard against absurd counts.
    if (num > full_.size() / 4) {
        throw std::runtime_error("IndexBlockReader: corrupt");
    }

    const size_t off_bytes = static_cast<size_t>(num) * 4;
    if (full_.size() < 4 + off_bytes) {
        throw std::runtime_error("IndexBlockReader: corrupt");
    }

    num_ = num;

    // Parse entry offsets and verify monotonicity and range.
    const char* p = full_.data() + full_.size() - 4 - off_bytes;
    offsets_.assign(num_, 0);
    for (uint32_t i = 0; i < num_; ++i) {
        const uint32_t off = detail::DecodeFixed32(p + i * 4);

        // Offsets must be non-decreasing.
        if (i > 0 && off < offsets_[i - 1]) {
            throw std::runtime_error("IndexBlockReader: corrupt offsets");
        }

        // Offsets must lie within the entries region (before the trailer).
        if (off > full_.size() - 4 - off_bytes) {
            throw std::runtime_error("IndexBlockReader: corrupt offsets");
        }

        offsets_[i] = off;
    }

    // Slice off the trailer so the entries region is clean to index into.
    entries_ = full_.substr(0, full_.size() - 4 - off_bytes);
}

/**
 * @brief Route `search_key` to the data block whose divider key is the last <= key.
 * @param search_key Key to route.
 * @param handle_out On success, receives the corresponding `BlockHandle`.
 * @return true if a suitable handle was found; false if `search_key` is smaller
 *         than the first divider key in this index block.
 *
 * Algorithm:
 * 1) Binary search over `offsets_` using the divider keys.
 * 2) Return the handle for the rightmost divider key <= `search_key`.
 *
 * Robustness:
 * - Validates each accessed entry (varint length and remaining bytes).
 * - Returns false on benign “before first” case; throws on structural corruption.
 */
bool IndexBlockReader::Find(std::string_view search_key, BlockHandle& handle_out) const {
    if (num_ == 0) return false;

    // Helper: decode the key and optionally the handle at entry `idx`.
    auto key_at = [&](int idx, std::string& key, BlockHandle* h) -> bool {
        std::string_view sv = entries_.substr(offsets_[idx]);

        // Decode varint length of the divider key.
        uint32_t klen = 0;
        if (!detail::GetVarint32(sv, klen)) {
            return false;
        }

        // Ensure key bytes + fixed BlockHandle fit in the remaining slice.
        if (sv.size() < klen + BlockHandle::kEncodedLength) {
            return false;
        }

        key.assign(sv.data(), klen);
        sv.remove_prefix(klen);

        if (h) {
            // DecodeFrom consumes 16 bytes from `sv`.
            *h = BlockHandle::DecodeFrom(sv);
        }
        return true;
    };

    // Binary search for rightmost divider key <= search_key.
    int lo = 0;
    int hi = static_cast<int>(num_) - 1;

    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        std::string mid_key;
        if (!key_at(mid, mid_key, nullptr)) {
            // Structural issue
            return false;
        }
        if (mid_key <= search_key) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }

    // Now `lo` is the candidate index. Validate and fetch its handle.
    std::string key;
    if (!key_at(lo, key, &handle_out)) {
        return false;
    }
    if (key > search_key) {
        // `search_key` is smaller than the first divider key.
        return false;
    }
    return true;
}

} // namespace VrootKV::io

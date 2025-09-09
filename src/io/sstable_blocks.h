/**
 * @file sstable_blocks.h
 * @author Vrutik Halani
 * @brief Builders and readers for SSTable Data and Index blocks.
 *
 * Overview
 * --------
 * This header declares the internal block primitives used by the SSTable:
 *
 *  - **DataBlockBuilder / DataBlockReader**
 *      A LevelDB-style, restart-based prefix-compressed block of sorted key–value
 *      entries. The builder emits a compact binary layout; the reader supports
 *      efficient point lookups (binary search over restart points, then a short scan).
 *
 *  - **IndexBlockBuilder / IndexBlockReader**
 *      A compact index that maps *divider keys* to `BlockHandle`s for data blocks.
 *      Each entry stores the divider key (varint length + bytes) followed by a
 *      fixed-size `BlockHandle`. A small offset table at the end enables O(log N)
 *      binary search by key without scanning the whole block.
 *
 * Encoding (Data Block)
 * ---------------------
 *  For each entry (key_i, value_i), we encode:
 *      [shared: u32][non_shared: u32][value_len: u32][key_delta bytes][value bytes]
 *  where:
 *      - `shared`     = shared prefix length with the previous key in the **same** restart run
 *      - `non_shared` = remaining key bytes after the shared prefix (key delta length)
 *      - `value_len`  = length of the value in bytes
 *
 *  A *restart point* starts a new run and always has `shared = 0` (full key stored).
 *  After all entries, we append:
 *      [restart_offsets: u32 array][num_restarts: u32]
 *
 * Encoding (Index Block)
 * ----------------------
 *  Repeated entries:
 *      [key_len: varint32][key bytes][BlockHandle(16 bytes)]
 *  followed by:
 *      [entry_offsets: u32 array][num_entries: u32]
 *
 * Invariants
 * ----------
 *  - Keys added to a DataBlockBuilder or IndexBlockBuilder must be **strictly increasing**
 *    (lexicographic). Violations are treated as programmer errors (implementations throw).
 *  - Blocks are immutable after `Finish()`.
 *  - Readers expect well-formed inputs; malformed blocks cause exceptions at parse time.
 *
 * Notes
 * -----
 *  - These classes are internal helpers (kept under `src/`) and are not part of the
 *    public API. Test targets may include this header to exercise the implementations.
 *  - All multi-byte integers are encoded little-endian.
 */

#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "VrootKV/io/sstable_format.h"

namespace VrootKV::io {

/**
 * @class DataBlockBuilder
 * @brief
 *     Builds a restart-based prefix-compressed **data block** of sorted key–value pairs.
 *
 * Usage pattern:
 *  1. Construct with an optional restart interval (default 16).
 *  2. Repeatedly call `Add()` with **strictly increasing** keys.
 *  3. Call `Finish()` to obtain the serialized block bytes.
 *
 * Complexity:
 *  - `Add()` amortized O(key length + value length).
 *  - Memory grows with the encoded block size and restart table.
 */
class DataBlockBuilder {
public:
    /**
     * @brief Create a builder.
     * @param restart_interval Number of entries between forced restart points.
     *        Larger values improve compression; smaller values reduce lookup work.
     *        Must be >= 1. (Validated in the implementation.)
     */
    explicit DataBlockBuilder(int restart_interval = 16);

    /**
     * @brief Append a sorted key–value pair to the block.
     * @param key   Next key (must be strictly greater than the previous key).
     * @param value Arbitrary byte payload for the key.
     *
     * The builder computes the shared prefix relative to the previous key in the same
     * restart run, encodes the key delta and value, and updates internal state.
     * @throws std::runtime_error if keys are not strictly increasing or builder was finished.
     */
    void Add(const std::string& key, const std::string& value);

    /**
     * @brief Finalize and return the serialized block.
     * @return A byte string containing:
     *         [entries...][restart_offsets...][num_restarts]
     *
     * The builder becomes immutable after `Finish()`; subsequent calls return the same
     * buffer. The returned memory is owned by the builder (copy it if you need to keep it).
     */
    std::string Finish();

    /**
     * @brief Estimate the current encoded size if the block were finalized now.
     * @return Size in bytes.
     */
    size_t CurrentSize() const;

private:
    std::string buffer_;              ///< Accumulated encoded entries and (later) restart table.
    std::vector<uint32_t> restarts_;  ///< Byte offsets of restart points within `buffer_`.
    std::string last_key_;            ///< Last full key added (for prefix-sharing).
    int restart_interval_;            ///< Entries per restart run.
    int counter_;                     ///< Entries since last restart.
    bool finished_;                   ///< Whether `Finish()` has been called.
};

/**
 * @class DataBlockReader
 * @brief
 *     Parses a serialized data block and supports **point lookups** (`Get(key)`).
 *
 * Lookup strategy:
 *  - Binary search over the restart table to find the last restart whose key <= target.
 *  - Linearly scan **within that restart run** reconstructing full keys until we pass
 *    the target or find a match.
 */
class DataBlockReader {
public:
    /**
     * @brief Construct a reader for a serialized data block.
     * @param block The entire block bytes including the restart table/trailer.
     * @throws std::runtime_error on structural corruption (e.g., truncated trailer).
     */
    explicit DataBlockReader(std::string_view block);

    /**
     * @brief Point query by exact key.
     * @param key       The key to search for.
     * @param value_out On success, receives the associated value bytes.
     * @return `true` if found; `false` if the key is not present in this block.
     * @throws std::runtime_error if the block encoding is malformed.
     */
    bool Get(std::string_view key, std::string& value_out) const;

private:
    std::string_view full_;           ///< Entire block bytes.
    std::string_view entries_;        ///< Entries region (excludes restart table & count).
    std::vector<uint32_t> restarts_;  ///< Parsed restart offsets.
};

/**
 * @class IndexBlockBuilder
 * @brief
 *     Builds a compact **index block** mapping divider keys to `BlockHandle`s.
 *
 * Each call to `Add(divider_key, handle)` appends an entry:
 *     [key_len: varint32][key bytes][BlockHandle(16 bytes)]
 * At `Finish()`, we append:
 *     [entry_offsets: u32 array][num_entries: u32]
 *
 * The divider key is typically the smallest key of the corresponding data block.
 * Keys must be strictly increasing.
 */
class IndexBlockBuilder {
public:
    /**
     * @brief Append a mapping from `divider_key` to a data block handle.
     * @param divider_key Smallest key contained in the referenced data block.
     * @param handle      Location/size of that data block.
     * @throws std::runtime_error if keys are not strictly increasing.
     */
    void Add(const std::string& divider_key, const BlockHandle& handle);

    /**
     * @brief Finalize and return the serialized index block.
     * @return A byte string containing:
     *         [entries...][entry_offsets...][num_entries]
     */
    std::string Finish();

private:
    std::string buffer_;              ///< Encoded entries.
    std::vector<uint32_t> offsets_;   ///< Byte offsets of each entry within `buffer_`.
    std::string last_key_;            ///< Last divider key appended (enforces sorting).
};

/**
 * @class IndexBlockReader
 * @brief
 *     Parses a serialized index block and supports fast handle lookup by key.
 *
 * `Find(k)` returns the handle for the rightmost divider key `<= k`. If all divider
 * keys are greater than `k`, `Find` returns false, signaling the caller that no
 * data block in this file can contain `k` (typical only for “before-first” cases).
 */
class IndexBlockReader {
public:
    /**
     * @brief Construct a reader for a serialized index block.
     * @param block The entire index block bytes including the trailing offsets.
     * @throws std::runtime_error on structural corruption (e.g., bad trailer).
     */
    explicit IndexBlockReader(std::string_view block);

    /**
     * @brief Locate the data block whose divider key is the last one `<= search_key`.
     * @param search_key   Key to route.
     * @param handle_out   On success, receives the corresponding `BlockHandle`.
     * @return `true` if a suitable handle was found; `false` if `search_key` is smaller
     *         than the first divider (i.e., would not belong to any indexed block here).
     * @throws std::runtime_error if the index encoding is malformed.
     */
    bool Find(std::string_view search_key, BlockHandle& handle_out) const;

private:
    std::string_view full_;           ///< Entire index block.
    std::string_view entries_;        ///< Entries region (before offset table).
    std::vector<uint32_t> offsets_;   ///< Parsed entry offsets for binary search.
    uint32_t num_{0};                 ///< Entry count.
};

} // namespace VrootKV::io

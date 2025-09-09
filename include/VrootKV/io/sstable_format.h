/**
 * @file sstable_format.h
 * @author Vrutik Halani
 * @brief
 *     Public definitions for the **on-disk SSTable format**: block pointers and
 *     the fixed-size file footer.
 *
 * Overview
 * --------
 * This header defines two POD-style structs that describe how readers and
 * writers locate content inside an SSTable file:
 *
 *   1) `BlockHandle` — a compact pointer to a contiguous block within the file,
 *      stored as an absolute file offset and byte size.
 *
 *   2) `SSTableFooter` — a fixed-size structure written at the very end of the
 *      file that stores the `BlockHandle` for the Filter Block and Index Block,
 *      plus a magic number for quick file-type validation.
 *
 * Encoding
 * --------
 * All fixed-width integers are encoded in **little-endian** order to disk.
 * The serialized layouts are:
 *
 *   BlockHandle (16 bytes):
 *     [offset: uint64_le][size: uint64_le]
 *
 *   SSTableFooter (40 bytes):
 *     [filter_handle: BlockHandle][index_handle: BlockHandle][magic: uint64_le]
 *
 * Notes
 * -----
 * - These structures are intentionally trivial (POD) and are serialized by
 *   copying their fixed-size fields into a `std::string` buffer.
 * - The code assumes a little-endian host. If porting to a big-endian system,
 *   introduce explicit byte-order conversion helpers before serialization.
 * - `SSTableFooter` is located by **seeking to the final 40 bytes** of the file.
 *   Reading it allows a single I/O to discover where the Filter and Index reside.
 *
 * Example (writer)
 * ----------------
 * @code
 *   using namespace VrootKV::io;
 *
 *   // After writing all data and index/filter blocks...
 *   SSTableFooter f;
 *   f.filter_handle = BlockHandle{filter_off, filter_size};
 *   f.index_handle  = BlockHandle{index_off, index_size};
 *
 *   std::string tail;
 *   f.EncodeTo(tail);
 *   file.write(tail.data(), tail.size());   // append at end of file
 * @endcode
 *
 * Example (reader)
 * ----------------
 * @code
 *   using namespace VrootKV::io;
 *
 *   // Seek to last 40 bytes of file and read them into `buf`
 *   std::string_view sv(buf.data(), buf.size());
 *   SSTableFooter f = SSTableFooter::DecodeFrom(sv); // consumes the 40 bytes
 *
 *   // Now locate the index block
 *   file.seek(f.index_handle.offset);
 *   read_exact(index_buf, f.index_handle.size);
 * @endcode
 */

#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <stdexcept>

namespace VrootKV::io {

/**
 * @struct BlockHandle
 * @brief
 *     A compact pointer to a block within an SSTable file.
 *
 * Semantics
 * ---------
 * - `offset` is the absolute byte offset from the beginning of the file.
 * - `size`   is the exact number of bytes that make up the block.
 *
 * Stability / Compatibility
 * -------------------------
 * The on-disk encoding is always **16 bytes**:
 *   `[offset(8) little-endian][size(8) little-endian]`.
 * This ensures readers can parse block locations without ambiguity.
 */
struct BlockHandle {
    /// Byte offset to the block within the file (absolute).
    uint64_t offset = 0;

    /// Size of the block in bytes.
    uint64_t size   = 0;

    /// Number of bytes in the serialized form of a BlockHandle.
    static constexpr size_t kEncodedLength = 16; // 8 + 8

    /**
     * @brief
     *     Append the BlockHandle to a byte buffer in little-endian format.
     *
     * @param dst
     *     A mutable byte buffer. On return, `dst` grows by 16 bytes.
     *
     * @post
     *     The buffer is appended with `[offset_le][size_le]`.
     */
    void EncodeTo(std::string& dst) const {
        char buf[16];
        // NOTE: We memcpy raw little-endian integers. On big-endian platforms,
        // introduce explicit byte swapping to preserve the on-disk little-endian format.
        std::memcpy(buf + 0, &offset, 8);
        std::memcpy(buf + 8, &size,   8);
        dst.append(buf, 16);
    }

    /**
     * @brief
     *     Decode a BlockHandle from the beginning of `in`, then advance `in`
     *     by `kEncodedLength` bytes.
     *
     * @param in
     *     A string_view referencing a buffer whose first 16 bytes contain a serialized
     *     BlockHandle. On success, `in.remove_prefix(16)` is performed.
     *
     * @return Parsed `BlockHandle`.
     *
     * @throws std::runtime_error
     *     If `in` has fewer than 16 bytes.
     */
    static BlockHandle DecodeFrom(std::string_view& in) {
        if (in.size() < kEncodedLength) {
            throw std::runtime_error("BlockHandle: truncated buffer");
        }
        BlockHandle h;
        // See note in EncodeTo regarding endianness expectations.
        std::memcpy(&h.offset, in.data() + 0, 8);
        std::memcpy(&h.size,   in.data() + 8, 8);
        in.remove_prefix(kEncodedLength);
        return h;
    }
};

/**
 * @struct SSTableFooter
 * @brief
 *     Fixed-size footer written at the end of the SSTable file.
 *
 * Contents
 * --------
 * - `filter_handle` : `BlockHandle` to the optional Filter Block.
 * - `index_handle`  : `BlockHandle` to the (required) Index Block.
 * - `magic`         : 64-bit identifier used to validate file type / version.
 *
 * Layout (40 bytes)
 * -----------------
 * `[filter_handle(16)][index_handle(16)][magic(8)]`
 *
 * Usage
 * -----
 * Readers locate the footer by reading the last `kEncodedLength` bytes of the file in
 * a single seek+read, then decode the handles to find the index (and filter) blocks.
 *
 * Versioning
 * ----------
 * The `magic` number (`0xF00DBAADF00DBAAD`) is **deliberately distinctive**.
 * If the format evolves, consider changing this field or extending the footer
 * in a backward-compatible way (e.g., larger trailing region with a new magic).
 */
struct SSTableFooter {
    /// Handle to the optional filter block. May be {0,0} if no filter is present.
    BlockHandle filter_handle;

    /// Handle to the index block. Must be a valid block.
    BlockHandle index_handle;

    /// File-type / version identifier to quickly sanity-check reads.
    uint64_t magic = 0xF00DBAADF00DBAADull;

    /// Number of bytes in the serialized form of an `SSTableFooter`.
    static constexpr size_t kEncodedLength = 16 + 16 + 8; // 40

    /**
     * @brief
     *     Append a serialized footer to the provided byte buffer.
     *
     * @param dst
     *     A mutable byte buffer to which the 40-byte footer is appended.
     *
     * @post
     *     `dst` grows by exactly `kEncodedLength` bytes, containing:
     *     `[filter_handle][index_handle][magic]` (all little-endian).
     */
    void EncodeTo(std::string& dst) const {
        filter_handle.EncodeTo(dst);
        index_handle.EncodeTo(dst);
        char m[8];
        std::memcpy(m, &magic, 8);
        dst.append(m, 8);
    }

    /**
     * @brief
     *     Decode an `SSTableFooter` from the start of `in`, consuming
     *     exactly `kEncodedLength` bytes on success.
     *
     * @param in
     *     A string_view referencing a buffer whose first 40 bytes contain a serialized
     *     footer. On success, `in.remove_prefix(kEncodedLength)` is performed.
     *
     * @return Parsed `SSTableFooter`.
     *
     * @throws std::runtime_error
     *     If fewer than 40 bytes are available in `in`, or if the magic field
     *     appears truncated.
     *
     * @note
     *     This function **consumes** the bytes from `in` (advances the view).
     *     If you need to decode without consuming, pass a copy of the view.
     */
    static SSTableFooter DecodeFrom(std::string_view& in) {
        if (in.size() < kEncodedLength) {
            throw std::runtime_error("SSTableFooter: truncated buffer");
        }

        SSTableFooter f;

        // Restrict parsing to the next 40 bytes while leaving the caller's view intact
        // until we explicitly remove the prefix at the end.
        std::string_view tmp = in.substr(0, kEncodedLength);

        // Decode the two BlockHandles in sequence. Each call advances `tmp` by 16 bytes.
        f.filter_handle = BlockHandle::DecodeFrom(tmp); // consumes 16
        f.index_handle  = BlockHandle::DecodeFrom(tmp); // consumes 16 (total 32)

        // The remaining 8 bytes are the magic.
        if (tmp.size() < 8) {
            throw std::runtime_error("SSTableFooter: missing magic");
        }
        std::memcpy(&f.magic, tmp.data(), 8);

        // Consumption: advance the caller's view by the fixed footer size.
        in.remove_prefix(kEncodedLength);

        return f;
    }
};

} // namespace VrootKV::io

/**
 * @file sstable_format.h
 * @author Vrutik Halani
 * @brief Defines the on-disk format of SSTable files.
 *
 * This file contains the definitions for the structures that represent the
 * on-disk format of SSTable files. This includes the BlockHandle, which is a
 * pointer to a block within the file, and the SSTableFooter, which is a
 * fixed-size structure at the end of the file that contains pointers to the
 * filter and index blocks.
 */

#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <stdexcept>

namespace VrootKV::io {

// BlockHandle: absolute file offset and byte size for a block inside an SSTable.
// Binary encoding: little-endian [offset(8)][size(8)].
struct BlockHandle {
    uint64_t offset = 0;
    uint64_t size   = 0;

    static constexpr size_t kEncodedLength = 16; // 8 + 8

    void EncodeTo(std::string& dst) const {
        char buf[16];
        std::memcpy(buf + 0, &offset, 8);
        std::memcpy(buf + 8, &size,   8);
        dst.append(buf, 16);
    }

    static BlockHandle DecodeFrom(std::string_view& in) {
        if (in.size() < kEncodedLength) {
            throw std::runtime_error("BlockHandle: truncated buffer");
        }
        BlockHandle h;
        std::memcpy(&h.offset, in.data() + 0, 8);
        std::memcpy(&h.size,   in.data() + 8, 8);
        in.remove_prefix(kEncodedLength);
        return h;
    }
};

// Fixed-size SSTable footer at file end, so readers can seek once to load it.
// Layout: [filter_handle(16)][index_handle(16)][magic(8)]  ==> 40 bytes total.
// This matches the guide’s “Footer points to Filter and Index blocks.”
struct SSTableFooter {
    BlockHandle filter_handle;
    BlockHandle index_handle;
    uint64_t magic = 0xF00DBAADF00DBAADull;  // distinctive constant / version tag

    static constexpr size_t kEncodedLength = 16 + 16 + 8; // 40

    void EncodeTo(std::string& dst) const {
        filter_handle.EncodeTo(dst);
        index_handle.EncodeTo(dst);
        char m[8];
        std::memcpy(m, &magic, 8);
        dst.append(m, 8);
    }

    static SSTableFooter DecodeFrom(std::string_view& in) {
        if (in.size() < kEncodedLength) {
            throw std::runtime_error("SSTableFooter: truncated buffer");
        }
        SSTableFooter f;
        std::string_view tmp = in.substr(0, kEncodedLength);

        f.filter_handle = BlockHandle::DecodeFrom(tmp); // consumes 16
        f.index_handle  = BlockHandle::DecodeFrom(tmp); // consumes 16 (total 32)
        if (tmp.size() < 8) {
            throw std::runtime_error("SSTableFooter: missing magic");
        }
        std::memcpy(&f.magic, tmp.data(), 8);          // ✅ read the next 8 bytes

        // If you want this function to CONSUME from `in`, uncomment the next line:
        in.remove_prefix(kEncodedLength);

        return f;
    }

};

} // namespace VrootKV::io
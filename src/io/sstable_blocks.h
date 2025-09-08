#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "VrootKV/io/sstable_format.h"

namespace VrootKV::io {

// Data block = restart-based prefix-compressed KV entries.
class DataBlockBuilder {
public:
    explicit DataBlockBuilder(int restart_interval = 16);
    void Add(const std::string& key, const std::string& value);  // keys strictly increasing
    std::string Finish();                                        // serialized block
    size_t CurrentSize() const;

private:
    std::string buffer_;
    std::vector<uint32_t> restarts_;
    std::string last_key_;
    int restart_interval_;
    int counter_;
    bool finished_;
};

// Parses a data block produced by DataBlockBuilder; supports Get(key).
class DataBlockReader {
public:
    explicit DataBlockReader(std::string_view block);
    bool Get(std::string_view key, std::string& value_out) const;

private:
    std::string_view full_;
    std::string_view entries_;
    std::vector<uint32_t> restarts_;
};

// Index block for mapping divider keys -> BlockHandle
class IndexBlockBuilder {
public:
    void Add(const std::string& divider_key, const BlockHandle& handle); // keys strictly increasing
    std::string Finish();

private:
    std::string buffer_;
    std::vector<uint32_t> offsets_;
    std::string last_key_;
};

class IndexBlockReader {
public:
    explicit IndexBlockReader(std::string_view block);
    // Finds the rightmost divider key <= search_key; returns its handle.
    bool Find(std::string_view search_key, BlockHandle& handle_out) const;

private:
    std::string_view full_;
    std::string_view entries_;
    std::vector<uint32_t> offsets_;
    uint32_t num_{0};
};

} // namespace VrootKV::io

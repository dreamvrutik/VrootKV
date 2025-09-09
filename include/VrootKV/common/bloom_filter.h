#pragma once
/**
 * @file bloom_filter.h
 * @author Vrutik Halani
 * @brief Public BloomFilter class for fast probabilistic membership tests.
 *
 * This Bloom filter is a compact, dependency-free implementation designed for the
 * storage engine’s read path. It supports:
 *  - add(key): insert a key into the filter
 *  - might_contain(key): test if a key may be present (never false negative, may be false positive)
 *  - serialize()/Deserialize(): portable serialization to/from a byte buffer
 *
 * Design notes:
 *  - Size (bits) and number of hash functions are chosen via standard formulas given
 *    expected_items (n) and target false_positive_rate (p):
 *      m = ceil( -n * ln(p) / (ln(2)^2) ),  k = round( (m/n) * ln(2) )
 *  - Double hashing (Kirsch–Mitzenmacher) derives k positions from two 64-bit hashes:
 *      h_i = (h1 + i * h2) mod m
 *  - Serialization is little-endian and includes a magic/version header for safety.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace VrootKV::common {

/**
 * @class BloomFilter
 * @brief Compact bitset + multi-hash membership structure with serialization.
 *
 * Usage:
 *   BloomFilter bf(expected_items, false_positive_rate);
 *   bf.add("key");
 *   if (bf.might_contain("key")) {  probably present  }
 *   std::string bytes = bf.serialize();
 *   BloomFilter bf2 = BloomFilter::Deserialize(bytes);
 */
class BloomFilter {
public:
    /**
     * @brief Construct a filter sized to meet a target false positive probability.
     * @param expected_items Estimated number of inserted keys (n).
     * @param false_positive_rate Target false positive probability (0 < p < 1).
     *
     * The bit array size (m) and number of hash functions (k) are computed from n and p.
     * A small n or extreme p is clamped to safe minimums.
     */
    BloomFilter(std::size_t expected_items, double false_positive_rate);

    /**
     * @brief Deserialize a filter from a byte buffer previously produced by serialize().
     * @param bytes Binary buffer view (must exactly match serialized format).
     * @return A reconstructed BloomFilter.
     * @throws std::runtime_error on malformed header or size mismatch.
     */
    static BloomFilter Deserialize(std::string_view bytes);

    /**
     * @brief Insert a key into the filter.
     * @param key Arbitrary byte string; treated as opaque.
     *
     * Sets k bit positions derived from double hashing of the key.
     */
    void add(std::string_view key);

    /**
     * @brief Test membership with no false negatives.
     * @param key Arbitrary byte string; treated as opaque.
     * @return false if definitely not present; true if possibly present (may be false positive).
     *
     * Checks that all k bit positions derived from the key are set.
     */
    bool might_contain(std::string_view key) const;

    /**
     * @brief Serialize to a portable byte buffer (little-endian).
     * @return Buffer in the format:
     *   [magic: u32="VKBF"][version: u32=1][num_bits: u64][k: u32][pad: u32=0][bit-bytes...]
     */
    std::string serialize() const;

    // -------- Introspection (for tests / diagnostics) --------
    std::size_t bit_size() const { return num_bits_; }
    std::size_t byte_size() const { return (num_bits_ + 7u) / 8u; }
    std::uint32_t num_hashes() const { return num_hashes_; }

private:
    // Internal constructor used by Deserialize.
    BloomFilter(std::size_t num_bits, std::uint32_t k, std::vector<std::uint8_t>&& bytes);

    // Internal helpers (implemented in .cpp)
    static std::size_t OptimalNumBits(std::size_t n, double p);
    static std::uint32_t OptimalNumHashes(std::size_t n, std::size_t m);
    void set_bit(std::size_t bit_index);
    bool get_bit(std::size_t bit_index) const;
    void positions(std::string_view key, std::size_t* out, std::uint32_t k) const;

    // State
    std::size_t num_bits_{0};            ///< Total number of bits (m).
    std::uint32_t num_hashes_{0};        ///< Number of hash functions (k).
    std::vector<std::uint8_t> bits_;     ///< Bit array (packed in bytes, LSB-first per byte).
};

} // namespace VrootKV::common

/**
 * @file bloom_filter.cpp
 * @author Vrutik Halani
 * @brief Implementation of a space-efficient Bloom filter with portable serialization.
 *
 * Implementation highlights:
 *  - Portable little-endian encoding helpers for header fields.
 *  - Fast 64-bit hashing (SplitMix64-style mixer) used for double hashing.
 *  - No external dependencies; suitable for embedding as the SSTable Filter Block
 */

#include "VrootKV/common/bloom_filter.h"

#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace VrootKV::common {

// ====================== Portable little-endian helpers ======================
namespace detail {

inline void PutU32(std::string& dst, std::uint32_t v) {
    unsigned char b[4] = {
        static_cast<unsigned char>(v & 0xFFu),
        static_cast<unsigned char>((v >> 8) & 0xFFu),
        static_cast<unsigned char>((v >> 16) & 0xFFu),
        static_cast<unsigned char>((v >> 24) & 0xFFu)
    };
    dst.append(reinterpret_cast<const char*>(b), 4);
}

inline void PutU64(std::string& dst, std::uint64_t v) {
    unsigned char b[8] = {
        static_cast<unsigned char>(v & 0xFFull),
        static_cast<unsigned char>((v >> 8) & 0xFFull),
        static_cast<unsigned char>((v >> 16) & 0xFFull),
        static_cast<unsigned char>((v >> 24) & 0xFFull),
        static_cast<unsigned char>((v >> 32) & 0xFFull),
        static_cast<unsigned char>((v >> 40) & 0xFFull),
        static_cast<unsigned char>((v >> 48) & 0xFFull),
        static_cast<unsigned char>((v >> 56) & 0xFFull)
    };
    dst.append(reinterpret_cast<const char*>(b), 8);
}

inline std::uint32_t GetU32(const char* p) {
    return  (static_cast<std::uint32_t>(static_cast<unsigned char>(p[0]))       ) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(p[1])) <<  8) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(p[2])) << 16) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(p[3])) << 24);
}

inline std::uint64_t GetU64(const char* p) {
    return  (static_cast<std::uint64_t>(static_cast<unsigned char>(p[0]))       ) |
            (static_cast<std::uint64_t>(static_cast<unsigned char>(p[1])) <<  8) |
            (static_cast<std::uint64_t>(static_cast<unsigned char>(p[2])) << 16) |
            (static_cast<std::uint64_t>(static_cast<unsigned char>(p[3])) << 24) |
            (static_cast<std::uint64_t>(static_cast<unsigned char>(p[4])) << 32) |
            (static_cast<std::uint64_t>(static_cast<unsigned char>(p[5])) << 40) |
            (static_cast<std::uint64_t>(static_cast<unsigned char>(p[6])) << 48) |
            (static_cast<std::uint64_t>(static_cast<unsigned char>(p[7])) << 56);
}

/**
 * @brief Fast 64-bit hash (SplitMix64-style mixing) for arbitrary byte strings.
 * @param s Input bytes
 * @param seed Per-hash seed to decorrelate h1 and h2
 * @return 64-bit hash
 *
 * We mix 8-byte chunks, then a tail. This is not cryptographic; it is fast and
 * produces well-distributed bits for Bloom filters.
 */
inline std::uint64_t Hash64(std::string_view s, std::uint64_t seed) {
    std::uint64_t x = seed ^ (0x9E3779B97F4A7C15ull + s.size());
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s.data());
    std::size_t i = 0;

    // Mix 8-byte chunks
    while (i + 8 <= s.size()) {
        std::uint64_t k;
        std::memcpy(&k, p + i, 8);
        x += k + 0x9E3779B97F4A7C15ull;
        x ^= (x >> 30);
        x *= 0xBF58476D1CE4E5B9ull;
        x ^= (x >> 27);
        x *= 0x94D049BB133111EBull;
        i += 8;
    }

    // Handle tail bytes
    std::uint64_t tail = 0;
    int shift = 0;
    while (i < s.size()) {
        tail |= (static_cast<std::uint64_t>(p[i]) << shift);
        shift += 8;
        ++i;
    }
    x += tail;

    // Final mix
    x ^= (x >> 30);
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= (x >> 27);
    x *= 0x94D049BB133111EBull;
    x ^= (x >> 31);
    return x;
}

} // namespace detail

// =============================== Sizing =====================================

/**
 * @brief Compute optimal bit count m for expected inserts n and target FPR p.
 * @param n Expected number of items.
 * @param p Target false positive probability (clamped to sensible range).
 * @return m in bits (>= 1).
 *
 * Formula: m = -n * ln(p) / (ln(2)^2)
 */
std::size_t BloomFilter::OptimalNumBits(std::size_t n, double p) {
    if (n == 0) return 1u;
    if (p <= 0.0) p = 1e-9;
    if (p >= 1.0) p = 0.999999;
    const double m = -static_cast<double>(n) * std::log(p) / (std::log(2.0) * std::log(2.0));
    std::size_t mm = static_cast<std::size_t>(std::ceil(m));
    if (mm == 0) mm = 1;
    return mm;
}

/**
 * @brief Compute optimal number of hashes k for given n and m.
 * @param n Expected number of items.
 * @param m Total bit count.
 * @return k (>= 1), rounded to nearest integer.
 *
 * Formula: k = (m/n) * ln(2)
 */
std::uint32_t BloomFilter::OptimalNumHashes(std::size_t n, std::size_t m) {
    if (n == 0 || m == 0) return 1u;
    const double k = (static_cast<double>(m) / static_cast<double>(n)) * std::log(2.0);
    std::uint32_t kk = static_cast<std::uint32_t>(std::round(k));
    if (kk == 0) kk = 1;
    return kk;
}

// ============================== Construction ================================

/**
 * @brief Public constructor: sizes filter to meet target p for n inserts.
 */
BloomFilter::BloomFilter(std::size_t expected_items, double false_positive_rate) {
    num_bits_ = OptimalNumBits(expected_items, false_positive_rate);
    num_hashes_ = OptimalNumHashes(expected_items, num_bits_);
    bits_.assign((num_bits_ + 7u) / 8u, 0);
}

/**
 * @brief Internal constructor used by Deserialize().
 */
BloomFilter::BloomFilter(std::size_t num_bits, std::uint32_t k, std::vector<std::uint8_t>&& bytes)
    : num_bits_(num_bits), num_hashes_(k), bits_(std::move(bytes)) {}

// ================================ Bit I/O ===================================

/**
 * @brief Set the bit at bit_index in the underlying array.
 */
void BloomFilter::set_bit(std::size_t bit_index) {
    const std::size_t byte = bit_index >> 3;
    const std::uint8_t mask = static_cast<std::uint8_t>(1u << (bit_index & 7u));
    bits_[byte] |= mask;
}

/**
 * @brief Read the bit at bit_index in the underlying array.
 */
bool BloomFilter::get_bit(std::size_t bit_index) const {
    const std::size_t byte = bit_index >> 3;
    const std::uint8_t mask = static_cast<std::uint8_t>(1u << (bit_index & 7u));
    return (bits_[byte] & mask) != 0;
}

// ============================== Hash positions ==============================

/**
 * @brief Compute k bit positions for key using double hashing.
 * @param key Arbitrary bytes to hash.
 * @param out Caller-provided array of size >= k to receive positions.
 * @param k   Number of positions to generate.
 *
 * We compute two independent 64-bit hashes and produce:
 *   pos_i = (h1 + i * step) % m
 * where step is derived from h2 and forced odd to ensure full residue coverage
 * even if h2 has a poor lower bit distribution.
 */
void BloomFilter::positions(std::string_view key, std::size_t* out, std::uint32_t k) const {
    const std::uint64_t h1 = detail::Hash64(key, 0x243F6A8885A308D3ull);
    const std::uint64_t h2 = detail::Hash64(key, 0x13198A2E03707344ull);
    const std::uint64_t m  = static_cast<std::uint64_t>(num_bits_);

    // Make step odd; avoid step==0 corner cases.
    std::uint64_t step = (h2 << 1u) | 1u;
    std::uint64_t x = (m == 0) ? 0 : (h1 % m);

    for (std::uint32_t i = 0; i < k; ++i) {
        out[i] = static_cast<std::size_t>(x);
        if (m) {
            x += step;
            x %= m;
        }
    }
}

// ============================== Public API ==================================

/**
 * @brief Insert a key by setting k derived bit positions.
 */
void BloomFilter::add(std::string_view key) {
    if (num_bits_ == 0) return;

    // Use stack buffer for typical small k; spill to vector if needed.
    std::size_t idxs_stack[64];
    std::size_t* idxs = idxs_stack;
    std::vector<std::size_t> dyn;
    if (num_hashes_ > 64) {
        dyn.resize(num_hashes_);
        idxs = dyn.data();
    }

    positions(key, idxs, num_hashes_);
    for (std::uint32_t i = 0; i < num_hashes_; ++i) {
        set_bit(idxs[i]);
    }
}

/**
 * @brief Membership test with no false negatives.
 * @return false if definitely absent; true if possibly present.
 */
bool BloomFilter::might_contain(std::string_view key) const {
    if (num_bits_ == 0) return false;

    std::size_t idxs_stack[64];
    std::size_t* idxs = idxs_stack;
    std::vector<std::size_t> dyn;
    if (num_hashes_ > 64) {
        dyn.resize(num_hashes_);
        idxs = dyn.data();
    }

    positions(key, idxs, num_hashes_);
    for (std::uint32_t i = 0; i < num_hashes_; ++i) {
        if (!get_bit(idxs[i])) return false;
    }
    return true;
}

/**
 * @brief Serialize to a byte buffer with a small header for safety/versioning.
 */
std::string BloomFilter::serialize() const {
    // Format:
    // [magic: u32 'VKBF'][version: u32=1][num_bits: u64][k: u32][pad: u32=0][bits...]
    std::string out;
    out.reserve(24 + bits_.size());

    const std::uint32_t kMagic   = 0x46424B56u; // 'V''K''B''F' in little-endian
    const std::uint32_t kVersion = 1u;

    detail::PutU32(out, kMagic);
    detail::PutU32(out, kVersion);
    detail::PutU64(out, static_cast<std::uint64_t>(num_bits_));
    detail::PutU32(out, static_cast<std::uint32_t>(num_hashes_));
    detail::PutU32(out, 0u); // pad for future-proofing/alignment

    out.append(reinterpret_cast<const char*>(bits_.data()),
                static_cast<std::ptrdiff_t>(bits_.size()));

    return out;
}

/**
 * @brief Deserialize a filter from serialize() output.
 * @throws std::runtime_error on corruption, bad magic/version, or size mismatch.
 */
BloomFilter BloomFilter::Deserialize(std::string_view bytes) {
    if (bytes.size() < 24) {
        throw std::runtime_error("BloomFilter: truncated header");
    }
    const char* p = bytes.data();

    const std::uint32_t magic   = detail::GetU32(p + 0);
    const std::uint32_t version = detail::GetU32(p + 4);
    const std::uint64_t m_bits  = detail::GetU64(p + 8);
    const std::uint32_t k       = detail::GetU32(p + 16);
    // p + 20 is a pad field (ignored)

    const std::uint32_t kMagic = 0x46424B56u;
    if (magic != kMagic || version != 1u) {
        throw std::runtime_error("BloomFilter: bad magic or version");
    }
    if (m_bits == 0 || k == 0) {
        throw std::runtime_error("BloomFilter: invalid parameters");
    }

    const std::size_t needed = static_cast<std::size_t>((m_bits + 7u) / 8u);
    if (bytes.size() != 24 + needed) {
        throw std::runtime_error("BloomFilter: size mismatch");
    }

    std::vector<std::uint8_t> buf(needed);
    std::memcpy(buf.data(), p + 24, needed);

    return BloomFilter(static_cast<std::size_t>(m_bits), k, std::move(buf));
}

} // namespace VrootKV::common

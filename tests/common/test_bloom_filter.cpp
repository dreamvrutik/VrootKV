/**
 * @file test_bloom_filter.cpp
 * @author Vrutik Halani
 * @brief Unit tests for BloomFilter:
 *   - No false negatives for inserted items.
 *   - False positive rate (FPR) is within a reasonable bound of the configured target.
 *
 * The Implementation Guide requires verifying lack of false negatives and
 * that FPR is within configured bounds
 */

#include <gtest/gtest.h>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

#include "VrootKV/common/bloom_filter.h"

using VrootKV::common::BloomFilter;

/**
 * @brief Smoke test: insert multiple keys, verify might_contain() returns true for all.
 * Also verifies serialization round-trip preserves structure and behavior.
 */
TEST(BloomFilter, NoFalseNegatives_And_SerializationRoundTrip) {
    const std::size_t N = 20000;
    const double fpp = 0.01;

    BloomFilter bf(N, fpp);

    // Stable RNG to get reproducible coverage and behavior.
    std::mt19937_64 rng(123456789);
    std::uniform_int_distribution<uint64_t> dist;

    std::vector<std::string> keys;
    keys.reserve(N);

    // Insert N random 8-byte keys.
    for (std::size_t i = 0; i < N; ++i) {
        uint64_t x = dist(rng);
        keys.emplace_back(reinterpret_cast<const char*>(&x), sizeof(x));
        bf.add(keys.back());
    }

    // No false negatives: all inserted keys should be possibly present.
    for (const auto& k : keys) {
        EXPECT_TRUE(bf.might_contain(k));
    }

    // Serialize and restore; properties must survive exactly.
    const std::string dump = bf.serialize();
    BloomFilter bf2 = BloomFilter::Deserialize(dump);

    // Still no false negatives after deserialization.
    for (const auto& k : keys) {
        EXPECT_TRUE(bf2.might_contain(k));
    }

    // Structural equality sanity checks.
    EXPECT_EQ(bf.bit_size(), bf2.bit_size());
    EXPECT_EQ(bf.num_hashes(), bf2.num_hashes());
    EXPECT_EQ(dump, bf2.serialize());
}

/**
 * @brief Statistical test: measure false positive rate on non-inserted keys.
 *
 * The measured FPR will vary due to randomness; we allow a modest slack factor
 * relative to the configured target (e.g., 1.8×) to keep the test robust across platforms.
 */
TEST(BloomFilter, FalsePositiveRateWithinConfiguredBound) {
    const std::size_t N = 20000;        // expected inserts
    const double target_fpp = 0.01;     // configuration target

    BloomFilter bf(N, target_fpp);

    // Insert exactly N distinct keys.
    std::mt19937_64 rng(987654321);
    std::uniform_int_distribution<uint64_t> dist;
    std::unordered_set<uint64_t> inserted;
    inserted.reserve(N * 2);

    for (std::size_t i = 0; i < N; ++i) {
        uint64_t x;
        do { x = dist(rng); } while (!inserted.insert(x).second);
        bf.add(std::string(reinterpret_cast<const char*>(&x), sizeof(x)));
    }

    // Probe M keys that are guaranteed not inserted to estimate FPR.
    const std::size_t M = 20000;
    std::size_t false_positives = 0;

    for (std::size_t i = 0; i < M; ++i) {
        uint64_t y;
        do { y = dist(rng); } while (inserted.count(y));
        if (bf.might_contain(std::string(reinterpret_cast<const char*>(&y), sizeof(y)))) {
            ++false_positives;
        }
    }

    const double measured = static_cast<double>(false_positives) / static_cast<double>(M);

    // Allow slack for variance. Tighten if you prefer, or base it on a binomial bound.
    EXPECT_LE(measured, target_fpp * 1.8)
        << "Measured FPR=" << measured << " exceeds acceptable bound.";
}

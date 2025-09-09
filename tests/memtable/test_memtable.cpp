/**
 * @file test_memtable.cpp
 * @author Vrutik Halani
 * @brief Unit tests for the single-threaded Skip List used by the Memtable.
 *
 * What these tests verify
 * -----------------------
 * • Basic operations: Insert, Put (upsert), Get, Contains, Erase
 * • Duplicate insertion rejection on Insert()
 * • Ordered forward iteration over all keys
 * • Seek(target) positions at the first key >= target
 */

#include <gtest/gtest.h>
#include <algorithm>
#include <random>
#include <string>
#include <vector>

#include "src/memtable/skip_list.h"

using VrootKV::memtable::SkipList;

TEST(SkipList, Empty_OnStart) {
    SkipList sl;
    EXPECT_TRUE(sl.empty());
    EXPECT_EQ(sl.size(), 0u);
    EXPECT_FALSE(sl.Contains("a"));
    std::string v;
    EXPECT_FALSE(sl.Get("a", v));
}

TEST(SkipList, Insert_And_Get_Ordered) {
    SkipList sl;

    // Insert keys in mixed order to ensure sorting is handled internally.
    std::vector<std::pair<std::string,std::string>> kv = {
        {"delta", "4"}, {"alpha", "1"}, {"charlie", "3"}, {"bravo", "2"},
        {"echo", "5"}, {"foxtrot", "6"}
    };
    for (auto& p : kv) {
        EXPECT_TRUE(sl.Insert(p.first, p.second));
    }
    EXPECT_EQ(sl.size(), kv.size());

    // Verify lookups
    for (auto& p : kv) {
        std::string out;
        EXPECT_TRUE(sl.Get(p.first, out));
        EXPECT_EQ(out, p.second);
        EXPECT_TRUE(sl.Contains(p.first));
    }

    // Verify non-existent keys
    std::string out;
    EXPECT_FALSE(sl.Get("zzz", out));
    EXPECT_FALSE(sl.Contains("zzz"));

    // Verify iteration order: alpha, bravo, charlie, delta, echo, foxtrot
    std::vector<std::string> keys;
    for (auto it = sl.Begin(); it.Valid(); it.Next()) {
        keys.push_back(it.key());
    }
    std::vector<std::string> expected = {"alpha","bravo","charlie","delta","echo","foxtrot"};
    EXPECT_EQ(keys, expected);
}

TEST(SkipList, Insert_Duplicate_Rejected) {
    SkipList sl;
    EXPECT_TRUE(sl.Insert("k", "1"));
    EXPECT_FALSE(sl.Insert("k", "2")); // duplicate should fail for Insert()
    std::string v;
    ASSERT_TRUE(sl.Get("k", v));
    EXPECT_EQ(v, "1");
}

TEST(SkipList, Put_Upsert_Overwrites) {
    SkipList sl;
    // First time: insert
    EXPECT_TRUE(sl.Put("x", "100"));
    // Second time: overwrite
    EXPECT_FALSE(sl.Put("x", "101"));

    std::string v;
    ASSERT_TRUE(sl.Get("x", v));
    EXPECT_EQ(v, "101");
    EXPECT_EQ(sl.size(), 1u);
}

TEST(SkipList, Erase_Basic) {
    SkipList sl;
    EXPECT_TRUE(sl.Insert("a", "1"));
    EXPECT_TRUE(sl.Insert("b", "2"));
    EXPECT_TRUE(sl.Insert("c", "3"));
    EXPECT_EQ(sl.size(), 3u);

    EXPECT_TRUE(sl.Erase("b"));
    EXPECT_FALSE(sl.Erase("b")); // already gone
    EXPECT_EQ(sl.size(), 2u);

    std::string v;
    EXPECT_FALSE(sl.Get("b", v));
    EXPECT_TRUE(sl.Get("a", v));
    EXPECT_EQ(v, "1");
    EXPECT_TRUE(sl.Get("c", v));
    EXPECT_EQ(v, "3");

    // Remaining iteration order: a, c
    std::vector<std::string> keys;
    for (auto it = sl.Begin(); it.Valid(); it.Next()) {
        keys.push_back(it.key());
    }
    std::vector<std::string> expected = {"a","c"};
    EXPECT_EQ(keys, expected);
}

TEST(SkipList, Seek_Behavior) {
    SkipList sl;
    for (auto&& k : {"a","c","e","g"}) {
        EXPECT_TRUE(sl.Insert(k, std::string(1, static_cast<char>(toupper(k[0])))));
    }
    // Seek to existing
    {
        auto it = sl.Seek("c");
        ASSERT_TRUE(it.Valid());
        EXPECT_EQ(it.key(), "c");
        EXPECT_EQ(it.value(), "C");
    }
    // Seek to in-between → first >= target
    {
        auto it = sl.Seek("d");
        ASSERT_TRUE(it.Valid());
        EXPECT_EQ(it.key(), "e");
    }
    // Seek beyond last → end iterator (invalid)
    {
        auto it = sl.Seek("z");
        EXPECT_FALSE(it.Valid());
    }
}

TEST(SkipList, Many_Inserts_Random_Order) {
    SkipList sl;
    // Generate 100 random keys "k###"
    std::vector<std::string> keys;
    for (int i = 0; i < 100; ++i) {
        keys.emplace_back("k" + std::to_string(i));
    }
    // Shuffle for random insertion order
    std::mt19937 rng(123);
    std::shuffle(keys.begin(), keys.end(), rng);

    for (const auto& k : keys) {
        EXPECT_TRUE(sl.Insert(k, "v" + k));
    }
    EXPECT_EQ(sl.size(), keys.size());

    // Verify sorted iteration matches lexicographic order "k0..k99"
    std::vector<std::string> iter_keys;
    for (auto it = sl.Begin(); it.Valid(); it.Next()) {
        iter_keys.push_back(it.key());
    }

    std::vector<std::string> expected = keys;
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(iter_keys, expected);

    // Spot check some values
    std::string v;
    EXPECT_TRUE(sl.Get("k0", v));  EXPECT_EQ(v, "vk0");
    EXPECT_TRUE(sl.Get("k50", v)); EXPECT_EQ(v, "vk50");
    EXPECT_TRUE(sl.Get("k99", v)); EXPECT_EQ(v, "vk99");
}

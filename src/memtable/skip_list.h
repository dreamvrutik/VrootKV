/**
 * @file skip_list.h
 * @author Vrutik Halani
 * @brief Single-threaded Skip List for the Memtable (sorted in-memory KV).
 *
 * Overview
 * --------
 * This header implements a **standard (single-threaded) Skip List** that stores
 * sorted key-value pairs (std::string -> std::string). It is intended to be used
 * by the Memtable module in Phase 1 (single-threaded) of the storage engine.
 * A later module will introduce a concurrent variant using atomics/CAS.
 *
 * Characteristics
 * ---------------
 * - Average O(log n) for search/insert/erase via probabilistic multi-level links.
 * - Keys are stored in **strictly increasing** lexicographic order.
 * - Supports:
 *      • Insert (fails if key exists)
 *      • Put/Upsert (insert or overwrite)
 *      • Get / Contains
 *      • Erase
 *      • Ordered iteration (forward only) and point Seek
 *
 * Design
 * ------
 * - A fixed MAX_LEVEL tower height and geometric level promotion with p = 1/4.
 * - A sentinel head node with MAX_LEVEL forward pointers.
 * - `findGreaterOrEqual()` collects per-level predecessors to splice nodes in/out.
 *
 * Threading
 * ---------
 * - **Not thread-safe.** Intended for single-threaded use (Module 1.3.2).
 *   The concurrent version (Module 3.1.1) will replace pointers with atomics.
 *
 * Memory
 * ------
 * - Nodes are heap-allocated and freed at destruction or `Clear()`.
 */

#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace VrootKV::memtable {

class SkipList {
private:
    // Forward declaration MUST appear before Iterator uses Node.
    struct Node;

public:
    // --------- Public types ---------
    using Key   = std::string;
    using Value = std::string;

    /**
     * @brief Read-only forward iterator over key/value pairs in sorted order.
     *
     * Usage:
     *   SkipList sl;
     *   for (SkipList::Iterator it = sl.Begin(); it.Valid(); it.Next()) {
     *       // use it.key(), it.value()
     *   }
     *
     * NOTE: Iterator is invalidated by structural modifications (insert/erase).
     */
    class Iterator {
    public:
        Iterator() = default;

        /** @brief True if the iterator points to a valid node (not end). */
        bool Valid() const noexcept { return node_ != nullptr; }

        /** @brief Advance to the next item (no-op if already end). */
        void Next() noexcept {
            if (node_) node_ = node_->next_[0];
        }

        /** @brief Access the current key. Precondition: Valid() == true. */
        const Key& key() const noexcept { return node_->key; }

        /** @brief Access the current value. Precondition: Valid() == true. */
        const Value& value() const noexcept { return node_->value; }

    private:
        friend class SkipList;
        // Only SkipList can construct it with an internal Node*.
        explicit Iterator(Node* n) : node_(n) {}
        Node* node_ = nullptr;
    };

    // --------- Construction / rule-of-five ---------

    /**
     * @brief Construct an empty SkipList.
     * @param max_level     Maximum height of towers; typical 12–20 is fine.
     * @param p_numerator   Probability numerator for level promotion (default 1).
     * @param p_denominator Probability denominator for level promotion (default 4 → p=1/4).
     */
    explicit SkipList(int max_level = 16, int p_numerator = 1, int p_denominator = 4)
            : max_level_(max_level),
              p_num_(p_numerator),
              p_den_(p_denominator),
              level_(1),
              size_(0),
              rng_(std::random_device{}()),
              dist_(0, p_denominator - 1) {
        if (max_level_ < 1) max_level_ = 1;
        if (p_den_ <= 1 || p_num_ < 1 || p_num_ >= p_den_) {
            // Fallback to 1/4 if caller passes pathological values.
            p_num_ = 1; p_den_ = 4;
        }
        head_ = new Node(max_level_);
        for (int i = 0; i < max_level_; ++i) head_->next_[i] = nullptr;
    }

    /** @brief Destroy the list and free all nodes. */
    ~SkipList() { Clear(); delete head_; }

    SkipList(const SkipList&) = delete;
    SkipList& operator=(const SkipList&) = delete;
    SkipList(SkipList&&) = delete;
    SkipList& operator=(SkipList&&) = delete;

    // --------- Basic queries ---------

    /** @brief Number of elements in the list. */
    std::size_t size() const noexcept { return size_; }

    /** @brief True if empty. */
    bool empty() const noexcept { return size_ == 0; }

    /** @brief Remove all entries and reset to empty. */
    void Clear() noexcept {
        // Delete level-0 chain.
        Node* cur = head_->next_[0];
        while (cur) {
            Node* nxt = cur->next_[0];
            delete cur;
            cur = nxt;
        }
        for (int i = 0; i < max_level_; ++i) head_->next_[i] = nullptr;
        level_ = 1;
        size_  = 0;
    }

    // --------- Lookup / access ---------

    /**
     * @brief Return true if key exists.
     * @details Walks top-down levels, then checks bottom neighbor for equality.
     */
    bool Contains(const Key& key) const noexcept {
        const Node* x = findGreaterOrEqual(key);
        return (x && x->key == key);
    }

    /**
     * @brief Get the value for key if present.
     * @return true on success; false if key missing.
     */
    bool Get(const Key& key, Value& out_value) const {
        const Node* x = findGreaterOrEqual(key);
        if (x && x->key == key) {
            out_value = x->value;
            return true;
        }
        return false;
    }

    // --------- Modifying operations ---------

    /**
     * @brief Insert (fails if key already exists).
     * @return true if inserted; false if duplicate key.
     */
    bool Insert(const Key& key, const Value& value) {
        std::vector<Node*> update(max_level_, nullptr);
        Node* x = findGreaterOrEqualMut(key, update);
        if (x && x->key == key) {
            return false;  // do not overwrite on Insert()
        }
        int lvl = randomLevel();
        if (lvl > level_) {
            for (int i = level_; i < lvl; ++i) update[i] = head_;
            level_ = lvl;
        }
        Node* n = new Node(lvl, key, value);
        for (int i = 0; i < lvl; ++i) {
            n->next_[i] = update[i]->next_[i];
            update[i]->next_[i] = n;
        }
        ++size_;
        return true;
    }

    /**
     * @brief Upsert (insert or assign). If key exists, updates value in-place.
     * @return true if a new key was inserted; false if it was an overwrite.
     */
    bool Put(const Key& key, const Value& value) {
        std::vector<Node*> update(max_level_, nullptr);
        Node* x = findGreaterOrEqualMut(key, update);
        if (x && x->key == key) {
            x->value = value;
            return false; // overwrite
        }
        int lvl = randomLevel();
        if (lvl > level_) {
            for (int i = level_; i < lvl; ++i) update[i] = head_;
            level_ = lvl;
        }
        Node* n = new Node(lvl, key, value);
        for (int i = 0; i < lvl; ++i) {
            n->next_[i] = update[i]->next_[i];
            update[i]->next_[i] = n;
        }
        ++size_;
        return true; // inserted
    }

    /**
     * @brief Erase key if present.
     * @return true if a node was removed; false if key not found.
     */
    bool Erase(const Key& key) {
        std::vector<Node*> update(max_level_, nullptr);
        Node* x = head_;
        // Collect predecessors at each level so we can splice out the node.
        for (int i = level_ - 1; i >= 0; --i) {
            while (x->next_[i] && x->next_[i]->key < key) {
                x = x->next_[i];
            }
            update[i] = x;
        }
        x = x->next_[0];
        if (!x || x->key != key) {
            return false;
        }
        for (int i = 0; i < level_; ++i) {
            if (update[i]->next_[i] == x) {
                update[i]->next_[i] = x->next_[i];
            }
        }
        delete x;
        --size_;
        // Reduce overall level if top levels become empty.
        while (level_ > 1 && head_->next_[level_ - 1] == nullptr) {
            --level_;
        }
        return true;
    }

    // --------- Iteration ---------

    /** @brief Iterator to the first (smallest) key. */
    Iterator Begin() const noexcept { return Iterator(head_->next_[0]); }

    /**
     * @brief Create an iterator positioned at the first entry with key >= target.
     * @details If all keys are less than target, returns an end() iterator (Valid()==false).
     */
    Iterator Seek(const Key& target) const noexcept {
        return Iterator(const_cast<Node*>(findGreaterOrEqual(target)));
    }

private:
    // --------- Node definition (kept private) ---------
    struct Node {
        explicit Node(int lvl)
            : next_(static_cast<std::size_t>(lvl), nullptr) {}
        Node(int lvl, Key k, Value v)
            : key(std::move(k)), value(std::move(v)), next_(static_cast<std::size_t>(lvl), nullptr) {}
        Key key;
        Value value;
        std::vector<Node*> next_; // forward pointers of size == node level
    };

    // --------- Internal helpers ---------

    /**
     * @brief Return the first node with key >= target (or nullptr if none).
     * @details Non-modifying search used by Contains/Get/Seek.
     */
    const Node* findGreaterOrEqual(const Key& target) const noexcept {
        const Node* x = head_;
        for (int i = level_ - 1; i >= 0; --i) {
            while (x->next_[i] && x->next_[i]->key < target) {
                x = x->next_[i];
            }
        }
        x = x->next_[0];
        return x;
    }

    /**
     * @brief Same as findGreaterOrEqual, but records the last node < target at each level.
     * @details This is used for splicing during insertions.
     * @param update Output array of size max_level_ to hold the predecessors.
     * @return Node* first node with key >= target (or nullptr).
     */
    Node* findGreaterOrEqualMut(const Key& target, std::vector<Node*>& update) noexcept {
        Node* x = head_;
        for (int i = level_ - 1; i >= 0; --i) {
            while (x->next_[i] && x->next_[i]->key < target) {
                x = x->next_[i];
            }
            update[i] = x;
        }
        x = x->next_[0];
        return x;
    }

    /**
     * @brief Randomly choose a level in [1, max_level_], geometric with P(promote) = p_num_/p_den_.
     * @details Higher levels are exponentially rarer. Ensures at least level 1.
     */
    int randomLevel() {
        int lvl = 1;
        // Promote while coin flips succeed, up to max_level_
        while (lvl < max_level_ && dist_(rng_) < p_num_) {
            ++lvl;
        }
        return lvl;
    }

private:
    // Configuration
    int max_level_;
    int p_num_;
    int p_den_;

    // Current tallest level in the list (1..max_level_)
    int level_;

    // Element count
    std::size_t size_;

    // Sentinel head node with max_level_ forward pointers
    Node* head_;

    // PRNG for level selection
    std::mt19937 rng_;
    std::uniform_int_distribution<int> dist_;
};

} // namespace VrootKV::memtable

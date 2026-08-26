#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <functional>
#include <new>

#include "util/arena.h"

namespace mini_leveldb
{

// Random：Park-Miller 最小标准伪随机数生成器（周期 2^31-2）
class Random
{
public:
    explicit Random(uint32_t s) : seed_(s & 0x7fffffffu) {
        if (seed_ == 0 || seed_ == 2147483647u) {
            seed_ = 1;   // 避开退化点
        }
    }
    uint32_t Next() {
        static const uint32_t M = 2147483647u;    // 2^31 - 1
        static const uint64_t A = 16807;          // Park-Miller 乘子
        uint64_t product = seed_ * A;
        seed_ = static_cast<uint32_t>((product >> 31) + (product & M));
        if (seed_ > M) seed_ -= M;
        return seed_;
    }
private:
    uint32_t seed_;
};

// 跳表
template <typename Key, typename Comparator = std::less<Key>>
class SkipList
{
public:
    explicit SkipList(Arena* arena, Comparator cmp = Comparator())
        : arena_(arena), compare_(cmp)
    {
        head_ = NewNode(Key{});
        cur_height_.store(1, std::memory_order_relaxed);
    }

    // 写入
    void Insert(const Key& key)
    {
        Node* prev[kMaxHeight];
        FindGreaterOrEqual(key, prev);

        int h = RandomHeight();

        if (h > cur_height_.load(std::memory_order_relaxed)) {
            for (int i = cur_height_.load(std::memory_order_relaxed); i < h; i++) {
                prev[i] = head_;
            }
            cur_height_.store(h, std::memory_order_relaxed);
        }

        Node* n = NewNode(key);
        for (int i = 0; i < h; i++) {
            n->next_[i].store(prev[i]->next_[i].load(std::memory_order_relaxed),
                              std::memory_order_relaxed);
        }

        for (int i = 0; i < h; i++) {
            prev[i]->next_[i].store(n, std::memory_order_release);
        }
    }

    // 读取
    bool Contains(const Key& key) const
    {
        Node* x = FindGreaterOrEqual(key, nullptr);
        return x != nullptr
            && !compare_(x->key, key)
            && !compare_(key, x->key);
    }

    // 迭代器
    class Iterator
    {
    public:
        explicit Iterator(const SkipList* list) : list_(list), node_(nullptr) {}

        bool Valid() const { return node_ != nullptr; }
        const Key& key() const { assert(Valid()); return node_->key; }

        // Next：沿 L0 走一步，O(1)
        void Next() { assert(Valid()); node_ = node_->Next(0); }

        // SeekToFirst：定位到第一个节点（head 的 L0 后继）
        void SeekToFirst() { node_ = list_->head_->Next(0); }

        // Seek：跳表多层快速定位到 >= target 的第一个节点，O(log n)
        void Seek(const Key& target) {
            node_ = list_->FindGreaterOrEqual(target, nullptr);
        }

    private:
        const SkipList* list_;
        Node* node_;
    };

private:
    static constexpr int kMaxHeight  = 12;  // 最大层数，覆盖 ~4^12 = 16M 节点
    static constexpr int kBranching   = 4;  // 每层 1/4 概率上升

    struct Node {
        Key key;
        std::atomic<Node*> next_[kMaxHeight];

        explicit Node(const Key& k) : key(k) {
            for (int i = 0; i < kMaxHeight; i++) {
                next_[i].store(nullptr, std::memory_order_relaxed);
            }
        }
        Node* Next(int n) const {
            return next_[n].load(std::memory_order_acquire);
        }
    };

    Arena* arena_;                  // 内存池，分配节点
    Comparator compare_;            // 比较器（具体类，非虚）
    Node* head_;                    // 哑头节点，占 kMaxHeight 层
    std::atomic<int> cur_height_;   // 当前跳表最高占用层数

    // 分配随即高度
    Random rng_{0x9e3779b9};
    int RandomHeight() {
        int h = 1;
        while (h < kMaxHeight && (rng_.Next() % kBranching) == 0) {
            h++;
        }
        return h;
    }

    Node* NewNode(const Key& key) {
        char* mem = arena_->AllocateAligned(sizeof(Node));
        return new (mem) Node(key);
    }

    Node* FindGreaterOrEqual(const Key& key, Node** prev) const {
        int level = cur_height_.load(std::memory_order_acquire) - 1;
        Node* x = head_;
        while (true) {
            Node* next = x->Next(level);
            if (next != nullptr && compare_(next->key, key)) {
                x = next;
            } else {
                if (prev != nullptr) prev[level] = x;
                if (level == 0) return next;
                level--;
            }
        }
    }

    SkipList(const SkipList&) = delete;
    SkipList& operator=(const SkipList&) = delete;
};

}   // namespace mini_leveldb

#pragma once

#include "db/dbformat.h"
#include "util/arena.h"
#include "db/skiplist.h"
#include "mini-leveldb/slice.h"

namespace mini_leveldb
{

class MemTable {
public:
    explicit MemTable() : table_(&arena_) {}

    void Add(SequenceNumber seq, ValueType type,
             const Slice& key, const Slice& value);
    bool Get(const Slice& user_key, std::string* value) const;
    size_t ApproximateMemoryUsage() const;

private:
    struct Key {            // 指向 arena 里的连续记录
        Slice ikey;         // user_key + 8B tag
        Slice value;
    };
    struct Cmp {            // 转发给 InternalKeyComparator
        bool operator()(const Key& a, const Key& b) const {
            return InternalKeyComparator()(a.ikey, b.ikey);
        }
    };

    using Table = SkipList<Key, Cmp>;

    Arena arena_;
    Table table_;
};

}   // namespace mini_leveldb

#pragma once

#include "db/dbformat.h"
#include "db/skiplist.h"
#include "mini-leveldb/slice.h"
#include "util/arena.h"

namespace mini_leveldb {

class MemTable {
   public:
    explicit MemTable() : table_(&arena_) {}

    void Add(SequenceNumber seq, ValueType type, const Slice& key,
             const Slice& value);
    LookupState Get(const Slice& user_key, std::string* value) const;
    size_t ApproximateMemoryUsage() const;

   private:
    struct Key {     // 指向 arena 里的连续记录
        Slice ikey;  // user_key + 8B tag
        Slice value;
    };
    struct Cmp {  // 转发给 InternalKeyComparator
        bool operator()(const Key& a, const Key& b) const {
            return InternalKeyComparator()(a.ikey, b.ikey);
        }
    };

    using Table = SkipList<Key, Cmp>;

   public:
    // flush 用：顺序遍历，ikey 全局递增（user 升序、同 user 高 seq 在前），
    // 正是 TableBuilder 要求的输入序；墓碑条目也会出现，必须一起刷进 SSTable
    class Iterator {
       public:
        explicit Iterator(const MemTable* mem) : it_(&mem->table_) {}
        bool Valid() const { return it_.Valid(); }
        Slice ikey() const { return it_.key().ikey; }
        Slice value() const { return it_.key().value; }
        void Next() { it_.Next(); }
        void SeekToFirst() { it_.SeekToFirst(); }

       private:
        Table::Iterator it_;  // 依赖上方 using Table，声明顺序不能倒
    };
    Iterator NewIterator() const { return Iterator(this); }

   private:
    Arena arena_;
    Table table_;
};

}  // namespace mini_leveldb

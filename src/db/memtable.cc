#include "db/memtable.h"

#include <cstring>

namespace mini_leveldb
{

void MemTable::Add(SequenceNumber seq, ValueType type,
                   const Slice& key, const Slice& value) {
    // 记录布局：[user_key][8B tag][value]，实体存 arena，跳表只挂视图
    const size_t klen = key.size() + 8;
    char* buf = arena_.AllocateAligned(klen + value.size());
    std::memcpy(buf, key.data(), key.size());
    EncodeFixed64(buf + key.size(), PackSequenceAndType(seq, type));
    if (!value.empty()) {
        std::memcpy(buf + klen, value.data(), value.size());
    }
    table_.Insert(Key{Slice(buf, klen), Slice(buf + klen, value.size())});
}

bool MemTable::Get(const Slice& user_key, std::string* value) const {
    // tag 取最大值的哨兵键：同 user_key 按 tag 降序，Seek 恰停在其最新版本
    std::string lookup;
    AppendInternalKey(&lookup, user_key, kMaxSequenceNumber, kTypeValue);
    Table::Iterator it(&table_);
    it.Seek(Key{Slice(lookup), Slice()});

    if (!it.Valid()) return false;
    const Key& entry = it.key();
    if (ExtractUserKey(entry.ikey) != user_key) return false;  // 落到了下一个 key

    if (static_cast<ValueType>(ExtractTag(entry.ikey) & 0xff) == kTypeValue) {
        value->assign(entry.value.data(), entry.value.size());
        return true;
    }
    return false;   // kTypeDeletion：tombstone 视为未命中
}

size_t MemTable::ApproximateMemoryUsage() const {
    return arena_.MemoryUsage();
}

}   // namespace mini_leveldb

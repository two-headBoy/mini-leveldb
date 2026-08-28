#pragma once

#include <cstdint>
#include <string>

#include "mini-leveldb/slice.h"
#include "mini-leveldb/status.h"

namespace mini_leveldb
{

// 把写请求收集编码成字节串，提交时作为一个整体
class WriteBatch {
public:
    WriteBatch() { Clear(); }

    // 追加操作，仅编码进内部缓冲，不触达存储层
    void Put(const Slice& key, const Slice& value);
    void Delete(const Slice& key);

    int Count() const;
    void Clear();

    uint64_t sequence() const;
    void set_sequence(uint64_t seq);

    // WAL相关
    const std::string& Data() const { return rep_; }
    Status SetContents(const Slice& contents);

    // 回放接口：按序把记录交给 handler，回调携带递增后的 seq
    class Handler {
    public:
        virtual ~Handler() = default;
        virtual void Put(uint64_t seq,
                         const Slice& key, const Slice& value) = 0;
        virtual void Delete(uint64_t seq, const Slice& key) = 0;
    };
    Status Iterate(Handler* handler) const;

private:
    // rep_ 字节布局（自描述，WAL 原样落盘）:
    // ┌────────────┬───────────┬──────────────────────────────────────┐
    // │ [8B seq]   │ [4B count]│            记录区（变长）             │
    // └────────────┴───────────┴──────────────────────────────────────┘
    //  └──── kHeader = 12 ────┘
    // 每条记录:
    //   Put    = [1B kTypeValue][varint klen][key][varint vlen][value]
    //   Delete = [1B kTypeDeletion][varint klen][key]
    // seq 是整批的起始版本号，回放时逐条 seq++ 传给 handler
    // count 是记录条数，恢复时用来校验是否被截断
    std::string rep_;
    static constexpr size_t kHeader = 12;   // 头部长度

};

}   // namespace mini_leveldb

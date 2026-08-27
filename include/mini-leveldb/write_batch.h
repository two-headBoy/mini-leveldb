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
    std::string rep_;   // 布局: [8B seq][4B count][record...]
    static constexpr size_t kHeader = 12;   // 头部长度

};

}   // namespace mini_leveldb

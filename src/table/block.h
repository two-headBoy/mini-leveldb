#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mini-leveldb/slice.h"

namespace mini_leveldb
{

// 数据块读端：block 字节流 → 可 Seek/Next 的迭代器
//
// 物理布局（自前向后）：
// ┌──────────────────────────────────────────────────────────┐
// │ entry 数据区                                              │
// │  ┌────────────────────────────────────────────────────┐  │
// │  │ [varint shared][varint unshared][varint vlen]       │  │
// │  │ [key 差量][value]                                    │  │
// │  │  ↑ 三元组解码 key 还原：key_ 截到 shared 再追加差量  │  │
// │  └────────────────────────────────────────────────────┘  │
// │  ↑ 每条 entry 前缀压缩，只和上一条比 shared              │
// ├──────────────────────────────────────────────────────────┤
// │ restart 数组 (count × 4B fixed32)                         │
// │  存每个 restart point 在 entry 区的偏移                  │
// │  restart point 处 entry 的 shared=0，读端 key_ 重置      │
// ├──────────────────────────────────────────────────────────┤
// │ count (4B fixed32)                                        │
// │  倒推用：读端从块尾拿 count，才能定位 restart 数组起点    │
// └──────────────────────────────────────────────────────────┘
//
// 注意：Iterator 持有 restarts_ 的引用，Block 析构后 iterator 不可用
class Block {
public:
    explicit Block(const Slice& contents);
    ~Block() = default;

    Block(const Block&) = delete;
    Block& operator=(const Block&) = delete;

    class Iterator {
    public:
        Iterator(const Slice& data, const std::vector<uint32_t>& restarts)
            : data_(data), restarts_(restarts) {}

        bool Valid() const { return valid_; }
        Slice key() const { return key_; }
        Slice value() const { return value_; }

        void SeekToFirst();
        void Seek(const Slice& target);
        void Next();

    private:
        Slice data_;   // entry 数据区，不含 restart 数组和 count
        const std::vector<uint32_t>& restarts_;   // 引用 Block 内部数组，生命周期绑定
        uint32_t current_ = 0;     // 当前 entry 在 data_ 中的字节偏移
        uint32_t restart_index_ = 0;   // current_ 所属 restart 区间下标
        std::string key_;          // 还原后的完整 key，跨 entry 累积依赖前一条状态
        Slice value_;              // 零拷贝指向 data_ 内部，DecodeEntry 时赋值
        bool valid_ = false;

        // 解析 current_ 处的单条 entry 帧：
        //  读三元组(shared/unshared/vlen) → key_.resize(shared).append(差量) → value_ 切片 → current_ 推进整条 entry
        // key 还原依赖上一条 key_ 状态；restart point 处 shared=0 自动重置
        void DecodeEntry();
    };

    Iterator NewIterator() const { return Iterator(data_, restarts_); }

private:
    Slice data_;                        // entry 数据区（不含 restart 数组）
    std::vector<uint32_t> restarts_;   // restart offsets（在 data_ 之后，构造时倒推得到）
};

}   // namespace mini_leveldb

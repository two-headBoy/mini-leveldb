#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mini-leveldb/slice.h"

namespace mini_leveldb {

// 数据块写端：有序 (key,value) → block 字节流
// entry 格式：[varint shared][varint unshared][varint
// value_len][key差量][value] 每 16 条埋 restart point（shared=0 存全量
// key），读端据此二分定位 块尾：restart offsets 数组 + 4B count（读端倒推 count
// 找数组起点）
class BlockBuilder {
   public:
    BlockBuilder() { Reset(); }

    void Reset();
    void Add(const Slice& key, const Slice& value);
    Slice Finish();
    size_t CurrentSizeEstimate() const;
    bool empty() const { return buffer_.empty(); }

   private:
    // 16 是经验值：压缩率与随机定位开销的平衡点
    static constexpr int kRestartInterval = 16;

    std::string buffer_;  // entry 数据区，Finish 时再追加 restart 数组
    std::string last_key_;            // 上一条 key，算前缀 shared 用
    std::vector<uint32_t> restarts_;  // 每个 restart point 在 buffer_ 中的偏移
    int counter_ = 0;  // 当前区间已写条数，到 16 归零触发下一个 restart
    bool finished_ = false;  // Finish 后禁止再 Add
};

}  // namespace mini_leveldb

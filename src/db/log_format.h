#pragma once

#include <cstdint>

namespace mini_leveldb {

// 32KB 块内物理记录布局: [crc 4B][len 2B][type 1B][payload]
// record 不跨块；块尾剩不到一个头时填零废弃
constexpr int kBlockSize = 32 * 1024;
constexpr int kHeaderSize = 4 + 2 + 1;

enum RecordType : uint8_t {
    kZeroType = 0,   // 只用于填充，从不主动发出
    kFullType = 1,   // 完整一条
    kFirstType = 2,  // 首片
    kMiddleType = 3, // 中间片
    kLastType = 4,   // 尾片
};

}  // namespace mini_leveldb

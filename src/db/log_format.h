#pragma once

#include <cstdint>

namespace mini_leveldb {

// WAL 文件按 32KB 分块，块内是一串物理记录
//
// 物理记录布局:
// ┌─────────┬────────┬────────┬──────────────────┐
// │ CRC 4B  │ len 2B │ type 1B│ payload（len 字节）│
// └─────────┴────────┴────────┴──────────────────┘
//   CRC 算的是 type 那一字节 + payload，落盘前 Mask
//   len  是 payload 字节数（不含头）
//   type 决定本片在整条逻辑记录中的位置
//
// 铁律:
//   1. 物理记录永不跨块——块尾剩不到 7B 头时填零废弃
//   2. 块对齐是 Reader 坏块重同步的基础（下一块开头必是新头）
//   3. 一条逻辑记录（WriteBatch 的 Data()）可能被切成多片，靠 type 串链
constexpr int kBlockSize = 32 * 1024;
constexpr int kHeaderSize = 4 + 2 + 1;   // 7B 头: CRC + len + type

enum RecordType : uint8_t {
    kZeroType = 0,   // 只用于块尾填充，从不主动发出，Reader 见 len=0 跳过
    kFullType = 1,   // 完整一条（短记录，一片装下）
    kFirstType = 2,  // 首片（长记录跨块的第一片）
    kMiddleType = 3, // 中间片
    kLastType = 4,   // 尾片（拼上后就是完整一条）
};

}  // namespace mini_leveldb

#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

#include "mini-leveldb/slice.h"
#include "mini-leveldb/status.h"

namespace mini_leveldb
{

// SSTable 文件指纹，打开文件时校验，防止拿错文件当表读
constexpr uint64_t kTableMagicNumber = 0xdb4775248b80fb57ull;

// block 的定位信息：文件内偏移 + 数据长度（不含 5B 尾部）45
class BlockHandle {
public:
    uint64_t offset() const { return offset_; }
    uint64_t size() const { return size_; }
    void set_offset(uint64_t o) { offset_ = o; }
    void set_size(uint64_t s) { size_ = s; }

    void EncodeTo(std::string* dst) const;
    Status DecodeFrom(Slice* input);

    // 两个 varint64 的最坏编码长度
    static constexpr int kMaxEncodedLength = 10 + 10;

private:
    uint64_t offset_ = 0;
    uint64_t size_ = 0;
};

// 文件末尾的定长引导区，读表时从这里拿到两个索引块的位置
//
// 物理布局（定长 48B，永远可从文件尾倒推定位）：
// ┌──────────────────┬──────────────────┬────────┬────────┐
// │ metaindex_handle │  index_handle    │ 补零   │ magic  │
// │   ≤20B           │     ≤20B         │        │  8B    │
// └──────────────────┴──────────────────┴────────┴────────┘
//   magic == kTableMagicNumber  用来判断文件是不是 SSTable
//   metaindex_handle 指向元数据块（Bloom filter 等），现阶段恒空
//   index_handle     指向索引块，Get 路径靠它二分定位 data block
class Footer {
public:
    const BlockHandle& metaindex_handle() const { return metaindex_handle_; }
    const BlockHandle& index_handle() const { return index_handle_; }
    void set_metaindex_handle(const BlockHandle& h) { metaindex_handle_ = h; }
    void set_index_handle(const BlockHandle& h) { index_handle_ = h; }

    void EncodeTo(std::string* dst) const;
    Status DecodeFrom(Slice* input);

    static constexpr int kEncodedLength =
        2 * BlockHandle::kMaxEncodedLength + 8;

private:
    BlockHandle metaindex_handle_;   // 留给 filter 等元数据，现阶段恒为空
    BlockHandle index_handle_;
};

// block 尾部：1B 压缩标记 + 4B 掩码 CRC
constexpr int kBlockTrailerSize = 5;
constexpr uint8_t kNoCompression = 0x0;   // 不做压缩，标记位保留格式兼容

// 追加一个 block 到文件尾，handle 带回定位信息
Status WriteBlock(std::FILE* file, const Slice& contents, BlockHandle* handle);

// 读取 block，数据落在 scratch 里，*result 指向其中的有效部分
Status ReadBlock(std::FILE* file, const BlockHandle& handle,
                 std::string* scratch, Slice* result);

}   // namespace mini_leveldb

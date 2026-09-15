#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

#include "mini-leveldb/slice.h"
#include "mini-leveldb/status.h"
#include "table/block_builder.h"
#include "table/format.h"

namespace mini_leveldb {

// SSTable 写器：把有序 (key, value) 流组织成完整文件
// 流程：攒 data block → 满 4KB 切块落盘 + 记索引 → 写完组装索引块 + Footer
class TableBuilder {
   public:
    // file 由调用方打开传入，Finish/析构时负责 fflush + fclose
    explicit TableBuilder(std::FILE* file);
    ~TableBuilder();

    TableBuilder(const TableBuilder&) = delete;
    TableBuilder& operator=(const TableBuilder&) = delete;

    // 追加一条有序 KV，必须保证 key 全局递增
    Status Add(const Slice& key, const Slice& value);

    // 收尾：flush 剩余 data block → 写索引块 → 写 Footer → fflush
    Status Finish();

    // 已写入字节数估算
    uint64_t FileSize() const { return file_size_; }

   private:
    static constexpr size_t kBlockSize = 4 * 1024;  // 数据块目标大小

    // 把当前 data_block_builder_ 的内容落盘，记一条索引
    Status FlushDataBlock();

    std::FILE* file_;
    BlockBuilder data_block_builder_;
    BlockBuilder index_block_builder_;
    std::string last_key_;  // 当前数据块最后一条 key，索引条目用
    uint64_t file_size_ = 0;  // 已写入字节数（含 trailer）
    bool finished_ = false;
};

}  // namespace mini_leveldb

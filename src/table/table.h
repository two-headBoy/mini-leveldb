#pragma once

#include <cstdio>
#include <string>

#include "mini-leveldb/slice.h"
#include "mini-leveldb/status.h"
#include "table/block.h"
#include "table/format.h"

namespace mini_leveldb
{

class TableBuilder;   // 前向声明，文件管理交给调用方

// SSTable 读器：打开文件读 Footer，提供 Get 查找
// 查找路径：index block 二分 → 定位 data block → data block 二分
class Table {
public:
    // 打开已写好的 SSTable，校验 magic 并读入索引块
    static Status Open(const std::string& fname, Table** table);

    ~Table();

    Table(const Table&) = delete;
    Table& operator=(const Table&) = delete;

    // 查找 user_key 对应的最新 value
    // 命中返回 true 并填充 value；未命中返回 false
    bool Get(const Slice& user_key, std::string* value);

private:
    // index_data 已从文件读出，Table 内部拷贝一份供 Block 长期引用
    Table(std::FILE* file, const Slice& index_data);

    std::FILE* file_;

    // index_block_data_ 必须在 index_block_ 之前声明（Block 持有 Slice 指向它）
    std::string index_block_data_;
    Block index_block_;
};

}   // namespace mini_leveldb

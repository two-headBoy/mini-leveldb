#pragma once

#include <string>

#include "mini-leveldb/slice.h"
#include "mini-leveldb/status.h"

namespace mini_leveldb
{

// 实现 API：Open / Put / Get / Delete
class DB {
public:
    // 数据库就是 name 目录；不存在则创建，存在则回放 WAL 恢复
    static Status Open(const std::string& name, DB** dbptr);

    DB(const DB&) = delete;
    DB& operator=(const DB&) = delete;
    virtual ~DB() = default;

    virtual Status Put(const Slice& key, const Slice& value) = 0;
    virtual Status Delete(const Slice& key) = 0;
    // 命中返回 OK 且 value 填充；不存在或 tombstone 返回 NotFound
    virtual Status Get(const Slice& key, std::string* value) = 0;

protected:
    DB() = default;

};

}   // namespace mini_leveldb

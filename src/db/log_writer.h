#pragma once

#include <cstdio>

#include "db/log_format.h"
#include "mini-leveldb/slice.h"
#include "mini-leveldb/status.h"

namespace mini_leveldb
{

// WAL 写端：把 AddRecord 的字节流按 32KB 块切块落盘
class LogWriter {
public:
    explicit LogWriter(std::FILE* file) : file_(file) {}

    // 一条逻辑记录，内部自动分片（FIRST/MIDDLE/LAST）
    Status AddRecord(const Slice& data);

private:
    Status EmitPhysicalRecord(RecordType type, const char* ptr, size_t len);

    std::FILE* file_;
    size_t block_offset_ = 0;   // 当前块内写到的位置

};

}   // namespace mini_leveldb

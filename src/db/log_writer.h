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
    // offset：恢复场景下文件已有内容的长度，续写从它所在块内偏移继续
    explicit LogWriter(std::FILE* file, uint64_t offset = 0)
        : file_(file), block_offset_(offset % kBlockSize) {}

    // 一条逻辑记录，内部自动分片（FIRST/MIDDLE/LAST）
    Status AddRecord(const Slice& data);

private:
    Status EmitPhysicalRecord(RecordType type, const char* ptr, size_t len);

    std::FILE* file_;
    size_t block_offset_ = 0;   // 当前块内写到的位置

};

}   // namespace mini_leveldb

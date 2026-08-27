#pragma once

#include <cstdio>
#include <string>

#include "db/log_format.h"
#include "mini-leveldb/slice.h"

namespace mini_leveldb
{

// WAL 读端：拼分片还原逻辑记录
// 坏 record 丢弃并跳到下一块重新同步；返回的 record 指向内部缓冲，下次 ReadRecord 前有效
class LogReader {
public:
    explicit LogReader(std::FILE* file) : file_(file) { Refill(); }

    // 取下一条逻辑记录；没有更多返回 false（残缺尾巴就地丢弃）
    bool ReadRecord(Slice* record);

private:
    enum class ReadResult { kOk, kBad, kEof };

    ReadResult ReadPhysicalRecord(RecordType* type, Slice* frag);
    bool Refill();

    std::FILE* file_;
    char buffer_[kBlockSize];
    size_t pos_ = 0;     // 块内读位置
    size_t limit_ = 0;   // 有效数据尾
    bool eof_ = false;
    std::string res_;    // 分片拼接缓冲

};

}   // namespace mini_leveldb

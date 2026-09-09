#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>

#include "db/log_writer.h"
#include "db/memtable.h"
#include "mini-leveldb/db.h"

namespace mini_leveldb
{

class WriteBatch;

class DBImpl : public DB {
public:
    explicit DBImpl(const std::string& dbname);
    ~DBImpl() override;

    Status Init();

    Status Put(const Slice& key, const Slice& value) override;
    Status Delete(const Slice& key) override;
    Status Get(const Slice& key, std::string* value) override;

private:
    Status Recover(uint64_t* max_seq);
    Status Write(WriteBatch* batch);

    std::string dbname_;
    std::string log_path_;
    std::FILE* log_file_ = nullptr;
    std::unique_ptr<LogWriter> log_;
    std::unique_ptr<MemTable> mem_;
    uint64_t last_seq_ = 0;
    uint64_t next_file_number_ = 1;   // 编号基线，阶段B切 {n}.log 命名后启用
    std::mutex mutex_;

};

}   // namespace mini_leveldb

#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "db/log_writer.h"
#include "db/memtable.h"
#include "mini-leveldb/db.h"

namespace mini_leveldb {

class WriteBatch;
class Table;
class DBTest;

class DBImpl : public DB {
   public:
    explicit DBImpl(const std::string& dbname);
    ~DBImpl() override;

    Status Init();

    Status Put(const Slice& key, const Slice& value) override;
    Status Delete(const Slice& key) override;
    Status Get(const Slice& key, std::string* value) override;

   private:
    friend class DBTest;  // 测试缝：B4 之前手动触发 flush

    // 回放单个 WAL 进 mem，残尾 ftruncate 到 valid_end
    Status Recover(std::FILE* file, uint64_t* max_seq);
    Status Write(WriteBatch* batch);
    // 全程持 mutex：mem → {n}.ldb.tmp → rename → 删旧 WAL → 开新 WAL → 换新 mem
    Status FlushMemTable();

    std::string dbname_;
    uint64_t log_number_ = 0;  // 当前活跃 WAL 编号，与 mem 一一配对
    std::FILE* log_file_ = nullptr;
    std::unique_ptr<LogWriter> log_;
    std::unique_ptr<MemTable> mem_;
    std::vector<std::unique_ptr<Table>>
        tables_;  // 已刷盘 SSTable，编号降序（头=最新），即 Get 下探序；Open
                  // 时一次性常驻
    uint64_t last_seq_ = 0;
    uint64_t next_file_number_ = 1;  // log/ldb 共用单调编号
    std::mutex mutex_;
};

}  // namespace mini_leveldb

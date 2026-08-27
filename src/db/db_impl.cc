#include "db/db_impl.h"

#include <sys/stat.h>
#include <unistd.h>

#include "db/dbformat.h"
#include "db/log_reader.h"
#include "mini-leveldb/write_batch.h"

namespace mini_leveldb
{

namespace {

// WriteBatch 回放的接收端：直接落进 MemTable
class MemTableInserter : public WriteBatch::Handler {
public:
    explicit MemTableInserter(MemTable* mem) : mem_(mem) {}

    void Put(uint64_t seq, const Slice& key, const Slice& value) override {
        mem_->Add(seq, kTypeValue, key, value);
    }
    void Delete(uint64_t seq, const Slice& key) override {
        mem_->Add(seq, kTypeDeletion, key, Slice());
    }

private:
    MemTable* mem_;
};

}  // namespace

DBImpl::DBImpl(const std::string& dbname)
    : dbname_(dbname),
      log_path_(dbname + "/log"),
      mem_(new MemTable) {}

DBImpl::~DBImpl() {
    if (log_file_ != nullptr) {
        std::fflush(log_file_);
        std::fclose(log_file_);
    }
}

Status DB::Open(const std::string& name, DB** dbptr) {
    *dbptr = nullptr;
    DBImpl* impl = new DBImpl(name);
    const Status s = impl->Init();
    if (!s.ok()) {
        delete impl;
        return s;
    }
    *dbptr = impl;
    return Status::OK();
}

Status DBImpl::Init() {
    ::mkdir(dbname_.c_str(), 0755);   // 已存在则忽略

    log_file_ = std::fopen(log_path_.c_str(), "a+b");
    if (log_file_ == nullptr) {
        return Status::IOError("open log failed: " + log_path_);
    }

    uint64_t seq = 0;
    const Status s = Recover(&seq);
    if (!s.ok()) {
        return s;
    }
    last_seq_ = seq;

    // 文件尾即续写点
    std::fseek(log_file_, 0, SEEK_END);
    log_.reset(new LogWriter(log_file_, static_cast<uint64_t>(std::ftell(log_file_))));
    return Status::OK();
}

// 全量回放 WAL 重建 MemTable，顺带算出最大 seq
Status DBImpl::Recover(uint64_t* max_seq) {
    std::fseek(log_file_, 0, SEEK_SET);

    LogReader reader(log_file_);
    MemTableInserter inserter(mem_.get());
    WriteBatch batch;
    Slice record;
    uint64_t seq = 0;

    while (reader.ReadRecord(&record)) {
        const Status s1 = batch.SetContents(record);
        if (!s1.ok()) {
            break;
        }
        const Status s2 = batch.Iterate(&inserter);
        if (!s2.ok()) {
            return s2;
        }
        if (batch.Count() > 0) {
            const uint64_t end = batch.sequence() + batch.Count() - 1;
            if (end > seq) {
                seq = end;
            }
        }
    }
    *max_seq = seq;

    // 剪掉残尾：保证"坏只在尾部"，之后直接追加才安全
    if (::ftruncate(::fileno(log_file_), static_cast<off_t>(reader.valid_end())) != 0) {
        return Status::IOError("truncate log tail failed");
    }
    return Status::OK();
}

Status DBImpl::Write(WriteBatch* batch) {
    std::lock_guard<std::mutex> lock(mutex_);

    batch->set_sequence(last_seq_ + 1);
    const Status s1 = log_->AddRecord(batch->Data());   // 先落 WAL
    if (!s1.ok()) {
        return s1;
    }

    MemTableInserter inserter(mem_.get());
    const Status s2 = batch->Iterate(&inserter);        // 再进内存
    if (!s2.ok()) {
        return s2;
    }
    last_seq_ += batch->Count();
    return Status::OK();
}

Status DBImpl::Put(const Slice& key, const Slice& value) {
    WriteBatch batch;
    batch.Put(key, value);
    return Write(&batch);
}

Status DBImpl::Delete(const Slice& key) {
    WriteBatch batch;
    batch.Delete(key);
    return Write(&batch);
}

Status DBImpl::Get(const Slice& key, std::string* value) {
    std::lock_guard<std::mutex> lock(mutex_);
    // MemTable 内部用 kMaxSeq 哨兵，天然返回最新版本；tombstone 表现为未命中
    if (mem_->Get(key, value)) {
        return Status::OK();
    }
    return Status::NotFound(key);
}

}   // namespace mini_leveldb

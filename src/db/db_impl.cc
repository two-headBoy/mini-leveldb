#include "db/db_impl.h"

#include <sys/stat.h>
#include <unistd.h>

#include "db/dbformat.h"
#include "db/filename.h"
#include "db/log_reader.h"
#include "mini-leveldb/write_batch.h"
#include "table/table_builder.h"

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

// mem 超此容量同步刷盘（不做后台线程，卡顿在 Put 路径是已知代价）
constexpr size_t kMaxMemTableBytes = 4 * 1024 * 1024;

}  // namespace

DBImpl::DBImpl(const std::string& dbname)
    : dbname_(dbname),
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

    // 扫目录：定编号基线（max+1），顺带清理孤儿 .tmp（刷盘中途崩溃的残留）
    std::vector<FileInfo> files;
    if (!ListFiles(dbname_, &files).ok()) {
        return Status::IOError("scan dir failed: " + dbname_);
    }
    uint64_t max_log_number = 0;
    for (const auto& f : files) {
        if (f.number >= next_file_number_) {
            next_file_number_ = f.number + 1;
        }
        if (f.type == FileType::kTempFile) {
            if (std::remove(TempFileName(dbname_, f.number).c_str()) != 0) {
                return Status::IOError("remove orphan tmp failed");
            }
        } else if (f.type == FileType::kLogFile && f.number > max_log_number) {
            max_log_number = f.number;
        }
    }

    // 编号升序回放全部 WAL（正常仅 1 个；rename 后删 log 前崩溃可能留多个，
    // 回放产生重复数据，Get 从新到旧 + 高 seq 优先 → 幂等无害）
    uint64_t seq = 0;
    if (max_log_number != 0) {
        for (const auto& f : files) {
            if (f.type != FileType::kLogFile) {
                continue;
            }
            const std::string path = LogFileName(dbname_, f.number);
            // 最大 log 回放后续开为当前 WAL，其余回放完即关（残留待下次 flush 清）
            const bool is_current = (f.number == max_log_number);
            std::FILE* lf = std::fopen(path.c_str(), is_current ? "a+b" : "r+b");
            if (lf == nullptr) {
                return Status::IOError("open log failed: " + path);
            }
            const Status rs = Recover(lf, &seq);
            if (!rs.ok()) {
                std::fclose(lf);
                return rs;
            }
            if (is_current) {
                log_number_ = f.number;
                log_file_ = lf;
                // valid_end 之后即续写点
                std::fseek(log_file_, 0, SEEK_END);
                log_.reset(new LogWriter(log_file_,
                                         static_cast<uint64_t>(std::ftell(log_file_))));
            } else {
                std::fclose(lf);
            }
        }
    } else {
        // 全新库：分配首个 WAL 编号
        log_number_ = next_file_number_++;
        const std::string path = LogFileName(dbname_, log_number_);
        log_file_ = std::fopen(path.c_str(), "a+b");
        if (log_file_ == nullptr) {
            return Status::IOError("create log failed: " + path);
        }
        log_.reset(new LogWriter(log_file_, 0));
    }
    last_seq_ = seq;
    return Status::OK();
}

// 回放单个 WAL 重建 MemTable，顺带算出最大 seq，残尾 ftruncate
Status DBImpl::Recover(std::FILE* file, uint64_t* max_seq) {
    std::fseek(file, 0, SEEK_SET);

    LogReader reader(file);
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
    if (::ftruncate(::fileno(file), static_cast<off_t>(reader.valid_end())) != 0) {
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

    // 同步 flush：失败向上传播（WAL 已落盘，返回失败也不丢数据）
    if (mem_->ApproximateMemoryUsage() >= kMaxMemTableBytes) {
        return FlushMemTable();
    }
    return Status::OK();
}

// 同步刷盘：调用方已持 mutex_。表 rename 成功是崩溃安全分界线：
// 之前失败只丢 .tmp（重启清理 + WAL 回放重建）；之后旧 WAL 删除失败
// 也不影响正确性（重启升序回放幂等）
Status DBImpl::FlushMemTable() {
    auto it = mem_->NewIterator();
    it.SeekToFirst();
    if (!it.Valid()) {
        return Status::OK();   // 空 mem 不刷空表
    }

    const uint64_t table_number = next_file_number_++;
    const uint64_t new_log_number = next_file_number_++;

    // 1. mem 顺序喂 TableBuilder 写 .tmp（遍历序即 internal key 全局递增）
    const std::string tmp_name = TempFileName(dbname_, table_number);
    std::FILE* tf = std::fopen(tmp_name.c_str(), "wb");
    if (tf == nullptr) {
        return Status::IOError("create tmp table failed: " + tmp_name);
    }
    {
        TableBuilder builder(tf);   // 析构负责 fclose，错误路径也覆盖
        for (; it.Valid(); it.Next()) {
            const Status s = builder.Add(it.ikey(), it.value());
            if (!s.ok()) return s;
        }
        const Status s = builder.Finish();
        if (!s.ok()) return s;
    }

    // 2. rename：表此刻才对恢复可见
    const std::string table_name = TableFileName(dbname_, table_number);
    if (std::rename(tmp_name.c_str(), table_name.c_str()) != 0) {
        return Status::IOError("rename tmp table failed: " + tmp_name);
    }

    // 3. 关闭并删除旧 WAL（数据已在表中）。fclose 无论成败流都已失效，先摘指针
    const std::string old_log_name = LogFileName(dbname_, log_number_);
    std::fflush(log_file_);
    const int close_rc = std::fclose(log_file_);
    log_file_ = nullptr;
    log_.reset();
    if (close_rc != 0) {
        return Status::IOError("close old log failed: " + old_log_name);
    }
    if (std::remove(old_log_name.c_str()) != 0) {
        return Status::IOError("remove old log failed: " + old_log_name);
    }

    // 4. 开新 WAL，与新 mem 一一配对
    log_number_ = new_log_number;
    const std::string new_log_name = LogFileName(dbname_, log_number_);
    log_file_ = std::fopen(new_log_name.c_str(), "a+b");
    if (log_file_ == nullptr) {
        return Status::IOError("create new log failed: " + new_log_name);
    }
    log_.reset(new LogWriter(log_file_, 0));
    mem_.reset(new MemTable);
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
    // 单层语义：kValue→OK；kDeleted/kNotFound→NotFound（多层下探是 B5 的事）
    if (mem_->Get(key, value) == LookupState::kValue) {
        return Status::OK();
    }
    return Status::NotFound(key);
}

}   // namespace mini_leveldb

#include "mini-leveldb/write_batch.h"

#include "db/dbformat.h"
#include "util/coding.h"

namespace mini_leveldb
{

namespace {
// 记录首字节复用 MemTable 的值类型，回放时可直接对应 Add 的 type
constexpr uint8_t kTagValue    = static_cast<uint8_t>(kTypeValue);
constexpr uint8_t kTagDeletion = static_cast<uint8_t>(kTypeDeletion);
}  // namespace

void WriteBatch::Clear() {
    rep_.clear();
    rep_.resize(kHeader);
}

int WriteBatch::Count() const {
    return DecodeFixed32(rep_.data() + 8);
}

uint64_t WriteBatch::sequence() const {
    return DecodeFixed64(rep_.data());
}

void WriteBatch::set_sequence(uint64_t seq) {
    EncodeFixed64(&rep_[0], seq);
}

void WriteBatch::Put(const Slice& key, const Slice& value) {
    rep_.push_back(kTagValue);
    PutLengthPrefixedSlice(&rep_, key);
    PutLengthPrefixedSlice(&rep_, value);
    EncodeFixed32(rep_.data() + 8, Count() + 1);
}

void WriteBatch::Delete(const Slice& key) {
    rep_.push_back(kTagDeletion);
    PutLengthPrefixedSlice(&rep_, key);
    EncodeFixed32(rep_.data() + 8, Count() + 1);
}

Status WriteBatch::SetContents(const Slice& contents) {
    if (contents.size() < kHeader) {
        return Status::Corruption("malformed WriteBatch (too small)");
    }
    rep_.assign(contents.data(), contents.size());
    return Status::OK();
}

Status WriteBatch::Iterate(Handler* handler) const {
    Slice input(rep_);
    input.remove_prefix(kHeader);

    SequenceNumber seq = sequence();
    // 以 header 声明的数量为配额，逐条扣减；防止坏数据把多余记录喂进 handler
    uint64_t remaining = static_cast<uint64_t>(Count());
    Slice key, value;
    while (!input.empty()) {
        if (remaining == 0) {
            return Status::Corruption("WriteBatch has too many records");
        }
        const uint8_t tag = static_cast<uint8_t>(input.data()[0]);
        input.remove_prefix(1);
        switch (tag) {
            case kTagValue:
                if (!GetLengthPrefixedSlice(&input, &key) ||
                    !GetLengthPrefixedSlice(&input, &value)) {
                    return Status::Corruption("bad WriteBatch Put");
                }
                handler->Put(seq++, key, value);
                break;
            case kTagDeletion:
                if (!GetLengthPrefixedSlice(&input, &key)) {
                    return Status::Corruption("bad WriteBatch Delete");
                }
                handler->Delete(seq++, key);
                break;
            default:
                return Status::Corruption("unknown WriteBatch tag");
        }
        --remaining;
    }

    if (remaining != 0) {   // header 声明的记录数没凑齐
        return Status::Corruption("WriteBatch has wrong count");
    }
    return Status::OK();
}

}   // namespace mini_leveldb

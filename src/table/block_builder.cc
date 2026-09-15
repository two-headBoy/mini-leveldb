#include "table/block_builder.h"

#include <algorithm>

#include "util/coding.h"

namespace mini_leveldb {

void BlockBuilder::Reset() {
    buffer_.clear();
    last_key_.clear();
    restarts_.clear();
    counter_ = 0;
    finished_ = false;
}

void BlockBuilder::Add(const Slice& key, const Slice& value) {
    // counter_==0 即 restart point：记录当前偏移，后续 key 强制 shared=0
    if (counter_ == 0) {
        restarts_.push_back(static_cast<uint32_t>(buffer_.size()));
    }
    // 算与上一条 key 的公共前缀长度
    size_t shared = 0;
    if (counter_ > 0) {
        const size_t min_len = std::min(key.size(), last_key_.size());
        while (shared < min_len && key.data()[shared] == last_key_[shared]) {
            shared++;
        }
    }
    const size_t unshared = key.size() - shared;

    // 三元组 + key 差量 + value，全部追加到 buffer_
    PutVarint32(&buffer_, static_cast<uint32_t>(shared));
    PutVarint32(&buffer_, static_cast<uint32_t>(unshared));
    PutVarint32(&buffer_, static_cast<uint32_t>(value.size()));
    buffer_.append(key.data() + shared, unshared);
    buffer_.append(value.data(), value.size());

    last_key_.assign(key.data(), key.size());

    counter_++;
    if (counter_ >= kRestartInterval) {
        counter_ = 0;  // 归零，下一条即为新 restart point
    }
}

Slice BlockBuilder::Finish() {
    // 追写 restart 数组 + 4B count，读端从尾部倒推定位
    for (uint32_t offset : restarts_) {
        buffer_.append(4, '\0');
        EncodeFixed32(&buffer_[buffer_.size() - 4], offset);
    }
    buffer_.append(4, '\0');
    EncodeFixed32(&buffer_[buffer_.size() - 4],
                  static_cast<uint32_t>(restarts_.size()));
    finished_ = true;
    return Slice(buffer_);
}

// 预估当前 block 总大小（entry区 + restart数组 + count），上层据此切块
size_t BlockBuilder::CurrentSizeEstimate() const {
    return buffer_.size() + 4 + restarts_.size() * 4;
}

}  // namespace mini_leveldb

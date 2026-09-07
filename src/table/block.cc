#include "table/block.h"

#include "util/coding.h"

namespace mini_leveldb
{

Block::Block(const Slice& contents) {
    if (contents.size() < 4) return;   // 空块或非法块

    // 块尾 4B 是 restart count，先读它才能倒推数组起点
    const uint32_t count =
        DecodeFixed32(contents.data() + contents.size() - 4);
    const size_t restart_array_size = count * 4;
    if (contents.size() < 4 + restart_array_size) return;   // 损坏数据

    // restart 数组紧跟 count 之前，前面的就是 data_
    const char* restart_ptr =
        contents.data() + contents.size() - 4 - restart_array_size;
    data_ = Slice(contents.data(),
                  static_cast<size_t>(restart_ptr - contents.data()));

    restarts_.reserve(count);
    for (uint32_t i = 0; i < count; i++) {
        restarts_.push_back(DecodeFixed32(restart_ptr + i * 4));
    }
}

void Block::Iterator::DecodeEntry() {
    if (current_ >= data_.size()) {
        valid_ = false;
        return;
    }
    Slice input(data_.data() + current_, data_.size() - current_);
    uint32_t shared = 0, unshared = 0, value_len = 0;
    if (!GetVarint32(&input, &shared) ||
        !GetVarint32(&input, &unshared) ||
        !GetVarint32(&input, &value_len)) {
        valid_ = false;
        return;
    }

    // 还原 key：前 shared 个字符是上一条 key 的前缀，追加差量
    key_.resize(shared);
    key_.append(input.data(), unshared);
    input.remove_prefix(unshared);

    value_ = Slice(input.data(), value_len);   // 零拷贝

    // input.data() 此时指向 value 起点，差值 = 三元组+key差量的字节数，加 value_len 得整条 entry 大小
    const size_t entry_size =
        (input.data() - data_.data() - current_) + value_len;
    current_ += static_cast<uint32_t>(entry_size);
    valid_ = true;
}

void Block::Iterator::SeekToFirst() {
    if (restarts_.empty()) {
        valid_ = false;
        return;
    }
    restart_index_ = 0;
    current_ = restarts_[0];   // 第一条一定是 restart point
    DecodeEntry();
}

void Block::Iterator::Seek(const Slice& target) {
    if (restarts_.empty()) {
        valid_ = false;
        return;
    }

    // 第一层：二分 restart 数组，找最后一个 key < target 的 restart
    // restart 处 key 是全量的（shared=0），可直接比较
    uint32_t left = 0;
    uint32_t right = static_cast<uint32_t>(restarts_.size()) - 1;
    while (left < right) {
        // 上中点：left=mid 时不会死循环（如 left=3,right=4 → mid=4 → right=3 收敛）
        uint32_t mid = (left + right + 1) / 2;
        current_ = restarts_[mid];
        DecodeEntry();
        if (Slice(key_).compare(target) < 0) {
            left = mid;
        } else {
            right = mid - 1;
        }
    }

    // 第二层：从该 restart 线性扫描，直到 key >= target
    // 区间内 entry 是前缀压缩的，无法随机访问，只能逐条 Next
    restart_index_ = left;
    current_ = restarts_[left];
    DecodeEntry();
    while (Valid() && Slice(key_).compare(target) < 0) {
        Next();
    }
}

void Block::Iterator::Next() {
    if (!valid_) return;
    DecodeEntry();   // 解析下一条，current_ 自动推进

    // 同步 restart_index_：跨过下一个 restart 时前移
    // 非必须，下次 Seek 会重算，但保持状态一致更健康
    while (restart_index_ + 1 < restarts_.size() &&
           current_ >= restarts_[restart_index_ + 1]) {
        restart_index_++;
    }
}

}   // namespace mini_leveldb

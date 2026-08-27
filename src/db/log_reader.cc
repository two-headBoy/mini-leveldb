#include "db/log_reader.h"

#include "util/coding.h"
#include "util/crc32c.h"

namespace mini_leveldb
{

bool LogReader::ReadRecord(Slice* record) {
    res_.clear();
    bool in_frag = false;

    for (;;) {
        RecordType type;
        Slice frag;
        switch (ReadPhysicalRecord(&type, &frag)) {
            case ReadResult::kOk:
                if (type == kFullType) {
                    if (in_frag) {
                        res_.clear();   // 前一条残缺，丢弃
                        in_frag = false;
                    }
                    *record = frag;
                    return true;
                }
                if (type == kFirstType) {
                    res_.assign(frag.data(), frag.size());
                    in_frag = true;
                } else {   // MIDDLE / LAST
                    if (!in_frag) {
                        break;   // 孤儿片，当坏数据
                    }
                    res_.append(frag.data(), frag.size());
                    if (type == kLastType) {
                        *record = Slice(res_);
                        return true;
                    }
                }
                break;

            case ReadResult::kBad:
                res_.clear();
                in_frag = false;
                pos_ = kBlockSize;   // 本块剩余全丢，下块重新同步
                break;

            case ReadResult::kEof:
                return false;
        }
    }
}

LogReader::ReadResult LogReader::ReadPhysicalRecord(RecordType* type, Slice* frag) {
    for (;;) {
        if (pos_ + kHeaderSize > limit_) {
            if (eof_) {
                return ReadResult::kEof;   // 块尾填充或残缺尾巴都在这终结
            }
            pos_ = limit_;
            if (!Refill()) {
                return ReadResult::kEof;
            }
            continue;
        }

        const char* h = buffer_ + pos_;
        const size_t len = static_cast<uint8_t>(h[4]) |
                           (static_cast<uint8_t>(h[5]) << 8);
        if (pos_ + kHeaderSize + len > limit_) {
            // record 不跨块，payload 越界必坏；文件尾则说明尾巴被截
            if (eof_) {
                return ReadResult::kEof;
            }
            return ReadResult::kBad;
        }

        const uint8_t t = static_cast<uint8_t>(h[6]);
        if (t == kZeroType || t > kLastType) {
            return ReadResult::kBad;
        }
        const uint32_t expect = MaskCrc32c(Extend(Value(h + 6, 1), h + kHeaderSize, len));
        if (expect != DecodeFixed32(h)) {
            return ReadResult::kBad;
        }

        pos_ += kHeaderSize + len;
        *type = static_cast<RecordType>(t);
        *frag = Slice(h + kHeaderSize, len);
        return ReadResult::kOk;
    }
}

bool LogReader::Refill() {
    if (eof_) {
        return false;
    }
    limit_ = std::fread(buffer_, 1, kBlockSize, file_);
    pos_ = 0;
    if (limit_ < static_cast<size_t>(kBlockSize)) {
        eof_ = true;   // 最后一块
    }
    return limit_ > 0;
}

}   // namespace mini_leveldb

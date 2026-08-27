#include "db/log_writer.h"

#include <algorithm>

#include "util/coding.h"
#include "util/crc32c.h"

namespace mini_leveldb
{

Status LogWriter::AddRecord(const Slice& data) {
    const char* ptr = data.data();
    size_t left = data.size();
    bool begin = true;   // 是否本条记录的第一片
    Status s;

    do {
        const size_t leftover = kBlockSize - block_offset_;
        if (leftover < static_cast<size_t>(kHeaderSize)) {
            if (leftover > 0) {   // 块尾填零废弃
                static const char kZeros[kHeaderSize - 1] = {0};
                if (std::fwrite(kZeros, 1, leftover, file_) != leftover) {
                    return Status::IOError("write log padding failed");
                }
            }
            block_offset_ = 0;
        }

        const size_t avail = kBlockSize - block_offset_ - kHeaderSize;
        const size_t frag_len = std::min(left, avail);
        const bool end = (frag_len == left);

        RecordType type;
        if (begin && end) {
            type = kFullType;
        } else if (begin) {
            type = kFirstType;
        } else if (end) {
            type = kLastType;
        } else {
            type = kMiddleType;
        }

        s = EmitPhysicalRecord(type, ptr, frag_len);
        ptr += frag_len;
        left -= frag_len;
        begin = false;
    } while (s.ok() && left > 0);

    if (s.ok()) {
        std::fflush(file_);
    }
    return s;
}

Status LogWriter::EmitPhysicalRecord(RecordType type, const char* ptr, size_t len) {
    char header[kHeaderSize];
    header[4] = static_cast<char>(len & 0xff);
    header[5] = static_cast<char>((len >> 8) & 0xff);
    header[6] = static_cast<char>(type);

    // crc 只覆盖 type + payload
    const uint32_t crc = MaskCrc32c(Extend(Value(header + 6, 1), ptr, len));
    EncodeFixed32(header, crc);

    if (std::fwrite(header, 1, kHeaderSize, file_) != kHeaderSize ||
        (len > 0 && std::fwrite(ptr, 1, len, file_) != len)) {
        return Status::IOError("write log record failed");
    }
    block_offset_ += kHeaderSize + len;
    return Status::OK();
}

}   // namespace mini_leveldb

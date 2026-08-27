#pragma once

#include <cstdint>
#include <string>

#include "mini-leveldb/slice.h"
#include "util/coding.h"

namespace mini_leveldb {

enum ValueType : uint8_t {
    kTypeDeletion = 0,   // 删除标记
    kTypeValue    = 1,   // 普通
};

using SequenceNumber = uint64_t;
inline constexpr uint64_t kMaxSequenceNumber = (1ull << 56) - 1;

struct ParsedInternalKey {
    Slice user_key;
    SequenceNumber sequence;
    ValueType type;
};

inline uint64_t PackSequenceAndType(SequenceNumber seq, ValueType t) {
    return (seq << 8) | static_cast<uint8_t>(t);
}

inline Slice ExtractUserKey(const Slice& internal_key) {
    return Slice(internal_key.data(), internal_key.size() - 8);
}

inline uint64_t ExtractTag(const Slice& internal_key) {
    return DecodeFixed64(internal_key.data() + internal_key.size() - 8);
}

inline void AppendInternalKey(std::string* dst, const Slice& user_key,
                              SequenceNumber seq, ValueType type) {
    dst->append(user_key.data(), user_key.size());
    char buf[8];
    EncodeFixed64(buf, PackSequenceAndType(seq, type));
    dst->append(buf, 8);
}

inline bool ParseInternalKey(Slice internal, ParsedInternalKey* result) {
    if (internal.size() < 8) return false;
    const uint64_t tag = ExtractTag(internal);
    result->sequence = tag >> 8;
    result->type = static_cast<ValueType>(tag & 0xff);
    result->user_key = ExtractUserKey(internal);
    return true;
}

// 严格弱序：user_key 升序 → tag (seq<<8|type) 降序，保证同 key 新版本排前面
class InternalKeyComparator {
public:
    bool operator()(const Slice& a, const Slice& b) const {
        int r = ExtractUserKey(a).compare(ExtractUserKey(b));
        if (r != 0) return r < 0;
        return ExtractTag(a) > ExtractTag(b);
    }
};

} // namespace mini_leveldb

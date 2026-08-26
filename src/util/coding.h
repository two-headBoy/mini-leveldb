#pragma once

#include <cstdint>
#include <cstring>
#include <string>

#include "mini-leveldb/slice.h"

namespace mini_leveldb {

// 变长编码：String
void PutVarint32(std::string* dst, uint32_t value);
void PutVarint64(std::string* dst, uint64_t value);
void PutLengthPrefixedSlice(std::string* dst, const Slice& value);

// 变长编码：Slice 
bool GetVarint32(Slice* input, uint32_t* value);
bool GetVarint64(Slice* input, uint64_t* value);
bool GetLengthPrefixedSlice(Slice* input, Slice* result);

// 定长编码
inline void EncodeFixed32(char* dst, uint32_t value) {
    uint8_t* p = reinterpret_cast<uint8_t*>(dst);
    for (int i = 0; i < 4; i++) {
        p[i] = static_cast<uint8_t>(value >> (8 * i));
    }
}

inline void EncodeFixed64(char* dst, uint64_t value) {
    uint8_t* p = reinterpret_cast<uint8_t*>(dst);
    for (int i = 0; i < 8; i++) {
        p[i] = static_cast<uint8_t>(value >> (8 * i));
    }
}

inline uint32_t DecodeFixed32(const char* ptr) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(ptr);
    uint32_t result = 0;
    for (int i = 0; i < 4; i++) {
        result |= static_cast<uint32_t>(p[i]) << (8 * i);
    }
    return result;
}

inline uint64_t DecodeFixed64(const char* ptr) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(ptr);
    uint64_t result = 0;
    for (int i = 0; i < 8; i++) {
        result |= static_cast<uint64_t>(p[i]) << (8 * i);
    }
    return result;
}

} // namespace mini_leveldb

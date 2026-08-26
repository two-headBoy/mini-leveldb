#include "util/coding.h"

namespace mini_leveldb {

namespace {

char* EncodeVarint(char* dst, uint64_t v) {
    uint8_t* ptr = reinterpret_cast<uint8_t*>(dst);
    while (v >= 128) {
        *(ptr++) = static_cast<uint8_t>((v & 0x7F) | 0x80);
        v >>= 7;
    }
    *(ptr++) = static_cast<uint8_t>(v);
    return reinterpret_cast<char*>(ptr);
}

const char* GetVarint32Ptr(const char* p, const char* limit, uint32_t* value) {
    uint32_t result = 0;
    for (uint32_t shift = 0; shift <= 28 && p < limit; shift += 7) {
        uint32_t byte = *reinterpret_cast<const uint8_t*>(p++);
        if (byte & 128) {
            result |= (byte & 127) << shift;
        } else {
            result |= byte << shift;
            *value = result;
            return p;
        }
    }
    return nullptr;
}

const char* GetVarint64Ptr(const char* p, const char* limit, uint64_t* value) {
    uint64_t result = 0;
    for (uint32_t shift = 0; shift <= 63 && p < limit; shift += 7) {
        uint64_t byte = *reinterpret_cast<const uint8_t*>(p++);
        if (byte & 128) {
            result |= (byte & 127) << shift;
        } else {
            result |= byte << shift;
            *value = result;
            return p;
        }
    }
    return nullptr;
}

} // namespace

void PutVarint32(std::string* dst, uint32_t v) {
    char buf[5];
    char* ptr = EncodeVarint(buf, v);
    dst->append(buf, ptr - buf);
}

void PutVarint64(std::string* dst, uint64_t v) {
    char buf[10];
    char* ptr = EncodeVarint(buf, v);
    dst->append(buf, ptr - buf);
}

void PutLengthPrefixedSlice(std::string* dst, const Slice& value) {
    PutVarint32(dst, value.size());
    dst->append(value.data(), value.size());
}

bool GetVarint32(Slice* input, uint32_t* value) {
    const char* p = input->data();
    const char* q = GetVarint32Ptr(p, p + input->size(), value);
    if (q == nullptr) return false;
    input->remove_prefix(q - p);
    return true;
}

bool GetVarint64(Slice* input, uint64_t* value) {
    const char* p = input->data();
    const char* q = GetVarint64Ptr(p, p + input->size(), value);
    if (q == nullptr) return false;
    input->remove_prefix(q - p);
    return true;
}

bool GetLengthPrefixedSlice(Slice* input, Slice* result) {
    uint32_t len;
    if (GetVarint32(input, &len) && input->size() >= len) {
        *result = Slice(input->data(), len);
        input->remove_prefix(len);
        return true;
    }
    return false;
}

} // namespace mini_leveldb

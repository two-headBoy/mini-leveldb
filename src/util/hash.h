#pragma once

#include <cstddef>
#include <cstdint>

namespace mini_leveldb
{

// MurmurHash 变体，Bloom Filter 用。seed 让同一 key 可派生出 k 个独立哈希值。
// 必须确定性（跨平台一致），reopen 后 Bloom 位数组才不会失效。
inline uint32_t Hash(const char* data, size_t n, uint32_t seed) {
    const uint32_t m = 0xc6a4a793u;
    const uint32_t r = 24;
    const char* limit = data + n;
    uint32_t h = seed ^ (n * m);

    while (data + 4 <= limit) {
        uint32_t w = static_cast<uint8_t>(data[0]) |
                     (static_cast<uint8_t>(data[1]) << 8) |
                     (static_cast<uint8_t>(data[2]) << 16) |
                     (static_cast<uint8_t>(data[3]) << 24);
        data += 4;
        h += w;
        h *= m;
        h ^= (h >> 16);
    }

    switch (limit - data) {
        case 3:
            h += static_cast<uint8_t>(data[2]) << 16;
            [[fallthrough]];
        case 2:
            h += static_cast<uint8_t>(data[1]) << 8;
            [[fallthrough]];
        case 1:
            h += static_cast<uint8_t>(data[0]);
            h *= m;
            h ^= (h >> r);
            break;
    }
    return h;
}

}   // namespace mini_leveldb

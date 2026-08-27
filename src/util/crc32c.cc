#include "util/crc32c.h"

#include <cstring>

namespace mini_leveldb {

namespace {

constexpr uint32_t kPoly = 0x82f63b78u;

// 四张 256 项的表，一次消化 4 字节
struct Tables {
    uint32_t t[4][256];

    Tables() {
        for (uint32_t i = 0; i < 256; ++i) {   // 单字节基础表
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) ? (c >> 1) ^ kPoly : (c >> 1);
            }
            t[0][i] = c;
        }
        for (uint32_t i = 0; i < 256; ++i) {   // 其余三张从 t[0] 派生
            uint32_t c = t[0][i];
            for (int lvl = 1; lvl < 4; ++lvl) {
                c = (c >> 8) ^ t[0][c & 0xff];
                t[lvl][i] = c;
            }
        }
    }
};

const Tables& GetTables() {
    static const Tables tables;   // 只构造一次
    return tables;
}

}  // namespace

uint32_t Extend(uint32_t crc, const char* data, size_t n) {
    const Tables& tab = GetTables();
    const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
    uint32_t c = ~crc;   // 头尾取反是 CRC 的规定动作

    while (n >= 4) {
        uint32_t word;
        std::memcpy(&word, p, 4);   // 不挑对齐
        c ^= word;
        c = tab.t[3][c & 0xff] ^
            tab.t[2][(c >> 8) & 0xff] ^
            tab.t[1][(c >> 16) & 0xff] ^
            tab.t[0][c >> 24];
        p += 4;
        n -= 4;
    }
    while (n--) {   // 尾巴凑不满 4 字节
        c = tab.t[0][(c ^ *p++) & 0xff] ^ (c >> 8);
    }
    return ~c;
}

}  // namespace mini_leveldb

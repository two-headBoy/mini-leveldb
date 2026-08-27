#pragma once

#include <cstddef>
#include <cstdint>

namespace mini_leveldb {

// CRC-32C 校验和
uint32_t Extend(uint32_t crc, const char* data, size_t n);

inline uint32_t Value(const char* data, size_t n) { return Extend(0, data, n); }

// 存前掩码一下，避免全零块算出 CRC=0 时和"没校验"混淆
constexpr uint32_t kMaskDelta = 0xa282ead8u;

inline uint32_t MaskCrc32c(uint32_t crc) {
    return ((crc >> 15) | (crc << 17)) + kMaskDelta;
}

inline uint32_t UnmaskCrc32c(uint32_t masked) {
    uint32_t rot = masked - kMaskDelta;
    return (rot >> 17) | (rot << 15);
}

}  // namespace mini_leveldb

#include "util/arena.h"

#include <cassert>
#include <cstdint>

namespace mini_leveldb
{

static const int kBlockSize = 4096;

Arena::~Arena() {
    for (auto* p : blocks_) {
        delete[] p;
    }
}

char* Arena::Allocate(size_t bytes) {
    assert(bytes > 0);
    if (bytes <= avail_) {
        char* result = curr_;
        curr_ += bytes;
        avail_ -= bytes;
        return result;
    }
    if (bytes > kBlockSize / 4) {
        return AllocateNewBlock(bytes);
    }

    curr_ = AllocateNewBlock(kBlockSize);
    avail_ = kBlockSize;
    char* result = curr_;
    curr_ += bytes;
    avail_ -= bytes;
    return result;
}

char* Arena::AllocateAligned(size_t bytes) {
    assert(bytes > 0);
    const size_t align = (sizeof(void*) > 8) ? sizeof(void*) : 8;
    size_t current_mod = reinterpret_cast<uintptr_t>(curr_) & (align - 1);
    size_t slop = (current_mod == 0) ? 0 : (align - current_mod);
    size_t needed = bytes + slop;

    char* result;
    if (needed <= avail_) {
        result = curr_ + slop;
        curr_ += needed;
        avail_ -= needed;
    } else {    // 参考原版，调用频率低，不进行检查大块小块检查
        result = AllocateNewBlock(needed);
    }
    assert(reinterpret_cast<uintptr_t>(result) % align == 0);
    return result;
}

char* Arena::AllocateNewBlock(size_t block_bytes) {
    char* result = new char[block_bytes];
    blocks_.push_back(result);
    blocks_bytes_ += block_bytes;
    return result;
}

}   // namespace mini_leveldb

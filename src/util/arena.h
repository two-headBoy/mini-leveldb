#pragma once

#include <cstddef>
#include <vector>

namespace mini_leveldb
{

class Arena {
public:
    Arena() = default;
    ~Arena();

    char* Allocate(size_t bytes);
    char* AllocateAligned(size_t bytes);

    size_t MemoryUsage() const { return blocks_bytes_; }

private:
    char* AllocateNewBlock(size_t bytes);

    char* curr_ = nullptr;
    size_t avail_ = 0;
    size_t blocks_bytes_ = 0;
    std::vector<char*> blocks_;

};

}   // namespace mini_leveldb

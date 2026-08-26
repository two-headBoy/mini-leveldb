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

private:
    char* AllocateNewBlock(size_t bytes);

    char* curr_ = nullptr;
    size_t avail_ = 0;
    std::vector<char*> blocks_;

};

}   // namespace mini_leveldb

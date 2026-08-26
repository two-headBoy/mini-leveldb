#pragma once

#include <string>
#include <string_view>
#include <cstring>
#include <algorithm>

namespace mini_leveldb
{

class Slice {
public:
    Slice() = default;
    Slice(const char* d, size_t n) : view_(d, n) {}
    Slice(const std::string& s) : view_(s) {}
    Slice(std::string_view v) : view_(v) {}

    const char* data() const { return view_.data(); }
    size_t size() const { return view_.size(); }
    bool empty() const { return view_.empty(); }

    void remove_prefix(size_t n) { view_.remove_prefix(n); }
    void clear() noexcept { view_ = {}; }
    std::string ToString() const { return std::string(view_); }

    int compare(const Slice& other) const {
        size_t min_len = std::min(size(), other.size());
        int r = std::memcmp(data(), other.data(), min_len);
        if (r != 0) return r;
        if (size() < other.size()) return -1;
        if (size() > other.size()) return 1;
        return 0;
    }

    bool starts_with(const Slice& prefix) const {
        return size() >= prefix.size()
            && std::memcmp(data(), prefix.data(), prefix.size()) == 0;
    }

    bool operator==(const Slice& other) const {
        return compare(other) == 0;
    }

    bool operator!=(const Slice& other) const {
        return !(*this == other);
    }

private:
    std::string_view view_;

};

}   // namespace mini_leveldb

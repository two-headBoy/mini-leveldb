#pragma once

#include <string>

#include "mini-leveldb/slice.h"

namespace mini_leveldb
{

class Status {
public:
    Status() noexcept : code_(Code::kOk) {}
    ~Status() = default;

    Status(const Status& rhs) = default;
    Status& operator=(const Status& rhs) = default;
    Status(Status&& rhs) noexcept = default;
    Status& operator=(Status&& rhs) noexcept = default;

    static Status OK() { return Status(); }
    static Status NotFound(const Slice& msg, const Slice& msg2 = Slice()) {
        return Status(kNotFound, msg, msg2);
    }
    static Status Corruption(const Slice& msg, const Slice& msg2 = Slice()) {
        return Status(kCorruption, msg, msg2);
    }
    static Status NotSupported(const Slice& msg, const Slice& msg2 = Slice()) {
        return Status(kNotSupported, msg, msg2);
    }
    static Status InvalidArgument(const Slice& msg, const Slice& msg2 = Slice()) {
        return Status(kInvalidArgument, msg, msg2);
    }
    static Status IOError(const Slice& msg, const Slice& msg2 = Slice()) {
        return Status(kIOError, msg, msg2);
    }

    bool ok() const { return code_ == kOk; }
    bool IsNotFound() const { return code_ == kNotFound; }
    bool IsCorruption() const { return code_ == kCorruption; }
    bool IsIOError() const { return code_ == kIOError; }
    bool IsNotSupportedError() const { return code_ == kNotSupported; }
    bool IsInvalidArgument() const { return code_ == kInvalidArgument; }

    std::string ToString() const;

private:
    enum Code {
        kOk = 0,
        kNotFound = 1,
        kCorruption = 2,
        kNotSupported = 3,
        kInvalidArgument = 4,
        kIOError = 5
    };

    Status(Code code, const Slice& msg, const Slice& msg2);

    Code code_;
    std::string msg_;

};

}   // namespace mini_leveldb

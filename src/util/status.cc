#include "mini-leveldb/status.h"

#include <cstdint>
#include <cassert>

namespace mini_leveldb
{

Status::Status(Code code, const Slice& msg, const Slice& msg2)
    : code_(code)
{
    assert(code != kOk);
    msg_.assign(msg.data(), msg.size());
    if (!msg2.empty())
    {
        msg_ += ": ";
        msg_.append(msg2.data(), msg2.size());
    }
}

std::string Status::ToString() const
{
    if (ok())
    {
        return "OK";
    }

    std::string result;
    
    switch (code_)
    {
    case kOk:
        result = "OK";
        break;
    case kNotFound:
        result = "NotFound: ";
        break;
    case kCorruption:
        result = "Corruption: ";
        break;
    case kNotSupported:
        result = "Not implemented: ";
        break;
    case kInvalidArgument:
        result = "Invalid argument: ";
        break;
    case kIOError:
        result = "IO error: ";
        break;
    default:
        char buf[30];
        std::snprintf(buf, sizeof(buf), "Unknown code(%d): ", static_cast<int>(code_));
        result = buf;
        break;
    }
    result.append(msg_);
    return result;
}

}   // namespace mini_leveldb

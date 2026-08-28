#include "table/format.h"

#include "util/coding.h"
#include "util/crc32c.h"

namespace mini_leveldb
{

void BlockHandle::EncodeTo(std::string* dst) const {
    PutVarint64(dst, offset_);
    PutVarint64(dst, size_);
}

Status BlockHandle::DecodeFrom(Slice* input) {
    if (!GetVarint64(input, &offset_) || !GetVarint64(input, &size_)) {
        return Status::Corruption("bad block handle");
    }
    return Status::OK();
}

void Footer::EncodeTo(std::string* dst) const {
    const size_t original_size = dst->size();
    metaindex_handle_.EncodeTo(dst);
    index_handle_.EncodeTo(dst);
    dst->resize(original_size + 2 * BlockHandle::kMaxEncodedLength);   // 补零到定长
    char magic[8];
    EncodeFixed32(magic, static_cast<uint32_t>(kTableMagicNumber & 0xffffffffu));
    EncodeFixed32(magic + 4, static_cast<uint32_t>(kTableMagicNumber >> 32));
    dst->append(magic, 8);
}

Status Footer::DecodeFrom(Slice* input) {
    if (input->size() < kEncodedLength) {
        return Status::Corruption("not an sstable (too small)");
    }
    const char* magic_ptr = input->data() + kEncodedLength - 8;
    if (DecodeFixed64(magic_ptr) != kTableMagicNumber) {
        return Status::Corruption("bad magic number");
    }
    Status s = metaindex_handle_.DecodeFrom(input);
    if (s.ok()) s = index_handle_.DecodeFrom(input);
    if (s.ok()) input->remove_prefix(kEncodedLength);
    return s;
}

Status WriteBlock(std::FILE* file, const Slice& contents, BlockHandle* handle) {
    const long off = std::ftell(file);
    if (off < 0) {
        return Status::IOError("ftell failed");
    }
    handle->set_offset(static_cast<uint64_t>(off));
    handle->set_size(contents.size());

    if (std::fwrite(contents.data(), 1, contents.size(), file) !=
        contents.size()) {
        return Status::IOError("while writing block");
    }

    // 尾部：1B 压缩标记 + 4B 掩码 CRC（覆盖标记位和数据）
    const uint8_t type = kNoCompression;
    char trailer[kBlockTrailerSize];
    trailer[0] = static_cast<char>(type);
    uint32_t crc = Value(reinterpret_cast<const char*>(&type), 1);
    crc = Extend(crc, contents.data(), contents.size());
    EncodeFixed32(trailer + 1, MaskCrc32c(crc));

    if (std::fwrite(trailer, 1, kBlockTrailerSize, file) != kBlockTrailerSize) {
        return Status::IOError("while writing block trailer");
    }
    return Status::OK();
}

Status ReadBlock(std::FILE* file, const BlockHandle& handle,
                 std::string* scratch, Slice* result) {
    const size_t n = static_cast<size_t>(handle.size());
    if (std::fseek(file, static_cast<long>(handle.offset()), SEEK_SET) != 0) {
        return Status::IOError("while seeking to block");
    }
    scratch->resize(n + kBlockTrailerSize);
    if (std::fread(&(*scratch)[0], 1, n + kBlockTrailerSize, file) !=
        n + kBlockTrailerSize) {
        return Status::IOError("while reading block");
    }

    const uint8_t type = static_cast<uint8_t>((*scratch)[n]);
    if (type != kNoCompression) {
        return Status::Corruption("bad block type");
    }
    uint32_t crc = Value(reinterpret_cast<const char*>(&type), 1);
    crc = Extend(crc, scratch->data(), n);
    if (UnmaskCrc32c(DecodeFixed32(scratch->data() + n + 1)) != crc) {
        return Status::Corruption("block checksum mismatch");
    }

    *result = Slice(scratch->data(), n);
    return Status::OK();
}

}   // namespace mini_leveldb

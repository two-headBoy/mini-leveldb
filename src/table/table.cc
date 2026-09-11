#include "table/table.h"

#include <cstring>

#include "db/dbformat.h"
#include "util/coding.h"

namespace mini_leveldb
{

Table::Table(std::FILE* file, const Slice& index_data)
    : file_(file),
      index_block_data_(index_data.data(), index_data.size()),
      index_block_(Slice(index_block_data_)) {}

Table::~Table() {
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
}

Status Table::Open(const std::string& fname, Table** table) {
    *table = nullptr;

    std::FILE* file = std::fopen(fname.c_str(), "rb");
    if (file == nullptr) {
        return Status::IOError("open table failed: " + fname);
    }

    // 读文件尾定长 Footer
    std::fseek(file, 0, SEEK_END);
    const long file_size = std::ftell(file);
    if (file_size < Footer::kEncodedLength) {
        std::fclose(file);
        return Status::Corruption("file too small to be sstable");
    }
    std::fseek(file, file_size - Footer::kEncodedLength, SEEK_SET);

    char footer_buf[Footer::kEncodedLength];
    if (std::fread(footer_buf, 1, Footer::kEncodedLength, file) !=
        Footer::kEncodedLength) {
        std::fclose(file);
        return Status::IOError("while reading footer");
    }

    Slice footer_slice(footer_buf, Footer::kEncodedLength);
    Footer footer;
    Status s = footer.DecodeFrom(&footer_slice);
    if (!s.ok()) {
        std::fclose(file);
        return s;
    }

    // 读索引块
    std::string index_scratch;
    Slice index_data;
    s = ReadBlock(file, footer.index_handle(), &index_scratch, &index_data);
    if (!s.ok()) {
        std::fclose(file);
        return s;
    }

    *table = new Table(file, index_data);
    return Status::OK();
}

LookupState Table::Get(const Slice& user_key, std::string* value) {
    // 第一层：索引块二分，定位 data block
    auto index_it = index_block_.NewIterator();
    index_it.Seek(user_key);
    if (!index_it.Valid()) {
        return LookupState::kNotFound;   // target 比所有块的 max key 都大
    }

    // 解码索引条目的 value（BlockHandle 编码）
    Slice handle_encoded = index_it.value();
    BlockHandle data_handle;
    if (!data_handle.DecodeFrom(&handle_encoded).ok()) {
        return LookupState::kNotFound;
    }

    // 读 data block
    std::string data_scratch;
    Slice data_block_contents;
    Status s = ReadBlock(file_, data_handle, &data_scratch, &data_block_contents);
    if (!s.ok()) {
        return LookupState::kNotFound;
    }

    // 第二层：data block 二分查 target
    Block data_block(data_block_contents);
    auto data_it = data_block.NewIterator();
    data_it.Seek(user_key);
    if (!data_it.Valid()) {
        return LookupState::kNotFound;
    }

    // 校验 user_key 命中，排除"落到下一个 key"的情况
    Slice ikey = data_it.key();
    if (ExtractUserKey(ikey) != user_key) {
        return LookupState::kNotFound;
    }

    const ValueType type =
        static_cast<ValueType>(ExtractTag(ikey) & 0xff);
    if (type != kTypeValue) {
        return LookupState::kDeleted;   // 本表最新版本是墓碑，必须上报
    }

    value->assign(data_it.value().data(), data_it.value().size());
    return LookupState::kValue;
}

}   // namespace mini_leveldb

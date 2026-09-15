#include "table/table_builder.h"

#include <cstring>

#include "util/coding.h"

namespace mini_leveldb {

TableBuilder::TableBuilder(std::FILE* file) : file_(file) {}

TableBuilder::~TableBuilder() {
    if (file_ != nullptr) {
        std::fflush(file_);
        std::fclose(file_);
        file_ = nullptr;
    }
}

Status TableBuilder::Add(const Slice& key, const Slice& value) {
    if (finished_) return Status::InvalidArgument("table already finished");

    data_block_builder_.Add(key, value);
    last_key_.assign(key.data(), key.size());

    if (data_block_builder_.CurrentSizeEstimate() >= kBlockSize) {
        return FlushDataBlock();
    }
    return Status::OK();
}

Status TableBuilder::FlushDataBlock() {
    if (data_block_builder_.empty()) return Status::OK();

    Slice block_contents = data_block_builder_.Finish();
    BlockHandle handle;
    Status s = WriteBlock(file_, block_contents, &handle);
    if (!s.ok()) return s;

    file_size_ += block_contents.size() + kBlockTrailerSize;

    // 索引条目：key = 该数据块最后一条 key，value = BlockHandle 编码
    std::string handle_encoded;
    handle.EncodeTo(&handle_encoded);
    index_block_builder_.Add(last_key_, Slice(handle_encoded));

    data_block_builder_.Reset();
    return Status::OK();
}

Status TableBuilder::Finish() {
    if (finished_) return Status::InvalidArgument("table already finished");

    // 1. flush 最后一块数据
    Status s = FlushDataBlock();
    if (!s.ok()) return s;

    // 2. 写索引块
    Slice index_contents = index_block_builder_.Finish();
    BlockHandle index_handle;
    s = WriteBlock(file_, index_contents, &index_handle);
    if (!s.ok()) return s;
    file_size_ += index_contents.size() + kBlockTrailerSize;

    // 3. 写 Footer（metaindex 暂空，指向 index block）
    Footer footer;
    footer.set_index_handle(index_handle);
    std::string footer_buf;
    footer.EncodeTo(&footer_buf);
    if (std::fwrite(footer_buf.data(), 1, footer_buf.size(), file_) !=
        footer_buf.size()) {
        return Status::IOError("while writing footer");
    }
    file_size_ += footer_buf.size();

    std::fflush(file_);
    finished_ = true;
    return Status::OK();
}

}  // namespace mini_leveldb

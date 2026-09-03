#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "mini-leveldb/write_batch.h"

using namespace mini_leveldb;

// 测试用 Handler：收集回放记录
struct Record {
    enum Op { kPut, kDelete } op;
    uint64_t seq;
    std::string key;
    std::string value;
};

class CollectingHandler : public WriteBatch::Handler {
public:
    void Put(uint64_t seq, const Slice& key, const Slice& value) override {
        records_.push_back({Record::kPut, seq, key.ToString(), value.ToString()});
    }
    void Delete(uint64_t seq, const Slice& key) override {
        records_.push_back({Record::kDelete, seq, key.ToString(), std::string()});
    }
    const std::vector<Record>& records() const { return records_; }

private:
    std::vector<Record> records_;
};

// 空 batch 默认状态
TEST(WriteBatchTest, EmptyBatch) {
    WriteBatch batch;
    EXPECT_EQ(batch.Count(), 0);
    EXPECT_EQ(batch.sequence(), 0);
}

// Put/Delete 后 Count 正确
TEST(WriteBatchTest, CountAfterOps) {
    WriteBatch batch;
    batch.Put("a", "1");
    batch.Put("b", "2");
    batch.Delete("c");
    EXPECT_EQ(batch.Count(), 3);
}

// Clear 重置
TEST(WriteBatchTest, ClearResets) {
    WriteBatch batch;
    batch.Put("a", "1");
    batch.Delete("b");
    EXPECT_EQ(batch.Count(), 2);

    batch.Clear();
    EXPECT_EQ(batch.Count(), 0);
}

// sequence 存取
TEST(WriteBatchTest, SequenceAccessors) {
    WriteBatch batch;
    batch.set_sequence(100);
    EXPECT_EQ(batch.sequence(), 100);
}

// 编码 → SetContents → Iterate 往返，seq 逐条递增
TEST(WriteBatchTest, RoundTripWithSeqIncrement) {
    WriteBatch batch;
    batch.set_sequence(42);
    batch.Put("alice", "100");
    batch.Delete("bob");
    batch.Put("charlie", "300");

    WriteBatch restored;
    ASSERT_TRUE(restored.SetContents(batch.Data()).ok());
    EXPECT_EQ(restored.Count(), 3);
    EXPECT_EQ(restored.sequence(), 42);

    CollectingHandler handler;
    ASSERT_TRUE(restored.Iterate(&handler).ok());
    const auto& recs = handler.records();
    ASSERT_EQ(recs.size(), 3);

    EXPECT_EQ(recs[0].op, Record::kPut);
    EXPECT_EQ(recs[0].seq, 42u);
    EXPECT_EQ(recs[0].key, "alice");
    EXPECT_EQ(recs[0].value, "100");

    EXPECT_EQ(recs[1].op, Record::kDelete);
    EXPECT_EQ(recs[1].seq, 43u);
    EXPECT_EQ(recs[1].key, "bob");

    EXPECT_EQ(recs[2].op, Record::kPut);
    EXPECT_EQ(recs[2].seq, 44u);
    EXPECT_EQ(recs[2].key, "charlie");
    EXPECT_EQ(recs[2].value, "300");
}

// value 含特殊字符（二进制数据）
TEST(WriteBatchTest, BinaryValues) {
    WriteBatch batch;
    std::string binary_value;
    binary_value.push_back('\x00');
    binary_value.push_back('\xff');
    binary_value.push_back('\x01');
    batch.Put("key", binary_value);

    WriteBatch restored;
    ASSERT_TRUE(restored.SetContents(batch.Data()).ok());
    CollectingHandler handler;
    ASSERT_TRUE(restored.Iterate(&handler).ok());
    ASSERT_EQ(handler.records().size(), 1);
    EXPECT_EQ(handler.records()[0].value, binary_value);
}

// 空 key / 空 value 边界
TEST(WriteBatchTest, EmptyKeyValue) {
    WriteBatch batch;
    batch.Put(Slice(), Slice());
    batch.Delete(Slice());

    WriteBatch restored;
    ASSERT_TRUE(restored.SetContents(batch.Data()).ok());
    CollectingHandler handler;
    ASSERT_TRUE(restored.Iterate(&handler).ok());
    ASSERT_EQ(handler.records().size(), 2);
    EXPECT_EQ(handler.records()[0].key, "");
    EXPECT_EQ(handler.records()[0].value, "");
    EXPECT_EQ(handler.records()[1].op, Record::kDelete);
}

// 内容太短拒绝
TEST(WriteBatchTest, RejectTooSmall) {
    WriteBatch batch;
    Status s = batch.SetContents(Slice("short"));
    EXPECT_FALSE(s.ok());
    EXPECT_TRUE(s.IsCorruption());
}

// count 配额校验：声明数 < 实际记录数
TEST(WriteBatchTest, RejectTooManyRecords) {
    WriteBatch batch;
    batch.set_sequence(1);
    batch.Put("a", "1");
    batch.Put("b", "2");
    // 篡改 count 为 1（实际 2 条）
    std::string data = batch.Data();
    // count 在 offset 8，4B
    data[8] = 1; data[9] = 0; data[10] = 0; data[11] = 0;

    WriteBatch restored;
    ASSERT_TRUE(restored.SetContents(data).ok());
    CollectingHandler handler;
    Status s = restored.Iterate(&handler);
    EXPECT_FALSE(s.ok());
    EXPECT_TRUE(s.IsCorruption());
}

// count 配额校验：声明数 > 实际记录数
TEST(WriteBatchTest, RejectTooFewRecords) {
    WriteBatch batch;
    batch.set_sequence(1);
    batch.Put("a", "1");
    // 篡改 count 为 5
    std::string data = batch.Data();
    data[8] = 5; data[9] = 0; data[10] = 0; data[11] = 0;

    WriteBatch restored;
    ASSERT_TRUE(restored.SetContents(data).ok());
    CollectingHandler handler;
    Status s = restored.Iterate(&handler);
    EXPECT_FALSE(s.ok());
    EXPECT_TRUE(s.IsCorruption());
}

// 未知 tag 拒绝
TEST(WriteBatchTest, RejectUnknownTag) {
    WriteBatch batch;
    batch.set_sequence(1);
    batch.Put("a", "1");
    std::string data = batch.Data();
    // 篡改第一条记录的 tag（offset 12 是记录区起点）
    data[12] = 0x99;

    WriteBatch restored;
    ASSERT_TRUE(restored.SetContents(data).ok());
    CollectingHandler handler;
    Status s = restored.Iterate(&handler);
    EXPECT_FALSE(s.ok());
    EXPECT_TRUE(s.IsCorruption());
}

// SetContents 大 batch 往返
TEST(WriteBatchTest, LargeBatchRoundTrip) {
    WriteBatch batch;
    batch.set_sequence(1000);
    const int kCount = 1000;
    for (int i = 0; i < kCount; i++) {
        char k[32], v[32];
        std::snprintf(k, sizeof(k), "key%04d", i);
        std::snprintf(v, sizeof(v), "val%04d", i);
        batch.Put(k, v);
    }

    WriteBatch restored;
    ASSERT_TRUE(restored.SetContents(batch.Data()).ok());
    EXPECT_EQ(restored.Count(), kCount);
    CollectingHandler handler;
    ASSERT_TRUE(restored.Iterate(&handler).ok());
    ASSERT_EQ(handler.records().size(), kCount);
    for (int i = 0; i < kCount; i++) {
        char k[32], v[32];
        std::snprintf(k, sizeof(k), "key%04d", i);
        std::snprintf(v, sizeof(v), "val%04d", i);
        EXPECT_EQ(handler.records()[i].seq, 1000u + i);
        EXPECT_EQ(handler.records()[i].key, k);
        EXPECT_EQ(handler.records()[i].value, v);
    }
}

#include "table/block.h"

#include <gtest/gtest.h>
#include <cstdio>

#include "table/block_builder.h"

using namespace mini_leveldb;

// 最小往返：3 条数据，顺序遍历
TEST(BlockTest, SmallRoundTrip) {
    BlockBuilder bb;
    bb.Add("alice", "100");
    bb.Add("bob", "200");
    bb.Add("charlie", "300");
    Slice block_data = bb.Finish();

    Block block(block_data);
    auto it = block.NewIterator();

    it.SeekToFirst();
    ASSERT_TRUE(it.Valid());
    EXPECT_EQ(it.key().ToString(), "alice");
    EXPECT_EQ(it.value().ToString(), "100");

    it.Next();
    ASSERT_TRUE(it.Valid());
    EXPECT_EQ(it.key().ToString(), "bob");
    EXPECT_EQ(it.value().ToString(), "200");

    it.Next();
    ASSERT_TRUE(it.Valid());
    EXPECT_EQ(it.key().ToString(), "charlie");
    EXPECT_EQ(it.value().ToString(), "300");

    it.Next();
    EXPECT_FALSE(it.Valid());
}

// Seek 精确命中：开头、中间、结尾
TEST(BlockTest, SeekHit) {
    BlockBuilder bb;
    for (int i = 0; i < 100; i++) {
        char k[32], v[32];
        std::snprintf(k, sizeof(k), "key%03d", i);
        std::snprintf(v, sizeof(v), "val%03d", i);
        bb.Add(k, v);
    }
    Block block(bb.Finish());
    auto it = block.NewIterator();

    it.Seek("key050");
    ASSERT_TRUE(it.Valid());
    EXPECT_EQ(it.key().ToString(), "key050");
    EXPECT_EQ(it.value().ToString(), "val050");

    it.Seek("key000");
    EXPECT_EQ(it.key().ToString(), "key000");

    it.Seek("key099");
    EXPECT_EQ(it.key().ToString(), "key099");
}

// Seek 未命中：落到下一个 key，或全表最大返回 invalid
TEST(BlockTest, SeekMiss) {
    BlockBuilder bb;
    for (int i = 0; i < 100; i++) {
        char k[32];
        std::snprintf(k, sizeof(k), "key%03d", i);
        bb.Add(k, "v");
    }
    Block block(bb.Finish());
    auto it = block.NewIterator();

    // 不存在的 key，应停在第一个 >= target 的
    it.Seek("key050XXX");
    ASSERT_TRUE(it.Valid());
    EXPECT_EQ(it.key().ToString(), "key051");

    // 比所有 key 都小
    it.Seek("aaa");
    EXPECT_EQ(it.key().ToString(), "key000");

    // 比所有 key 都大
    it.Seek("zzz");
    EXPECT_FALSE(it.Valid());
}

// 跨 restart point 的前缀压缩：还原正确 + Seek 正确 + 压缩确实生效
TEST(BlockTest, PrefixCompressionAcrossRestarts) {
    BlockBuilder bb;
    for (int i = 0; i < 200; i++) {
        char k[64], v[64];
        std::snprintf(k, sizeof(k), "user_profile_data_key_%04d", i);
        std::snprintf(v, sizeof(v), "value_number_%04d", i);
        bb.Add(k, v);
    }
    Slice block_data = bb.Finish();
    Block block(block_data);
    auto it = block.NewIterator();

    // 顺序遍历验证 key 还原正确
    it.SeekToFirst();
    for (int i = 0; i < 200; i++) {
        ASSERT_TRUE(it.Valid());
        char expected_k[64], expected_v[64];
        std::snprintf(expected_k, sizeof(expected_k),
                      "user_profile_data_key_%04d", i);
        std::snprintf(expected_v, sizeof(expected_v), "value_number_%04d", i);
        EXPECT_EQ(it.key().ToString(), expected_k);
        EXPECT_EQ(it.value().ToString(), expected_v);
        it.Next();
    }
    EXPECT_FALSE(it.Valid());

    // 跨 restart 区间 Seek（restart 在 0,16,32,... 处）
    it.Seek("user_profile_data_key_0050");
    ASSERT_TRUE(it.Valid());
    EXPECT_EQ(it.key().ToString(), "user_profile_data_key_0050");

    it.Seek("user_profile_data_key_0160");
    EXPECT_EQ(it.key().ToString(), "user_profile_data_key_0160");

    // 验证压缩确实生效
    size_t full_size = 200 * (30 + 20);
    EXPECT_LT(block_data.size(), full_size);
}

// 空块边界：所有操作都应 invalid
TEST(BlockTest, EmptyBlock) {
    BlockBuilder bb;
    Slice data = bb.Finish();
    Block block(data);
    auto it = block.NewIterator();
    EXPECT_FALSE(it.Valid());
    it.SeekToFirst();
    EXPECT_FALSE(it.Valid());
    it.Seek("anything");
    EXPECT_FALSE(it.Valid());
}

// Reset 后 BlockBuilder 可重新使用
TEST(BlockTest, Reset) {
    BlockBuilder bb;
    bb.Add("a", "1");
    bb.Finish();

    bb.Reset();
    EXPECT_TRUE(bb.empty());

    bb.Add("x", "9");
    Slice data = bb.Finish();
    Block block(data);
    auto it = block.NewIterator();
    it.SeekToFirst();
    ASSERT_TRUE(it.Valid());
    EXPECT_EQ(it.key().ToString(), "x");
    EXPECT_EQ(it.value().ToString(), "9");
}

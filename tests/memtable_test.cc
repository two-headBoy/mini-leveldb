#include <gtest/gtest.h>

#include <string>

#include "db/dbformat.h"
#include "db/memtable.h"

using namespace mini_leveldb;

// 基础往返：单条 Put 后 Get 命中
TEST(MemTableTest, BasicAddGet) {
    MemTable mem;
    mem.Add(1, kTypeValue, "alice", "100");

    std::string value;
    EXPECT_TRUE(mem.Get("alice", &value));
    EXPECT_EQ(value, "100");
}

// 多 key 互不干扰
TEST(MemTableTest, MultipleKeys) {
    MemTable mem;
    mem.Add(1, kTypeValue, "a", "1");
    mem.Add(2, kTypeValue, "b", "2");
    mem.Add(3, kTypeValue, "c", "3");

    std::string value;
    ASSERT_TRUE(mem.Get("a", &value));
    EXPECT_EQ(value, "1");
    ASSERT_TRUE(mem.Get("b", &value));
    EXPECT_EQ(value, "2");
    ASSERT_TRUE(mem.Get("c", &value));
    EXPECT_EQ(value, "3");
    EXPECT_FALSE(mem.Get("d", &value));
}

// 同 key 高 seq 覆盖低 seq
TEST(MemTableTest, OverwriteHigherSeqWins) {
    MemTable mem;
    mem.Add(1, kTypeValue, "key", "old");
    mem.Add(2, kTypeValue, "key", "new");
    mem.Add(3, kTypeValue, "key", "newer");

    std::string value;
    ASSERT_TRUE(mem.Get("key", &value));
    EXPECT_EQ(value, "newer");
}

// 低 seq 不应覆盖高 seq
TEST(MemTableTest, LowerSeqDoesNotOverride) {
    MemTable mem;
    mem.Add(5, kTypeValue, "key", "high");
    mem.Add(3, kTypeValue, "key", "low");

    std::string value;
    ASSERT_TRUE(mem.Get("key", &value));
    EXPECT_EQ(value, "high");
}

// tombstone 表现为未命中
TEST(MemTableTest, TombstoneReturnsNotFound) {
    MemTable mem;
    mem.Add(1, kTypeValue, "key", "value");
    mem.Add(2, kTypeDeletion, "key", Slice());

    std::string value;
    EXPECT_FALSE(mem.Get("key", &value));
}

// tombstone 不影响其他 key
TEST(MemTableTest, TombstoneOnlyAffectsSameKey) {
    MemTable mem;
    mem.Add(1, kTypeValue, "alice", "100");
    mem.Add(2, kTypeDeletion, "bob", Slice());

    std::string value;
    ASSERT_TRUE(mem.Get("alice", &value));
    EXPECT_EQ(value, "100");
    EXPECT_FALSE(mem.Get("bob", &value));
}

// 空 value 边界
TEST(MemTableTest, EmptyValue) {
    MemTable mem;
    mem.Add(1, kTypeValue, "key", Slice());

    std::string value = "stale";
    ASSERT_TRUE(mem.Get("key", &value));
    EXPECT_TRUE(value.empty());
}

// 空 key 边界
TEST(MemTableTest, EmptyKey) {
    MemTable mem;
    mem.Add(1, kTypeValue, Slice(), "value");

    std::string value;
    ASSERT_TRUE(mem.Get(Slice(), &value));
    EXPECT_EQ(value, "value");
}

// ApproximateMemoryUsage 随写入增长
TEST(MemTableTest, MemoryUsageGrows) {
    MemTable mem;
    size_t before = mem.ApproximateMemoryUsage();

    for (int i = 0; i < 1000; i++) {
        char k[32], v[32];
        std::snprintf(k, sizeof(k), "key%05d", i);
        std::snprintf(v, sizeof(v), "val%05d", i);
        mem.Add(i + 1, kTypeValue, k, v);
    }

    EXPECT_GT(mem.ApproximateMemoryUsage(), before);
}

// 批量写入 + 随机读验证一致性
TEST(MemTableTest, BatchWriteAndRandomRead) {
    MemTable mem;
    const int kCount = 5000;
    for (int i = 0; i < kCount; i++) {
        char k[32], v[64];
        std::snprintf(k, sizeof(k), "user_key_%04d", i);
        std::snprintf(v, sizeof(v), "user_value_%04d_padding", i);
        mem.Add(i + 1, kTypeValue, k, v);
    }

    std::string value;
    for (int i = 0; i < kCount; i++) {
        char k[32], expected[64];
        std::snprintf(k, sizeof(k), "user_key_%04d", i);
        std::snprintf(expected, sizeof(expected), "user_value_%04d_padding", i);
        ASSERT_TRUE(mem.Get(k, &value));
        EXPECT_EQ(value, expected);
    }
}

// tombstone 后高 seq Put 可见：删除后重新写入
TEST(MemTableTest, TombstoneThenHigherSeqPutVisible) {
    MemTable mem;
    mem.Add(1, kTypeValue, "key", "old");
    mem.Add(2, kTypeDeletion, "key", Slice());   // 删除
    mem.Add(3, kTypeValue, "key", "new");         // 重新写入，seq 更大

    std::string value;
    ASSERT_TRUE(mem.Get("key", &value));
    EXPECT_EQ(value, "new");
}

// tombstone 后低 seq Put 仍不可见：旧版本写入不能复活
TEST(MemTableTest, TombstoneThenLowerSeqPutInvisible) {
    MemTable mem;
    mem.Add(3, kTypeValue, "key", "high");
    mem.Add(5, kTypeDeletion, "key", Slice());    // 删除（seq=5）
    mem.Add(4, kTypeValue, "key", "stale");       // 旧版本写入，seq 更低

    std::string value;
    // seq=5 的 tombstone 仍最新，应不可见
    EXPECT_FALSE(mem.Get("key", &value));
}

// 中间版本被跳过：只看到最新和最旧，中间版本不影响
TEST(MemTableTest, IntermediateVersionsDoNotAffectGet) {
    MemTable mem;
    mem.Add(1, kTypeValue, "key", "v1");
    mem.Add(2, kTypeValue, "key", "v2");
    mem.Add(3, kTypeValue, "key", "v3");
    mem.Add(4, kTypeDeletion, "key", Slice());
    mem.Add(5, kTypeValue, "key", "final");

    std::string value;
    ASSERT_TRUE(mem.Get("key", &value));
    EXPECT_EQ(value, "final");
}

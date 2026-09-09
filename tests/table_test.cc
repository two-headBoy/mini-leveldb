#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>
#include <vector>

#include "db/dbformat.h"
#include "table/table.h"
#include "table/table_builder.h"

using namespace mini_leveldb;

namespace {

struct Entry {
    std::string user_key;
    SequenceNumber seq;
    ValueType type;
    std::string value;
};

// mkstemp 生成真实路径（Table::Open 按文件名打开，不能用 tmpfile）
std::string TempFilePath() {
    char tmpl[] = "/tmp/mini_table_test_XXXXXX";
    int fd = ::mkstemp(tmpl);
    if (fd >= 0) ::close(fd);
    return std::string(tmpl);
}

std::string IKey(const std::string& user, SequenceNumber seq, ValueType t) {
    std::string k;
    AppendInternalKey(&k, user, seq, t);
    return k;
}

// 按 InternalKeyComparator 顺序（user 升序、同 user 高 seq 在前）喂给 builder，
// 落盘 Finish 后返回文件路径
std::string BuildTable(const std::vector<Entry>& entries) {
    std::string path = TempFilePath();
    std::FILE* f = std::fopen(path.c_str(), "wb");
    EXPECT_NE(f, nullptr);
    {
        TableBuilder builder(f);
        for (const auto& e : entries) {
            EXPECT_TRUE(builder.Add(IKey(e.user_key, e.seq, e.type),
                                    e.value).ok());
        }
        EXPECT_TRUE(builder.Finish().ok());
        EXPECT_GT(builder.FileSize(), 0u);
    }   // 析构 fflush + fclose
    return path;
}

}  // namespace

// 最小往返：3 条数据，重开文件逐个 Get
TEST(TableTest, SmallRoundTrip) {
    std::string path = BuildTable({
        {"alice", 1, kTypeValue, "100"},
        {"bob", 1, kTypeValue, "200"},
        {"charlie", 1, kTypeValue, "300"},
    });

    Table* table = nullptr;
    ASSERT_TRUE(Table::Open(path, &table).ok());
    ASSERT_NE(table, nullptr);

    std::string value;
    EXPECT_TRUE(table->Get("alice", &value));
    EXPECT_EQ(value, "100");
    EXPECT_TRUE(table->Get("charlie", &value));
    EXPECT_EQ(value, "300");

    // 未命中：不存在的 key、比所有 key 小、比所有 key 大
    EXPECT_FALSE(table->Get("dave", &value));
    EXPECT_FALSE(table->Get("aaa", &value));
    EXPECT_FALSE(table->Get("zzz", &value));

    delete table;
    std::remove(path.c_str());
}

// 验收硬指标：10 万条跨数百个 data block，重开后全部读回
TEST(TableTest, LargeRoundTrip100K) {
    constexpr int kN = 100000;
    std::vector<Entry> entries;
    entries.reserve(kN);
    for (int i = 0; i < kN; i++) {
        char k[32], v[32];
        std::snprintf(k, sizeof(k), "key%06d", i);
        std::snprintf(v, sizeof(v), "val%06d", i);
        entries.push_back({k, 1, kTypeValue, v});
    }
    std::string path = BuildTable(entries);

    Table* table = nullptr;
    ASSERT_TRUE(Table::Open(path, &table).ok());

    std::string value;
    for (int i = 0; i < kN; i++) {
        char k[32], v[32];
        std::snprintf(k, sizeof(k), "key%06d", i);
        std::snprintf(v, sizeof(v), "val%06d", i);
        ASSERT_TRUE(table->Get(k, &value)) << "missing: " << k;
        EXPECT_EQ(value, v);
    }
    // 边界外未命中
    EXPECT_FALSE(table->Get("key100000", &value));
    EXPECT_FALSE(table->Get("key00000A", &value));

    delete table;
    std::remove(path.c_str());
}

// 单条大 value（超过 4KB block 目标）不切块逻辑异常
TEST(TableTest, LargeValue) {
    std::string big(100 * 1024, 'x');
    std::string path = BuildTable({{"big", 1, kTypeValue, big},
                                   {"small", 1, kTypeValue, "s"}});

    Table* table = nullptr;
    ASSERT_TRUE(Table::Open(path, &table).ok());

    std::string value;
    EXPECT_TRUE(table->Get("big", &value));
    EXPECT_EQ(value, big);
    EXPECT_TRUE(table->Get("small", &value));
    EXPECT_EQ(value, "s");

    delete table;
    std::remove(path.c_str());
}

// tombstone：同 user key 新版本是删除标记，Get 视为未命中
TEST(TableTest, TombstoneHidesValue) {
    // 顺序：高 seq 在前（删除），低 seq 在后（旧值）
    std::string path = BuildTable({
        {"k", 10, kTypeDeletion, ""},
        {"k", 5, kTypeValue, "old"},
    });

    Table* table = nullptr;
    ASSERT_TRUE(Table::Open(path, &table).ok());

    std::string value;
    EXPECT_FALSE(table->Get("k", &value));

    delete table;
    std::remove(path.c_str());
}

// 多版本：同 user key 存在多 seq，Get 返回最新版本
TEST(TableTest, MultipleVersionsReturnNewest) {
    std::string path = BuildTable({
        {"k", 100, kTypeValue, "new"},
        {"k", 50, kTypeValue, "old"},
    });

    Table* table = nullptr;
    ASSERT_TRUE(Table::Open(path, &table).ok());

    std::string value;
    EXPECT_TRUE(table->Get("k", &value));
    EXPECT_EQ(value, "new");

    delete table;
    std::remove(path.c_str());
}

// 打开不存在的文件 → IOError，且输出指针置空
TEST(TableTest, OpenMissingFile) {
    Table* table = reinterpret_cast<Table*>(0x1);
    Status s = Table::Open("/tmp/mini_table_no_such_file_xyz", &table);
    EXPECT_FALSE(s.ok());
    EXPECT_EQ(table, nullptr);
}

// 文件小于 Footer 长度 → Corruption
TEST(TableTest, OpenTruncatedFile) {
    std::string path = TempFilePath();
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        std::fputs("short", f);
        std::fclose(f);
    }

    Table* table = nullptr;
    EXPECT_FALSE(Table::Open(path, &table).ok());
    EXPECT_EQ(table, nullptr);

    std::remove(path.c_str());
}

// Footer magic 被破坏 → Corruption
TEST(TableTest, CorruptMagic) {
    std::string path = BuildTable({
        {"a", 1, kTypeValue, "1"},
        {"b", 1, kTypeValue, "2"},
    });

    // 覆盖文件尾 8B（magic 区）
    {
        std::FILE* f = std::fopen(path.c_str(), "r+b");
        ASSERT_NE(f, nullptr);
        std::fseek(f, -8, SEEK_END);
        char zeros[8] = {0};
        std::fwrite(zeros, 1, sizeof(zeros), f);
        std::fclose(f);
    }

    Table* table = nullptr;
    Status s = Table::Open(path, &table);
    EXPECT_FALSE(s.ok());
    EXPECT_EQ(table, nullptr);

    std::remove(path.c_str());
}

// 空表：Finish 时无数据块，Open 成功但任何 Get 都未命中
TEST(TableTest, EmptyTable) {
    std::string path = BuildTable({});

    Table* table = nullptr;
    ASSERT_TRUE(Table::Open(path, &table).ok());

    std::string value;
    EXPECT_FALSE(table->Get("anything", &value));

    delete table;
    std::remove(path.c_str());
}

#include <gtest/gtest.h>

#include <cstdio>
#include <dirent.h>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "db/db_impl.h"
#include "db/filename.h"

using namespace mini_leveldb;

namespace mini_leveldb {

// 测试缝：friend 直构 DBImpl，B4 之前手动触发 flush
class DBTest {
public:
    explicit DBTest(const std::string& dir) : impl_(new DBImpl(dir)) {}
    Status Init() { return impl_->Init(); }

    Status Put(const Slice& k, const Slice& v) { return impl_->Put(k, v); }
    Status Get(const Slice& k, std::string* v) { return impl_->Get(k, v); }
    Status Flush() { return impl_->FlushMemTable(); }

private:
    std::unique_ptr<DBImpl> impl_;
};

}   // namespace mini_leveldb

namespace {

// 建唯一临时库目录（c++17 严格模式下 mkdtemp 不可见，用 pid 避碰）
std::string MakeTempDir() {
    std::string dir = "/tmp/mini_leveldb_b3_" + std::to_string(::getpid());
    if (::mkdir(dir.c_str(), 0755) != 0) {
        return {};
    }
    return dir;
}

void RemoveDir(const std::string& dir) {
    DIR* d = opendir(dir.c_str());
    if (d != nullptr) {
        struct dirent* e;
        while ((e = readdir(d)) != nullptr) {
            if (std::string(e->d_name) == "." || e->d_name == std::string("..")) {
                continue;
            }
            std::remove((dir + "/" + e->d_name).c_str());
        }
        closedir(d);
    }
    ::rmdir(dir.c_str());
}

// 按类型统计目录文件
void CountFiles(const std::string& dir, int* logs, int* tables, int* tmps) {
    *logs = *tables = *tmps = 0;
    std::vector<FileInfo> files;
    ASSERT_TRUE(ListFiles(dir, &files).ok());
    for (const auto& f : files) {
        switch (f.type) {
            case FileType::kLogFile:   ++*logs; break;
            case FileType::kTableFile: ++*tables; break;
            case FileType::kTempFile:  ++*tmps; break;
        }
    }
}

}   // namespace

// 场景⑤：flush 后旧 .log 消失、.ldb 存在、无 .tmp，编号 log < ldb < 新 log
TEST(DBTest, FlushRotatesLogAndLeavesTable) {
    const std::string dir = MakeTempDir();
    ASSERT_FALSE(dir.empty());
    {
        DBTest db(dir);
        ASSERT_TRUE(db.Init().ok());
        ASSERT_TRUE(db.Put("a", "1").ok());
        ASSERT_TRUE(db.Put("b", "2").ok());
        ASSERT_TRUE(db.Flush().ok());

        int logs, tables, tmps;
        CountFiles(dir, &logs, &tables, &tmps);
        EXPECT_EQ(logs, 1);    // 旧 log 删除，新 log 已建
        EXPECT_EQ(tables, 1);
        EXPECT_EQ(tmps, 0);

        // 新 WAL 可正常读写
        ASSERT_TRUE(db.Put("c", "3").ok());
        std::string v;
        ASSERT_TRUE(db.Get("c", &v).ok());
        EXPECT_EQ(v, "3");

        // 第二次 flush：再出一张表，WAL 继续轮换
        ASSERT_TRUE(db.Flush().ok());
        CountFiles(dir, &logs, &tables, &tmps);
        EXPECT_EQ(logs, 1);
        EXPECT_EQ(tables, 2);
        EXPECT_EQ(tmps, 0);
    }
    RemoveDir(dir);
}

// 无 flush 重开：仅靠编号 WAL 回放恢复，并能续写同一文件
TEST(DBTest, RecoverFromNumberedLog) {
    const std::string dir = MakeTempDir();
    ASSERT_FALSE(dir.empty());
    {
        DBTest db(dir);
        ASSERT_TRUE(db.Init().ok());
        ASSERT_TRUE(db.Put("k1", "v1").ok());
        ASSERT_TRUE(db.Put("k2", "v2").ok());
    }
    {
        DBTest db(dir);
        ASSERT_TRUE(db.Init().ok());
        std::string v;
        ASSERT_TRUE(db.Get("k1", &v).ok());
        EXPECT_EQ(v, "v1");
        ASSERT_TRUE(db.Get("k2", &v).ok());
        EXPECT_EQ(v, "v2");
        ASSERT_TRUE(db.Put("k3", "v3").ok());   // 续写到原 log
    }
    {
        DBTest db(dir);
        ASSERT_TRUE(db.Init().ok());
        std::string v;
        EXPECT_TRUE(db.Get("k3", &v).ok());
        EXPECT_EQ(v, "v3");
    }
    RemoveDir(dir);
}

// 空 mem flush 不应刷出空表
TEST(DBTest, FlushEmptyMemIsNoop) {
    const std::string dir = MakeTempDir();
    ASSERT_FALSE(dir.empty());
    {
        DBTest db(dir);
        ASSERT_TRUE(db.Init().ok());
        ASSERT_TRUE(db.Flush().ok());
        int logs, tables, tmps;
        CountFiles(dir, &logs, &tables, &tmps);
        EXPECT_EQ(logs, 1);
        EXPECT_EQ(tables, 0);
        EXPECT_EQ(tmps, 0);
    }
    RemoveDir(dir);
}

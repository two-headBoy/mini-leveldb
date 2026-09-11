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
    Status Delete(const Slice& k) { return impl_->Delete(k); }
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

// 场景②：写超 4MB 阈值，Put 路径自动同步 flush 出 .ldb
// 注：flush 后旧数据读回（.ldb 挂载）是 B5/B6 的断言，此处只验证自动触发与目录形态
TEST(DBTest, AutoFlushWhenMemTableFull) {
    const std::string dir = MakeTempDir();
    ASSERT_FALSE(dir.empty());
    {
        DBTest db(dir);
        ASSERT_TRUE(db.Init().ok());

        const std::string big(1024, 'x');
        int flushed_at = -1;
        for (int i = 0; i < 8000; ++i) {
            char key[16];
            std::snprintf(key, sizeof(key), "k%06d", i);
            ASSERT_TRUE(db.Put(key, big).ok());
            if (i % 64 == 0) {
                int logs, tables, tmps;
                CountFiles(dir, &logs, &tables, &tmps);
                if (tables >= 1) {
                    flushed_at = i;
                    break;
                }
            }
        }
        ASSERT_GT(flushed_at, 0) << "写入 ~8MB 仍未触发自动 flush";

        int logs, tables, tmps;
        CountFiles(dir, &logs, &tables, &tmps);
        EXPECT_GE(tables, 1);   // .ldb 已产生
        EXPECT_EQ(logs, 1);     // WAL 已轮换，始终恰好 1 个活跃
        EXPECT_EQ(tmps, 0);     // 无残留临时文件

        // flush 之后的新写入进新 mem，立即可读
        ASSERT_TRUE(db.Put("tail", "tv").ok());
        std::string v;
        ASSERT_TRUE(db.Get("tail", &v).ok());
        EXPECT_EQ(v, "tv");
    }
    RemoveDir(dir);
}

// flush 后 mem 已空，Get 必须从刚刷出的 .ldb 读回（B4 遗留断言）
TEST(DBTest, ReadBackAfterFlush) {
    const std::string dir = MakeTempDir();
    ASSERT_FALSE(dir.empty());
    {
        DBTest db(dir);
        ASSERT_TRUE(db.Init().ok());
        ASSERT_TRUE(db.Put("a", "1").ok());
        ASSERT_TRUE(db.Put("b", "2").ok());
        ASSERT_TRUE(db.Flush().ok());

        std::string v;
        ASSERT_TRUE(db.Get("a", &v).ok());
        EXPECT_EQ(v, "1");
        ASSERT_TRUE(db.Get("b", &v).ok());
        EXPECT_EQ(v, "2");
    }
    RemoveDir(dir);
}

// 场景③：同 key 跨 mem/多个 .ldb 更新，Get 恒返回最新版本
TEST(DBTest, NewestValueAcrossLayers) {
    const std::string dir = MakeTempDir();
    ASSERT_FALSE(dir.empty());
    {
        DBTest db(dir);
        ASSERT_TRUE(db.Init().ok());

        ASSERT_TRUE(db.Put("k", "v1").ok());
        ASSERT_TRUE(db.Flush().ok());          // v1 进第 1 张表
        ASSERT_TRUE(db.Put("k", "v2").ok());   // v2 留 mem
        std::string v;
        ASSERT_TRUE(db.Get("k", &v).ok());
        EXPECT_EQ(v, "v2");

        ASSERT_TRUE(db.Flush().ok());          // v2 进第 2 张表（编号更大）
        ASSERT_TRUE(db.Put("k", "v3").ok());   // v3 留 mem
        ASSERT_TRUE(db.Get("k", &v).ok());
        EXPECT_EQ(v, "v3");

        ASSERT_TRUE(db.Flush().ok());          // mem 清空，v3 只在最新表
        ASSERT_TRUE(db.Get("k", &v).ok());
        EXPECT_EQ(v, "v3");
    }
    RemoveDir(dir);
}

// 场景④：旧表有值，新层是墓碑，Get 必须 NotFound 而不是读回旧值
TEST(DBTest, TombstoneShieldsOlderTable) {
    const std::string dir = MakeTempDir();
    ASSERT_FALSE(dir.empty());
    {
        DBTest db(dir);
        ASSERT_TRUE(db.Init().ok());

        ASSERT_TRUE(db.Put("x", "secret").ok());
        ASSERT_TRUE(db.Put("keep", "alive").ok());
        ASSERT_TRUE(db.Flush().ok());          // 旧表：x=secret, keep=alive

        ASSERT_TRUE(db.Delete("x").ok());      // 墓碑在 mem
        std::string v;
        EXPECT_TRUE(db.Get("x", &v).IsNotFound());
        ASSERT_TRUE(db.Get("keep", &v).ok());  // 不误伤其他 key
        EXPECT_EQ(v, "alive");

        ASSERT_TRUE(db.Flush().ok());          // 墓碑刷进新 .ldb
        EXPECT_TRUE(db.Get("x", &v).IsNotFound());
        ASSERT_TRUE(db.Get("keep", &v).ok());
        EXPECT_EQ(v, "alive");
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

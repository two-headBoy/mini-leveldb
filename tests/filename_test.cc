#include "db/filename.h"

#include <gtest/gtest.h>
#include <unistd.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace mini_leveldb;

// 拼路径 → 解析往返，三种类型
TEST(FileNameTest, BuildAndParseRoundTrip) {
    uint64_t n = 0;
    FileType t = FileType::kLogFile;

    EXPECT_TRUE(ParseFileName(LogFileName("/tmp/db", 7), &n, &t));
    EXPECT_EQ(n, 7u);
    EXPECT_EQ(t, FileType::kLogFile);

    EXPECT_TRUE(ParseFileName(TableFileName("/tmp/db", 42), &n, &t));
    EXPECT_EQ(n, 42u);
    EXPECT_EQ(t, FileType::kTableFile);

    EXPECT_TRUE(ParseFileName(TempFileName("/tmp/db", 100), &n, &t));
    EXPECT_EQ(n, 100u);
    EXPECT_EQ(t, FileType::kTempFile);
}

// 裸名（不带路径）也能解析
TEST(FileNameTest, ParseBareName) {
    uint64_t n = 0;
    FileType t;
    EXPECT_TRUE(ParseFileName("12.ldb", &n, &t));
    EXPECT_EQ(n, 12u);
    EXPECT_EQ(t, FileType::kTableFile);
}

// 非法名全部拒绝
TEST(FileNameTest, RejectInvalid) {
    uint64_t n = 0;
    FileType t;
    EXPECT_FALSE(ParseFileName("", &n, &t));
    EXPECT_FALSE(ParseFileName("log", &n, &t));
    EXPECT_FALSE(ParseFileName("123.txt", &n, &t));
    EXPECT_FALSE(ParseFileName("12a3.log", &n, &t));
    EXPECT_FALSE(ParseFileName(".log", &n, &t));      // 裸后缀
    EXPECT_FALSE(ParseFileName(".ldb.tmp", &n, &t));  // 裸后缀
    EXPECT_FALSE(ParseFileName("random", &n, &t));
}

// 扫描目录：只收识别文件，按编号升序，忽略无关条目
TEST(FileNameTest, ListAndSort) {
    char tmpl[] = "/tmp/mini_filename_test_XXXXXX";
    char* dir = ::mkdtemp(tmpl);
    ASSERT_NE(dir, nullptr);
    const std::string dbname(dir);

    auto touch = [&](const std::string& name) {
        std::FILE* f = std::fopen((dbname + "/" + name).c_str(), "w");
        ASSERT_NE(f, nullptr);
        std::fclose(f);
    };
    touch("3.log");
    touch("1.ldb");
    touch("2.ldb.tmp");
    touch("ignore.txt");

    std::vector<FileInfo> files;
    ASSERT_TRUE(ListFiles(dbname, &files).ok());
    ASSERT_EQ(files.size(), 3u);
    EXPECT_EQ(files[0].number, 1u);
    EXPECT_EQ(files[0].type, FileType::kTableFile);
    EXPECT_EQ(files[1].number, 2u);
    EXPECT_EQ(files[1].type, FileType::kTempFile);
    EXPECT_EQ(files[2].number, 3u);
    EXPECT_EQ(files[2].type, FileType::kLogFile);

    ::unlink((dbname + "/1.ldb").c_str());
    ::unlink((dbname + "/2.ldb.tmp").c_str());
    ::unlink((dbname + "/3.log").c_str());
    ::unlink((dbname + "/ignore.txt").c_str());
    ::rmdir(dbname.c_str());
}

// 目录不存在 → IOError
TEST(FileNameTest, ListMissingDirFails) {
    std::vector<FileInfo> files;
    Status s = ListFiles("/tmp/mini_filename_no_such_dir_xyz", &files);
    EXPECT_FALSE(s.ok());
    EXPECT_TRUE(files.empty());
}

// 空目录：OK 且无结果
TEST(FileNameTest, ListEmptyDir) {
    char tmpl[] = "/tmp/mini_filename_empty_XXXXXX";
    char* dir = ::mkdtemp(tmpl);
    ASSERT_NE(dir, nullptr);
    const std::string dbname(dir);

    std::vector<FileInfo> files;
    ASSERT_TRUE(ListFiles(dbname, &files).ok());
    EXPECT_TRUE(files.empty());

    ::rmdir(dbname.c_str());
}

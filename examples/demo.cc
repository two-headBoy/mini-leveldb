// mini-leveldb 上手演示：数据落在 demo_db/（每次运行先清空，保证可复现）
// 用法：项目根目录执行 ./build/demo [db目录]，自定义目录不自动清空
//
// 四段演示：
//   1. Put/Get/Delete 基础操作：覆盖写、墓碑
//   2. 批量写入撑爆 4MB MemTable，自动刷出 .ldb、旧 .log 删除
//   3. 多层读取：MemTable 遮蔽 SSTable 旧值，墓碑截断
//   4. 关闭重开：WAL 回放 + SSTable 挂载，数据原样还在

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "mini-leveldb/db.h"

using mini_leveldb::DB;
using mini_leveldb::Status;

namespace fs = std::filesystem;

namespace {

constexpr const char* kDbDir = "demo_db";
constexpr int kBulkCount = 40000;  // 含跳表节点开销约 8MB，至少触发一次刷盘

// 演示代码不做错误恢复，失败直接退出
void Ensure(const Status& s) {
    if (!s.ok()) {
        std::cerr << "fatal: " << s.ToString() << '\n';
        std::exit(1);
    }
}

void Banner(const std::string& title) {
    std::cout << "\n========== " << title << " ==========\n";
}

// 列出 db 目录文件（按文件号升序），.log/.ldb 的生灭是刷盘的直接证据
void ListDir(const std::string& dir) {
    std::vector<std::pair<uint64_t, fs::path>> files;  // (文件号, 路径)
    for (const auto& e : fs::directory_iterator(dir)) {
        files.emplace_back(std::stoull(e.path().stem().string()), e.path());
    }
    std::sort(files.begin(), files.end());

    std::cout << dir << "/：\n";
    for (const auto& [n, path] : files) {
        const auto sz = fs::file_size(path);
        std::cout << "  " << path.filename().string() << "  "
                  << (sz + 1023) / 1024 << " KB\n";
    }
}

void ShowGet(DB* db, const std::string& key) {
    std::string value;
    const Status s = db->Get(key, &value);
    std::cout << "  Get(\"" << key << "\") -> ";
    if (s.ok()) {
        // 长值截断，避免刷屏
        std::cout << "\""
                  << (value.size() > 48 ? value.substr(0, 48) + "..." : value)
                  << "\"\n";
    } else if (s.IsNotFound()) {
        std::cout << "NotFound\n";
    } else {
        std::cout << "错误: " << s.ToString() << '\n';
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::string dbname = argc >= 2 ? argv[1] : kDbDir;

    // ---------- 1. 基础读写删 ----------
    Banner("1. Put / Get / Delete");

    DB* db = nullptr;
    Ensure(DB::Open(dbname, &db));
    Ensure(db->Put("greeting", "hello world"));
    Ensure(db->Put("counter", "1"));
    ShowGet(db, "greeting");

    Ensure(db->Put("counter", "42"));  // 覆盖写
    ShowGet(db, "counter");

    Ensure(db->Delete("greeting"));  // 逻辑删除 = 写墓碑
    ShowGet(db, "greeting");
    ShowGet(db, "never-existed");
    std::cout << '\n';
    ListDir(dbname);  // 此刻只有 .log（WAL + MemTable），尚无 .ldb

    // ---------- 2. 自动刷盘 ----------
    Banner("2. 批量写入触发 MemTable 自动刷盘");

    char k[32], v[128];
    for (int i = 0; i < kBulkCount; ++i) {
        std::snprintf(k, sizeof(k), "bulk:%06d", i);
        std::snprintf(v, sizeof(v), "value-%06d-%080d", i, i);
        Ensure(db->Put(k, v));
    }
    std::cout << "写入 " << kBulkCount
              << " 条（约 8MB），两次越过 4MB MemTable 上限\n";
    ListDir(dbname);  // 两个 .ldb 出现，旧 .log 逐个删除，新 .log 接管

    // ---------- 3. 多层读取 ----------
    Banner("3. 多层读取：MemTable -> SSTable，首命中即返回");

    ShowGet(db, "bulk:000000");                       // SSTable 里的老数据
    Ensure(db->Put("bulk:000000", "LATEST-IN-MEM"));  // 新值遮蔽表内旧值
    ShowGet(db, "bulk:000000");
    Ensure(db->Delete("bulk:000001"));  // 墓碑遮蔽 SSTable 里的值
    ShowGet(db, "bulk:000001");

    // ---------- 4. 关闭重开 ----------
    Banner("4. 关闭重开：WAL 回放 + SSTable 挂载");

    delete db;
    std::cout << "DB 已关闭，重新 Open，逐条验证：\n";
    Ensure(DB::Open(dbname, &db));
    ShowGet(db, "greeting");     // 墓碑持久化，仍 NotFound
    ShowGet(db, "counter");      // 来自 WAL 回放
    ShowGet(db, "bulk:000000");  // MemTable 新值遮蔽表内旧值
    ShowGet(db, "bulk:000001");  // 墓碑回放
    ShowGet(db, "bulk:010000");  // 直接命中 SSTable

    delete db;
    return 0;
}

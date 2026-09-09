#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mini-leveldb/status.h"

namespace mini_leveldb
{

// 文件类型；log/ldb 共用同一单调编号空间，higher-number-is-newer
enum class FileType {
    kLogFile,
    kTableFile,
    kTempFile,   // 刷盘中 {n}.ldb.tmp，rename 后转正
};

std::string LogFileName(const std::string& dbname, uint64_t number);
std::string TableFileName(const std::string& dbname, uint64_t number);
std::string TempFileName(const std::string& dbname, uint64_t number);

// 解析路径末段 "{n}.log|.ldb|.ldb.tmp"；不认识返回 false
bool ParseFileName(const std::string& fname, uint64_t* number, FileType* type);

struct FileInfo {
    uint64_t number;
    FileType type;
};

// 扫描目录列出全部识别文件，按编号升序；无法识别的条目直接忽略
Status ListFiles(const std::string& dbname, std::vector<FileInfo>* files);

}   // namespace mini_leveldb

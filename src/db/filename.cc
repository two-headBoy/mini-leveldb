#include "db/filename.h"

#include <dirent.h>

#include <algorithm>
#include <cstdlib>

namespace mini_leveldb
{

std::string LogFileName(const std::string& dbname, uint64_t number) {
    return dbname + "/" + std::to_string(number) + ".log";
}

std::string TableFileName(const std::string& dbname, uint64_t number) {
    return dbname + "/" + std::to_string(number) + ".ldb";
}

std::string TempFileName(const std::string& dbname, uint64_t number) {
    return dbname + "/" + std::to_string(number) + ".ldb.tmp";
}

bool ParseFileName(const std::string& fname, uint64_t* number, FileType* type) {
    // 只看路径末段，传全路径或裸名都行
    const size_t slash = fname.find_last_of('/');
    const std::string name =
        (slash == std::string::npos) ? fname : fname.substr(slash + 1);

    FileType t;
    size_t base_len;
    // 最长后缀先判，避免 ".ldb.tmp" 被误切成 ".ldb"
    if (name.size() > 8 && name.compare(name.size() - 8, 8, ".ldb.tmp") == 0) {
        t = FileType::kTempFile;
        base_len = name.size() - 8;
    } else if (name.size() > 4 && name.compare(name.size() - 4, 4, ".ldb") == 0) {
        t = FileType::kTableFile;
        base_len = name.size() - 4;
    } else if (name.size() > 4 && name.compare(name.size() - 4, 4, ".log") == 0) {
        t = FileType::kLogFile;
        base_len = name.size() - 4;
    } else {
        return false;
    }

    if (base_len == 0) return false;   // 裸后缀没有编号
    for (size_t i = 0; i < base_len; i++) {
        if (name[i] < '0' || name[i] > '9') return false;
    }
    *number = std::strtoull(name.c_str(), nullptr, 10);
    *type = t;
    return true;
}

Status ListFiles(const std::string& dbname, std::vector<FileInfo>* files) {
    files->clear();
    ::DIR* dir = ::opendir(dbname.c_str());
    if (dir == nullptr) {
        return Status::IOError("opendir failed: " + dbname);
    }
    while (::dirent* entry = ::readdir(dir)) {
        uint64_t number;
        FileType type;
        if (ParseFileName(entry->d_name, &number, &type)) {
            files->push_back({number, type});
        }
    }
    ::closedir(dir);
    std::sort(files->begin(), files->end(),
              [](const FileInfo& a, const FileInfo& b) {
                  return a.number < b.number;
              });
    return Status::OK();
}

}   // namespace mini_leveldb

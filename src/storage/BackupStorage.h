#pragma once

#include <filesystem>
#include <string>

#include "common/Result.h"

namespace webdisk {
namespace storage {

class BackupStorage {
public:
    virtual ~BackupStorage() = default;

    // 将本地主存储中的内容寻址文件备份到远端存储。
    virtual common::Result<void> backup_file(const std::string& hashcode, const std::filesystem::path& local_path) = 0;
};

} // namespace storage
} // namespace webdisk

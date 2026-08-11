#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "common/Result.h"

namespace webdisk {
namespace storage {

class FileStorage {
public:
    explicit FileStorage(std::filesystem::path root);

    common::Result<void> init();
    common::Result<bool> store_if_absent(const std::string& hashcode, std::string_view content);
    bool exists(const std::string& hashcode) const;
    common::Result<std::filesystem::path> path_for(const std::string& hashcode) const;

private:
    static bool is_valid_hashcode(const std::string& hashcode);
    // 根据文件的 hashcode，生成一个带随机后缀的临时文件路径
    common::Result<std::filesystem::path> temporary_path(const std::string& hashcode) const;

    std::filesystem::path root_;
    std::filesystem::path temporary_root_;
};

} // namespace storage
} // namespace webdisk

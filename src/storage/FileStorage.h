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
    // 按内容哈希去重保存文件；true 表示新写入，false 表示内容已存在，失败则返回存储错误。
    common::Result<bool> store_if_absent(const std::string& hashcode, std::string_view content);
    bool exists(const std::string& hashcode) const;
    // 校验内容哈希并返回 root/hashcode 对应的存储路径，不检查该文件是否实际存在。
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

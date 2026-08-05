#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "common/Result.h"

namespace webdisk {
namespace storage {

class FileStorage {
public:
    virtual ~FileStorage() = default;

    // 按内容哈希去重存储文件；返回 true 表示新写入，false 表示内容已存在，失败则返回存储错误。
    virtual common::Result<bool> store_if_absent(const std::string& hashcode, std::string_view content) = 0;
    // 检查指定内容哈希对应的普通文件是否存在。
    virtual bool exists(const std::string& hashcode) const = 0;
    // 校验内容哈希并计算对应的存储路径，不检查该路径上的文件是否存在。
    virtual common::Result<std::filesystem::path> path_for(const std::string& hashcode) const = 0;
};

} // namespace storage
} // namespace webdisk

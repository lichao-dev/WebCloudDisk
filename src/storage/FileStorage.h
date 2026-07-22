#pragma once

#include "common/Result.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace webdisk {
namespace storage {

class FileStorage {
public:
    virtual ~FileStorage() = default;

    virtual common::Result<bool> store_if_absent(const std::string& hashcode, std::string_view content) = 0;
    virtual bool exists(const std::string& hashcode) const = 0;
    virtual common::Result<std::filesystem::path> path_for(const std::string& hashcode) const = 0;
};

} // namespace storage
} // namespace webdisk

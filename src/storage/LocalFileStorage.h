#pragma once

#include "storage/FileStorage.h"

#include <filesystem>

namespace webdisk {
namespace storage {

class LocalFileStorage final : public FileStorage {
public:
    explicit LocalFileStorage(std::filesystem::path root);

    common::Result<void> init();
    common::Result<bool> store_if_absent(const std::string& hashcode, std::string_view content) override;
    bool exists(const std::string& hashcode) const override;
    common::Result<std::filesystem::path> path_for(const std::string& hashcode) const override;

private:
    static bool valid_hashcode(const std::string& hashcode);
    common::Result<std::filesystem::path> temporary_path(const std::string& hashcode) const;

    std::filesystem::path root_;
    std::filesystem::path temporary_root_;
};

} // namespace storage
} // namespace webdisk

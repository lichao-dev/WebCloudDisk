#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "config/Config.h"
#include "storage/BackupStorage.h"

namespace alibabacloud {
namespace oss2 {
class OSSClient;
} // namespace oss2
} // namespace alibabacloud

namespace webdisk {
namespace storage {

class OssBackupStorage final : public BackupStorage {
public:
    static common::Result<std::unique_ptr<OssBackupStorage>> create(const config::Config::Oss& config);

    ~OssBackupStorage() override;

    common::Result<void> backup_file(const std::string& hashcode, const std::filesystem::path& local_path) override;

private:
    OssBackupStorage(std::string bucket, std::string key_prefix, std::unique_ptr<alibabacloud::oss2::OSSClient> client);

    static bool is_valid_hashcode(const std::string& hashcode);

    std::string bucket_;
    std::string key_prefix_;
    std::unique_ptr<alibabacloud::oss2::OSSClient> client_;
};

} // namespace storage
} // namespace webdisk

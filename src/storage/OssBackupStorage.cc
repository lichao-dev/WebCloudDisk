#include "storage/OssBackupStorage.h"

#include <cctype>
#include <system_error>
#include <utility>

#include <alibabacloud/oss2/ClientConfiguration.h>
#include <alibabacloud/oss2/OSSClient.h>
#include <alibabacloud/oss2/credentials/CredentialsProvider.h>

#include "log/Log.h"

namespace webdisk {
namespace storage {

OssBackupStorage::OssBackupStorage(std::string bucket, std::string key_prefix,
                                   std::unique_ptr<alibabacloud::oss2::OSSClient> client)
    : bucket_{std::move(bucket)},
      key_prefix_{std::move(key_prefix)},
      client_{std::move(client)} {}

OssBackupStorage::~OssBackupStorage() = default;

common::Result<std::unique_ptr<OssBackupStorage>> OssBackupStorage::create(const config::Config::Oss& config) {
    if (config.region.empty() || config.bucket.empty()) {
        return common::Result<std::unique_ptr<OssBackupStorage>>::failure(500,
                                                                          "OSS region and bucket must not be empty");
    }
    if (config.key_prefix.empty() || config.key_prefix.front() == '/') {
        return common::Result<std::unique_ptr<OssBackupStorage>>::failure(
            500, "OSS key prefix must be a non-empty relative prefix");
    }

    std::string key_prefix = config.key_prefix;
    if (key_prefix.back() != '/') {
        key_prefix.push_back('/');
    }

    auto credentials_provider = std::make_shared<alibabacloud::oss2::EnvironmentVariableCredentialsProvider>();
    const auto credentials = credentials_provider->getCredentials();
    if (!credentials.hasKeys()) {
        LOG_ERROR("OSS backup credentials are not available from environment variables");
        return common::Result<std::unique_ptr<OssBackupStorage>>::failure(500, "OSS credentials are not configured");
    }

    auto client_config = alibabacloud::oss2::ClientConfiguration::loadDefault();
    client_config.region = config.region;
    client_config.credentialsProvider = credentials_provider;
    auto client = std::make_unique<alibabacloud::oss2::OSSClient>(client_config);

    auto storage = std::unique_ptr<OssBackupStorage>(
        new OssBackupStorage(config.bucket, std::move(key_prefix), std::move(client)));
    LOG_INFO("OSS backup storage initialized: region={}, bucket={}, key_prefix={}", config.region, config.bucket,
             storage->key_prefix_);
    return common::Result<std::unique_ptr<OssBackupStorage>>::success(std::move(storage));
}

bool OssBackupStorage::is_valid_hashcode(const std::string& hashcode) {
    if (hashcode.size() != 64) {
        return false;
    }
    for (unsigned char c : hashcode) {
        // 检查是否为十六进制字符
        if (!std::isxdigit(c)) {
            return false;
        }
    }
    return true;
}

common::Result<void> OssBackupStorage::backup_file(const std::string& hashcode,
                                                   const std::filesystem::path& local_path) {
    if (!is_valid_hashcode(hashcode)) {
        LOG_ERROR("Invalid content hash passed to OSS backup storage");
        return common::Result<void>::failure(500, "Invalid file hash format");
    }

    std::error_code ec;
    if (!std::filesystem::is_regular_file(local_path, ec)) {
        if (ec) {
            LOG_ERROR("Failed to inspect OSS backup source: path={}, error={}", local_path.string(), ec.message());
        } else {
            LOG_ERROR("OSS backup source is not a regular file: path={}", local_path.string());
        }
        return common::Result<void>::failure(500, "OSS backup source file is unavailable");
    }

    const std::string object_key = key_prefix_ + hashcode;
    auto outcome = client_->putObjectFromFile(
        alibabacloud::oss2::models::PutObjectRequest().setBucket(bucket_).setKey(object_key), local_path.string());
    if (!outcome.has_value()) {
        const auto& sdk_error = outcome.error();
        LOG_ERROR("Failed to back up file to OSS: code={}, message={}, request_id={}, object_key={}",
                  sdk_error.getCode(), sdk_error.getMessage(), sdk_error.getRequestId(), object_key);
        return common::Result<void>::failure(500, "Failed to back up file to OSS");
    }

    LOG_INFO("File backed up to OSS: object_key={}", object_key);
    return common::Result<void>::success();
}

} // namespace storage
} // namespace webdisk

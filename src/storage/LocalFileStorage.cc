#include "storage/LocalFileStorage.h"

#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

#include <openssl/rand.h>

#include "log/Log.h"

namespace webdisk {
namespace storage {

LocalFileStorage::LocalFileStorage(std::filesystem::path root)
    : root_{std::move(root)}
    , temporary_root_{root_ / ".tmp"} {
}

common::Result<void> LocalFileStorage::init() {
    std::error_code error;
    std::filesystem::create_directories(root_, error);
    if (error) {
        LOG_ERROR("Failed to create storage directory: path={}, error={}", root_.string(), error.message());
        return common::Result<void>::failure(500, "Failed to create file storage directory");
    }
    std::filesystem::create_directories(temporary_root_, error);
    if (error) {
        LOG_ERROR("Failed to create temporary storage directory: path={}, error={}", temporary_root_.string(),
                  error.message());
        return common::Result<void>::failure(500, "Failed to create temporary file directory");
    }
    LOG_INFO("Local file storage initialized: root={}", root_.string());
    return common::Result<void>::success();
}

bool LocalFileStorage::is_valid_hashcode(const std::string& hashcode) {
    if (hashcode.size() != 64) {
        return false;
    }
    for (unsigned char c : hashcode) {
        if (!std::isxdigit(c)) {
            return false;
        }
    }
    return true;
}

common::Result<std::filesystem::path> LocalFileStorage::path_for(const std::string& hashcode) const {
    if (!is_valid_hashcode(hashcode)) {
        LOG_ERROR("Invalid content hash passed to local file storage");
        return common::Result<std::filesystem::path>::failure(500, "Invalid file hash format");
    }
    return common::Result<std::filesystem::path>::success(root_ / hashcode);
}

bool LocalFileStorage::exists(const std::string& hashcode) const {
    auto path = path_for(hashcode);
    std::error_code error;
    const bool exists = path && std::filesystem::is_regular_file(path.value(), error) && !error;
    if (error) {
        LOG_ERROR("Failed to inspect stored content: path={}, error={}", path.value().string(), error.message());
    }
    return exists;
}

common::Result<std::filesystem::path> LocalFileStorage::temporary_path(const std::string& hashcode) const {
    std::array<unsigned char, 8> random_bytes{};
    if (RAND_bytes(random_bytes.data(), static_cast<int>(random_bytes.size())) != 1) {
        LOG_ERROR("Failed to generate a random temporary filename");
        return common::Result<std::filesystem::path>::failure(500, "Failed to generate temporary filename");
    }

    std::ostringstream suffix;
    suffix << std::hex << std::setfill('0');
    for (unsigned char byte : random_bytes) {
        suffix << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return common::Result<std::filesystem::path>::success(temporary_root_ / (hashcode + "." + suffix.str()));
}

common::Result<bool> LocalFileStorage::store_if_absent(const std::string& hashcode, std::string_view content) {
    auto final_path = path_for(hashcode);
    if (!final_path) {
        return common::Result<bool>::failure(final_path.error().status_code, final_path.error().message);
    }

    std::error_code exists_error;
    if (std::filesystem::is_regular_file(final_path.value(), exists_error)) {
        LOG_DEBUG("Stored content already exists; skipping duplicate write");
        return common::Result<bool>::success(false);
    }

    auto temp_path = temporary_path(hashcode);
    if (!temp_path) {
        return common::Result<bool>::failure(temp_path.error().status_code, temp_path.error().message);
    }

    // 不能让下载请求看到只写了一部分的目标文件。先写入 .tmp，
    // 完整写入后再通过一次 rename 原子地发布到哈希路径。
    {
        std::ofstream output(temp_path.value(), std::ios::binary | std::ios::trunc);
        if (!output) {
            LOG_ERROR("Failed to create temporary upload file: path={}", temp_path.value().string());
            return common::Result<bool>::failure(500, "Failed to create temporary upload file");
        }
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        output.flush();
        if (!output) {
            LOG_ERROR("Failed to write temporary upload file: path={}", temp_path.value().string());
            std::error_code ignored;
            std::filesystem::remove(temp_path.value(), ignored);
            return common::Result<bool>::failure(500, "Failed to write file");
        }
    }

    std::error_code error;
    std::filesystem::rename(temp_path.value(), final_path.value(), error);
    if (error) {
        // 相同内容并发上传时可能同时执行 rename。如果目标文件此时已经存在，
        // 说明另一个请求已经完成发布，本次请求可以按去重成功处理。
        std::error_code exists_error;
        if (std::filesystem::is_regular_file(final_path.value(), exists_error) && !exists_error) {
            LOG_DEBUG("Concurrent content write was deduplicated");
            std::error_code ignored;
            std::filesystem::remove(temp_path.value(), ignored);
            return common::Result<bool>::success(false);
        }
        LOG_ERROR("Failed to publish stored content: source={}, destination={}, error={}", temp_path.value().string(),
                  final_path.value().string(), error.message());
        std::filesystem::remove(temp_path.value(), error);
        return common::Result<bool>::failure(500, "Failed to save file");
    }
    LOG_DEBUG("Stored new content: size={}", content.size());
    return common::Result<bool>::success(true);
}

} // namespace storage
} // namespace webdisk

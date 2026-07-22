#pragma once

#include "common/Result.h"
#include "model/FileInfo.h"
#include "repository/FileRepository.h"
#include "storage/FileStorage.h"

#include <wfrest/HttpServer.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace webdisk {
namespace service {

struct UploadedFile {
    std::uint64_t file_id;
    std::string filename;
};

struct DownloadFile {
    std::string filename;
    std::filesystem::path path;
};

class FileService {
public:
    using ListCallback = std::function<void(common::Result<std::vector<model::FileInfo>>)>;
    using UploadCallback = std::function<void(common::Result<UploadedFile>)>;
    using DownloadCallback = std::function<void(common::Result<DownloadFile>)>;

    FileService(const repository::FileRepository& files, storage::FileStorage& storage, std::uint64_t max_file_size);

    void list(std::uint64_t user_id, wfrest::HttpResp* response, ListCallback callback) const;
    void upload(std::uint64_t user_id, const std::string& untrusted_filename, const std::string& content,
                wfrest::HttpResp* response, UploadCallback callback) const;
    void find_download(std::uint64_t user_id, std::uint64_t file_id, wfrest::HttpResp* response,
                       DownloadCallback callback) const;

private:
    static common::Result<std::string> sanitize_filename(const std::string& filename);

    const repository::FileRepository& files_;
    storage::FileStorage& storage_;
    std::uint64_t max_file_size_;
};

} // namespace service
} // namespace webdisk

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <wfrest/HttpServer.h>

#include "common/Result.h"
#include "model/FileInfo.h"
#include "repository/FileRepository.h"
#include "storage/BackupStorage.h"
#include "storage/FileStorage.h"

namespace webdisk {
namespace service {

struct UploadedFile {
    uint64_t file_id;
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

    FileService(const repository::FileRepository& files, storage::FileStorage& storage, uint64_t max_file_size);

    // Application 在路由注册前注入可选备份存储；传入 nullptr 表示只使用本地主存储。
    void set_backup_storage(storage::BackupStorage* backup_storage);
    void list(uint64_t user_id, wfrest::HttpResp* response, ListCallback callback) const;
    void upload(uint64_t user_id, const std::string& untrusted_filename, const std::string& content,
                wfrest::HttpResp* response, UploadCallback callback) const;
    void find_download(uint64_t user_id, uint64_t file_id, wfrest::HttpResp* response, DownloadCallback callback) const;

private:
    // 把文件名清理成安全、合法、适合存储或使用的形式
    static common::Result<std::string> sanitize_filename(const std::string& filename);
    // OSS 启用时同步尝试备份本地文件；失败只记录日志，不影响主存储上传流程。
    void try_backup(uint64_t user_id, const std::string& hashcode) const;

    const repository::FileRepository& files_;
    storage::FileStorage& storage_;
    storage::BackupStorage* backup_storage_{nullptr};
    uint64_t max_file_size_;
};

} // namespace service
} // namespace webdisk

#include "service/FileService.h"

#include "log/Log.h"
#include "security/Sha256.h"

#include <utility>

namespace webdisk {
namespace service {

using common::Result;
using model::FileInfo;
using repository::FileRepository;
using security::Sha256;
using storage::FileStorage;

FileService::FileService(const FileRepository& files, FileStorage& storage, std::uint64_t max_file_size)
    : files_(files)
    , storage_(storage)
    , max_file_size_(max_file_size) {
}

Result<std::string> FileService::sanitize_filename(const std::string& filename) {
    const std::size_t slash = filename.find_last_of("/\\");
    std::string base = slash == std::string::npos ? filename : filename.substr(slash + 1);
    if (base.empty() || base == "." || base == ".." || base.size() > 255 || base.find('\0') != std::string::npos ||
        base.find('\r') != std::string::npos || base.find('\n') != std::string::npos) {
        return Result<std::string>::failure(400, "Invalid filename");
    }
    return Result<std::string>::success(std::move(base));
}

void FileService::list(std::uint64_t user_id, wfrest::HttpResp* response, ListCallback callback) const {
    response->add_task(files_.list_by_user(user_id, std::move(callback)));
}

void FileService::upload(std::uint64_t user_id, const std::string& untrusted_filename, const std::string& content,
                         wfrest::HttpResp* response, UploadCallback callback) const {
    if (content.size() > max_file_size_) {
        LOG_WARN("Upload rejected: user_id={}, size={}, max_size={}", user_id, content.size(), max_file_size_);
        callback(Result<UploadedFile>::failure(413, "File size exceeds the limit"));
        return;
    }

    auto filename = sanitize_filename(untrusted_filename);
    if (!filename) {
        LOG_DEBUG("Upload rejected because of invalid filename: user_id={}", user_id);
        callback(Result<UploadedFile>::failure(filename.error().status_code, filename.error().message));
        return;
    }

    auto hashcode = Sha256::hex(content);
    if (!hashcode) {
        callback(Result<UploadedFile>::failure(hashcode.error().status_code, hashcode.error().message));
        return;
    }

    // 先发布内容寻址文件，再插入数据库元数据。数据库失败可能留下无引用文件，
    // 但不会产生指向缺失文件的记录；无引用文件后续可以安全清理。
    auto stored = storage_.store_if_absent(hashcode.value(), content);
    if (!stored.ok()) {
        callback(Result<UploadedFile>::failure(stored.error().status_code, stored.error().message));
        return;
    }

    const std::string safe_filename = filename.value();
    const std::uint64_t file_size = static_cast<std::uint64_t>(content.size());
    const bool content_created = stored.value();
    WFMySQLTask* task = files_.create(
        user_id, safe_filename, hashcode.value(), file_size,
        [user_id, safe_filename, file_size, content_created,
         callback = std::move(callback)](Result<std::uint64_t> result) mutable {
            if (!result) {
                callback(Result<UploadedFile>::failure(result.error().status_code, result.error().message));
                return;
            }
            LOG_INFO("File uploaded: user_id={}, file_id={}, size={}, new_content={}", user_id, result.value(),
                     file_size, content_created);
            callback(Result<UploadedFile>::success(UploadedFile{result.value(), safe_filename}));
        });
    response->add_task(task);
}

void FileService::find_download(std::uint64_t user_id, std::uint64_t file_id, wfrest::HttpResp* response,
                                DownloadCallback callback) const {
    WFMySQLTask* task = files_.find_owned(
        user_id, file_id,
        [this, user_id, file_id, callback = std::move(callback)](Result<std::optional<FileInfo>> result) mutable {
            if (!result) {
                callback(Result<DownloadFile>::failure(result.error().status_code, result.error().message));
                return;
            }
            if (!result.value().has_value()) {
                LOG_DEBUG("Download target not found: user_id={}, file_id={}", user_id, file_id);
                callback(Result<DownloadFile>::failure(404, "File not found"));
                return;
            }

            const FileInfo& file = *result.value();
            auto path = storage_.path_for(file.hashcode);
            if (!path || !storage_.exists(file.hashcode)) {
                LOG_ERROR("Stored content missing: user_id={}, file_id={}", user_id, file_id);
                callback(Result<DownloadFile>::failure(404, "File not found"));
                return;
            }
            LOG_DEBUG("File download prepared: user_id={}, file_id={}", user_id, file_id);
            callback(Result<DownloadFile>::success(DownloadFile{file.filename, path.value()}));
        });
    response->add_task(task);
}

} // namespace service
} // namespace webdisk

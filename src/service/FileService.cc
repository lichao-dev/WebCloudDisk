#include "service/FileService.h"

#include <utility>

#include "log/Log.h"
#include "security/Sha256.h"

namespace webdisk {
namespace service {

FileService::FileService(const repository::FileRepository& files, storage::FileStorage& storage, uint64_t max_file_size)
    : files_{files},
      storage_{storage},
      max_file_size_{max_file_size} {}

void FileService::set_backup_task_publisher(messaging::RabbitMqBackupTaskPublisher* backup_task_publisher) {
    backup_task_publisher_ = backup_task_publisher;
}

common::Result<std::string> FileService::sanitize_filename(const std::string& filename) {
    // 查找文件名中最后一个 / 或 \，只保留最后的基础文件名，同时兼容 Linux/macOS 路径 和 Windows 路径
    const size_t slash = filename.find_last_of("/\\");
    std::string base = slash == std::string::npos ? filename : filename.substr(slash + 1);

    // 防止空文件名、当前目录.、上级目录..、超过数据库 VARCHAR(255) 的长度
    // 空字符导致字符串截断或文件 API 解析差异、回车换行导致 HTTP 响应头注入
    if (base.empty() || base == "." || base == ".." || base.size() > 255 || base.find('\0') != std::string::npos ||
        base.find('\r') != std::string::npos || base.find('\n') != std::string::npos) {
        return common::Result<std::string>::failure(400, "Invalid filename");
    }

    // 成功时返回安全的基础文件名
    return common::Result<std::string>::success(std::move(base));
}

void FileService::try_publish_backup(uint64_t user_id, const std::string& hashcode, uint64_t size) const {
    if (backup_task_publisher_ == nullptr) {
        return;
    }

    auto published =
        backup_task_publisher_->publish(messaging::BackupTask{messaging::BackupTask::current_version, hashcode, size});
    if (!published) {
        // 本地磁盘是主存储。第一阶段尚未实现 Outbox，发布失败不会撤销已经完成的本地上传和数据库记录。
        LOG_WARN("OSS backup task publication failed; continuing with local upload: user_id={}, hashcode={}, error={}",
                 user_id, hashcode, published.error().message);
        return;
    }
    LOG_DEBUG("OSS backup task published: user_id={}, hashcode={}, size={}", user_id, hashcode, size);
}

void FileService::list(uint64_t user_id, wfrest::HttpResp* response, ListCallback callback) const {
    response->add_task(files_.list_by_user(user_id, std::move(callback)));
}

void FileService::upload(uint64_t user_id, const std::string& untrusted_filename, const std::string& content,
                         wfrest::HttpResp* response, UploadCallback callback) const {
    if (content.size() > max_file_size_) {
        LOG_WARN("Upload rejected: user_id={}, size={}, max_size={}", user_id, content.size(), max_file_size_);
        callback(common::Result<UploadedFile>::failure(413, "File size exceeds the limit"));
        return;
    }

    auto filename = sanitize_filename(untrusted_filename);
    if (!filename) {
        LOG_DEBUG("Upload rejected because of invalid filename: user_id={}", user_id);
        callback(common::Result<UploadedFile>::failure(filename.error().status_code, filename.error().message));
        return;
    }

    auto hashcode = security::Sha256::hex(content);
    if (!hashcode) {
        callback(common::Result<UploadedFile>::failure(hashcode.error().status_code, hashcode.error().message));
        return;
    }

    // 先发布内容寻址文件，再插入数据库元数据。数据库失败可能留下无引用文件，
    // 但不会产生指向缺失文件的记录；无引用文件后续可以安全清理。
    auto stored = storage_.store_if_absent(hashcode.value(), content);
    if (!stored.ok()) {
        callback(common::Result<UploadedFile>::failure(stored.error().status_code, stored.error().message));
        return;
    }

    const std::string safe_filename = filename.value();
    const std::string content_hashcode = hashcode.value();
    const uint64_t file_size = static_cast<uint64_t>(content.size());
    const bool content_created = stored.value();
    WFMySQLTask* task = files_.create(
        user_id, safe_filename, content_hashcode, file_size,
        [this, user_id, safe_filename, content_hashcode, file_size, content_created, response,
         callback = std::move(callback)](common::Result<uint64_t> result) mutable {
            if (!result) {
                callback(common::Result<UploadedFile>::failure(result.error().status_code, result.error().message));
                return;
            }
            const uint64_t file_id = result.value();
            // BasicPublish 会等待 Broker 确认。移到计算队列，避免阻塞 Workflow 的数据库完成回调线程。
            response->Compute(0, [this, user_id, file_id, safe_filename, content_hashcode, file_size, content_created,
                                  callback = std::move(callback)]() {
                    // 只有文件元数据成功写入后才提交备份任务，避免为失败的上传创建无意义的 OSS 对象。
                try_publish_backup(user_id, content_hashcode, file_size);
                LOG_INFO("File uploaded: user_id={}, file_id={}, size={}, new_content={}", user_id, file_id, file_size,
                         content_created);
                callback(common::Result<UploadedFile>::success(UploadedFile{file_id, safe_filename}));
            });
        });
    response->add_task(task);
}

void FileService::find_download(uint64_t user_id, uint64_t file_id, wfrest::HttpResp* response,
                                DownloadCallback callback) const {
    // Result 表示数据库查询是否成功，optional<FileInfo> 表示是否找到当前用户拥有的文件；
    // 文件不存在或不属于当前用户都会返回空值，并统一映射为 404。
    WFMySQLTask* task = files_.find_owned(
        user_id, file_id,
        [this, user_id, file_id,
         callback = std::move(callback)](common::Result<std::optional<model::FileInfo>> result) {
            if (!result) {
                callback(common::Result<DownloadFile>::failure(result.error().status_code, result.error().message));
                return;
            }
            if (!result.value().has_value()) {
                LOG_DEBUG("Download target not found: user_id={}, file_id={}", user_id, file_id);
                callback(common::Result<DownloadFile>::failure(404, "File not found"));
                return;
            }

            const model::FileInfo& file = *result.value();
            auto path = storage_.path_for(file.hashcode);
            if (!path || !storage_.exists(file.hashcode)) {
                LOG_ERROR("Stored content missing: user_id={}, file_id={}", user_id, file_id);
                callback(common::Result<DownloadFile>::failure(404, "File not found"));
                return;
            }
            LOG_DEBUG("File download prepared: user_id={}, file_id={}", user_id, file_id);
            callback(common::Result<DownloadFile>::success(DownloadFile{file.filename, path.value()}));
        });
    response->add_task(task);
}

} // namespace service
} // namespace webdisk

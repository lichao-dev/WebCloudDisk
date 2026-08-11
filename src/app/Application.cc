#include "app/Application.h"

#include <algorithm>
#include <limits>

#include "storage/OssBackupStorage.h"

namespace webdisk {
namespace app {

Application::Application(config::Config config)
    : config_{std::move(config)},
      database_{config_.database},
      users_{database_},
      files_{database_},
      password_hasher_{config_.auth.password_iterations},
      jwt_service_{config_.auth.jwt_secret, config_.auth.jwt_issuer, config_.auth.token_ttl},
      storage_{config_.storage.root},
      auth_service_{users_, password_hasher_, jwt_service_},
      user_service_{users_},
      file_service_{files_, storage_, config_.storage.max_file_size},
      auth_middleware_{jwt_service_},
      auth_handler_{auth_service_},
      user_handler_{auth_middleware_, user_service_},
      file_handler_{auth_middleware_, file_service_} {}

common::Result<void> Application::init() {
    auto storage_result = storage_.init();
    if (!storage_result) {
        return storage_result;
    }
    if (config_.oss.enabled) {
        auto backup_storage = storage::OssBackupStorage::create(config_.oss);
        if (!backup_storage) {
            return common::Result<void>::failure(backup_storage.error().status_code, backup_storage.error().message);
        }
        backup_storage_ = backup_storage.take_value();
        file_service_.set_backup_storage(backup_storage_.get());
    }
    register_routes();
    return common::Result<void>::success();
}

void Application::register_routes() {
    if (routes_registered_) {
        return;
    }

    const wfrest::Handler register_handler = [this](const wfrest::HttpReq* request, wfrest::HttpResp* response) {
        auth_handler_.register_user(request, response);
    };
    const wfrest::Handler login_handler = [this](const wfrest::HttpReq* request, wfrest::HttpResp* response) {
        auth_handler_.login(request, response);
    };
    const wfrest::Handler current_user_handler = [this](const wfrest::HttpReq* request, wfrest::HttpResp* response) {
        user_handler_.current_user(request, response);
    };
    const wfrest::Handler list_files_handler = [this](const wfrest::HttpReq* request, wfrest::HttpResp* response) {
        file_handler_.list(request, response);
    };
    const wfrest::Handler upload_file_handler = [this](const wfrest::HttpReq* request, wfrest::HttpResp* response) {
        file_handler_.upload(request, response);
    };
    const wfrest::Handler download_file_handler = [this](const wfrest::HttpReq* request, wfrest::HttpResp* response) {
        file_handler_.download(request, response);
    };

    // 注册需要计算密码哈希，上传需要计算文件哈希、写盘并同步尝试 OSS 备份；
    // 这两个处理器进入计算队列执行，避免阻塞网络线程。
    server_.POST("/api/v1/auth/register", 0, register_handler);
    server_.POST("/api/v1/auth/login", login_handler);
    server_.GET("/api/v1/user/me", current_user_handler);
    server_.GET("/api/v1/files", list_files_handler);
    server_.POST("/api/v1/files", 0, upload_file_handler);
    server_.GET("/api/v1/file/{id}", download_file_handler);

    const std::string index_file = (config_.server.web_root / "index.html").string();
    const std::string static_root = (config_.server.web_root / "static").string();
    server_.Static("/", index_file.c_str());
    server_.Static("/static", static_root.c_str());

    // multipart 请求体除文件内容外还包含边界和字段头，因此额外预留 1 MiB，
    // 并在计算请求上限时避免无符号整数溢出。
    const uint64_t overhead = 1024ULL * 1024ULL;
    const uint64_t request_limit = config_.storage.max_file_size > std::numeric_limits<uint64_t>::max() - overhead
                                       ? config_.storage.max_file_size
                                       : config_.storage.max_file_size + overhead;
    // wfrest 使用 size_t 表示请求大小上限，因此先截取到当前平台可表示的最大值。
    server_.request_size_limit(
        static_cast<size_t>(std::min<uint64_t>(request_limit, std::numeric_limits<size_t>::max())));
    routes_registered_ = true;
}

int Application::start() {
    return server_.start(config_.server.port);
}

void Application::stop() {
    server_.stop();
}

} // namespace app
} // namespace webdisk

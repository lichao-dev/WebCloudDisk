#pragma once

#include <memory>

#include <wfrest/HttpServer.h>

#include "config/Config.h"
#include "database/MySqlClient.h"
#include "http/AuthHandler.h"
#include "http/AuthMiddleware.h"
#include "http/FileHandler.h"
#include "http/UserHandler.h"
#include "repository/FileRepository.h"
#include "repository/UserRepository.h"
#include "security/JwtService.h"
#include "security/PasswordHasher.h"
#include "service/AuthService.h"
#include "service/FileService.h"
#include "service/UserService.h"
#include "storage/BackupStorage.h"
#include "storage/FileStorage.h"

namespace webdisk {
namespace app {

class Application {
public:
    explicit Application(config::Config config);

    common::Result<void> init();
    int start();
    void stop();

private:
    void register_routes();

    config::Config config_;
    db::MySqlClient database_;
    repository::UserRepository users_;
    repository::FileRepository files_;
    security::PasswordHasher password_hasher_;
    security::JwtService jwt_service_;
    storage::FileStorage storage_;
    std::unique_ptr<storage::BackupStorage> backup_storage_;
    service::AuthService auth_service_;
    service::UserService user_service_;
    service::FileService file_service_;
    http::AuthMiddleware auth_middleware_;
    http::AuthHandler auth_handler_;
    http::UserHandler user_handler_;
    http::FileHandler file_handler_;
    wfrest::HttpServer server_;
    bool routes_registered_{false};
};

} // namespace app
} // namespace webdisk

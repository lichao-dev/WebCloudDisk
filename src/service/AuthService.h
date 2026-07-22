#pragma once

#include "common/Result.h"
#include "model/User.h"
#include "repository/UserRepository.h"
#include "security/JwtService.h"
#include "security/PasswordHasher.h"

#include <wfrest/HttpServer.h>

#include <cstdint>
#include <functional>
#include <string>

namespace webdisk {
namespace service {

struct RegisteredUser {
    uint64_t user_id;
    std::string username;
};

struct LoginResult {
    std::string access_token;
    model::User user;
};

class AuthService {
public:
    using RegisterCallback = std::function<void(common::Result<RegisteredUser>)>;
    using LoginCallback = std::function<void(common::Result<LoginResult>)>;

    AuthService(const repository::UserRepository& users, const security::PasswordHasher& password_hasher,
                const security::JwtService& jwt_service);

    void register_user(const std::string& username, const std::string& password, const std::string& confirm,
                       wfrest::HttpResp* response, RegisterCallback callback) const;

    void login(const std::string& username, const std::string& password, wfrest::HttpResp* response,
               LoginCallback callback) const;

private:
    void finish_login(const model::User& user, const std::string& password, wfrest::HttpResp* response,
                      LoginCallback callback) const;

    const repository::UserRepository& users_;
    const security::PasswordHasher& password_hasher_;
    const security::JwtService& jwt_service_;
};

} // namespace service
} // namespace webdisk

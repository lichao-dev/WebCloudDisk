#include "service/AuthService.h"

#include <utility>

#include "log/Log.h"

namespace webdisk {
namespace service {

AuthService::AuthService(const repository::UserRepository& users, const security::PasswordHasher& password_hasher,
                         const security::JwtService& jwt_service)
    : users_{users},
      password_hasher_{password_hasher},
      jwt_service_{jwt_service} {}

void AuthService::register_user(const std::string& username, const std::string& password, const std::string& confirm,
                                const common::TaskScheduler& scheduler, RegisterCallback callback) const {
    if (username.empty() || password.empty()) {
        callback(common::Result<RegisteredUser>::failure(400, "Username and password must not be empty"));
        return;
    }
    if (username.size() > 64 || password.size() > 1024) {
        callback(common::Result<RegisteredUser>::failure(400, "Username or password is too long"));
        return;
    }
    if (password != confirm) {
        callback(common::Result<RegisteredUser>::failure(400, "Passwords do not match"));
        return;
    }

    auto password_hash = password_hasher_.hash(password);
    if (!password_hash) {
        callback(
            common::Result<RegisteredUser>::failure(password_hash.error().status_code, password_hash.error().message));
        return;
    }

    WFMySQLTask* task = users_.create(
        username, password_hash.value(), [username, callback = std::move(callback)](common::Result<uint64_t> result) {
            if (!result) {
                callback(common::Result<RegisteredUser>::failure(result.error().status_code, result.error().message));
                return;
            }
            LOG_INFO("User registered: user_id={}", result.value());
            callback(common::Result<RegisteredUser>::success(RegisteredUser{result.value(), username}));
        });
    scheduler.add_task(task);
}

void AuthService::login(const std::string& username, const std::string& password,
                        const common::TaskScheduler& scheduler,
                        LoginCallback callback) const {
    if (username.empty() || password.empty()) {
        callback(common::Result<LoginResult>::failure(400, "Username and password must not be empty"));
        return;
    }
    if (username.size() > 64 || password.size() > 1024) {
        callback(common::Result<LoginResult>::failure(401, "Invalid username or password"));
        return;
    }

    // Result 表示数据库查询是否成功，optional<User> 表示查询成功后是否找到用户。
    WFMySQLTask* task =
        users_.find_by_username(username, [this, password, scheduler, callback = std::move(callback)](
                                              common::Result<std::optional<model::User>> result) mutable {
            if (!result) {
                callback(common::Result<LoginResult>::failure(result.error().status_code, result.error().message));
                return;
            }
            if (!result.value().has_value()) {
                LOG_WARN("Login rejected: user not found");
                callback(common::Result<LoginResult>::failure(401, "Invalid username or password"));
                return;
            }

            // 这里的 * 是 std::optional 的“解包”操作，用来取出里面存的 model::User 对象
            model::User user = *result.value();
            // MySQL 回调运行在 Workflow 的处理线程中，而 PBKDF2 会大量占用 CPU。
            // 将密码验证转移到计算队列，避免阻塞网络和数据库回调线程。
            scheduler.add_compute_task(
                [this, user = std::move(user), password, scheduler, callback = std::move(callback)]() mutable {
                    finish_login(user, password, scheduler, std::move(callback));
                });
        });
    scheduler.add_task(task);
}

void AuthService::finish_login(const model::User& user, const std::string& password,
                               const common::TaskScheduler& scheduler, LoginCallback callback) const {
    auto verified = password_hasher_.verify(password, user.password_hash);
    // 验证流程本身失败，例如存储的哈希格式损坏或 PBKDF2 计算出错。
    if (!verified.ok()) {
        callback(common::Result<LoginResult>::failure(verified.error().status_code, verified.error().message));
        return;
    }
    // 验证正常完成但摘要不匹配，说明用户输入的密码错误。
    if (!verified.value()) {
        LOG_WARN("Login rejected: invalid credentials for user_id={}", user.id);
        callback(common::Result<LoginResult>::failure(401, "Invalid username or password"));
        return;
    }

    auto token = jwt_service_.generate(user.id);
    if (!token) {
        callback(common::Result<LoginResult>::failure(token.error().status_code, token.error().message));
        return;
    }

    LoginResult login_result{token.value(), user};
    if (!password_hasher_.needs_rehash(user.password_hash)) {
        LOG_INFO("User logged in: user_id={}", user.id);
        callback(common::Result<LoginResult>::success(std::move(login_result)));
        return;
    }

    // 登录成功后顺便升级旧的迭代参数，调整密码策略时无需强制用户重置密码。
    auto upgraded_hash = password_hasher_.hash(password);
    if (!upgraded_hash) {
        callback(
            common::Result<LoginResult>::failure(upgraded_hash.error().status_code, upgraded_hash.error().message));
        return;
    }

    WFMySQLTask* update_task = users_.update_password_hash(
        user.id, upgraded_hash.value(),
        [user_id = user.id, login_result = std::move(login_result),
         callback = std::move(callback)](common::Result<void> result) mutable {
            if (!result) {
                callback(common::Result<LoginResult>::failure(result.error().status_code, result.error().message));
                return;
            }
            LOG_INFO("User logged in and password hash upgraded: user_id={}", user_id);
            callback(common::Result<LoginResult>::success(std::move(login_result)));
        });
    scheduler.add_task(update_task);
}

} // namespace service
} // namespace webdisk

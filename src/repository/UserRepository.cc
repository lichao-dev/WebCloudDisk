#include "repository/UserRepository.h"

#include "log/Log.h"

#include <workflow/MySQLResult.h>
#include <workflow/Workflow.h>
#include <workflow/mysql_types.h>

#include <utility>
#include <vector>

namespace webdisk {
namespace repository {

using common::AppError;
using common::Result;
using db::MySqlClient;
using model::User;

namespace {

std::optional<AppError> database_error(WFMySQLTask* task, const std::string& operation) {
    // Workflow 传输失败和 MySQL 返回错误包是两条独立的失败路径，
    // 读取结果游标前必须分别检查。
    if (task->get_state() != WFT_STATE_SUCCESS) {
        LOG_ERROR("MySQL transport failure during {}: {}", operation, task->get_error());
        return AppError{500, "Internal server error"};
    }
    const auto* response = task->get_resp();
    if (response->is_error_packet()) {
        LOG_ERROR("MySQL error during {}: code={}, state={}, message={}", operation, response->get_error_code(),
                  response->get_sql_state(), response->get_error_msg());
        return AppError{500, "Internal server error"};
    }
    return std::nullopt;
}

Result<std::optional<User>> read_one_user(WFMySQLTask* task, const std::string& operation) {
    if (auto error = database_error(task, operation)) {
        return Result<std::optional<User>>::failure(error->status_code, error->message);
    }

    protocol::MySQLResultCursor cursor(task->get_resp());
    if (cursor.get_cursor_status() != MYSQL_STATUS_GET_RESULT) {
        LOG_ERROR("Unexpected MySQL result cursor status during {}: status={}", operation,
                  static_cast<int>(cursor.get_cursor_status()));
        return Result<std::optional<User>>::failure(500, "Internal server error");
    }

    std::vector<protocol::MySQLCell> row;
    if (!cursor.fetch_row(row)) {
        return Result<std::optional<User>>::success(std::nullopt);
    }

    User user;
    user.id = row[0].as_ulonglong();
    user.username = row[1].as_string();
    user.password_hash = row[2].as_string();
    user.created_at = row[3].as_datetime();
    user.updated_at = row[4].as_datetime();
    return Result<std::optional<User>>::success(std::move(user));
}

} // namespace

UserRepository::UserRepository(const MySqlClient& database)
    : database_(database) {
}

WFMySQLTask* UserRepository::find_by_username(const std::string& username, FindCallback callback) const {
    const std::string sql = "SELECT id, username, password_hash, created_at, updated_at FROM tbl_user "
                            "WHERE username='" +
                            MySqlClient::escape(username) + "' LIMIT 1";
    return database_.create_task(sql, [callback = std::move(callback)](WFMySQLTask* task) mutable {
        callback(read_one_user(task, "find user by username"));
    });
}

WFMySQLTask* UserRepository::find_by_id(uint64_t user_id, FindCallback callback) const {
    const std::string sql =
        "SELECT id, username, password_hash, created_at, updated_at FROM tbl_user WHERE id=" + std::to_string(user_id) +
        " LIMIT 1";
    return database_.create_task(sql, [callback = std::move(callback)](WFMySQLTask* task) mutable {
        callback(read_one_user(task, "find user by id"));
    });
}

WFMySQLTask* UserRepository::create(const std::string& username, const std::string& password_hash,
                                    CreateCallback callback) const {
    const std::string sql = "INSERT INTO tbl_user(username, password_hash) VALUES('" + MySqlClient::escape(username) +
                            "','" + MySqlClient::escape(password_hash) + "')";
    return database_.create_task(sql, [callback = std::move(callback)](WFMySQLTask* task) {
        if (task->get_state() != WFT_STATE_SUCCESS) {
            LOG_ERROR("MySQL transport failure during create user: {}", task->get_error());
            callback(Result<uint64_t>::failure(500, "Internal server error"));
            return;
        }

        const auto* response = task->get_resp();
        if (response->is_error_packet()) {
            // 1062 才是 uk_user_username 唯一键冲突；连接或表结构错误
            // 不能被误报成“用户名已存在”。
            if (response->get_error_code() == 1062) {
                callback(Result<uint64_t>::failure(409, "Username already exists"));
                return;
            }
            LOG_ERROR("MySQL error during create user: code={}, state={}, message={}", response->get_error_code(),
                      response->get_sql_state(), response->get_error_msg());
            callback(Result<uint64_t>::failure(500, "Internal server error"));
            return;
        }
        callback(Result<uint64_t>::success(response->get_last_insert_id()));
    });
}

WFMySQLTask* UserRepository::update_password_hash(uint64_t user_id, const std::string& password_hash,
                                                  UpdateCallback callback) const {
    const std::string sql = "UPDATE tbl_user SET password_hash='" + MySqlClient::escape(password_hash) +
                            "' WHERE id=" + std::to_string(user_id);
    return database_.create_task(sql, [callback = std::move(callback)](WFMySQLTask* task) mutable {
        if (auto error = database_error(task, "update password hash")) {
            callback(Result<void>::failure(error->status_code, error->message));
            return;
        }
        callback(Result<void>::success());
    });
}

} // namespace repository
} // namespace webdisk

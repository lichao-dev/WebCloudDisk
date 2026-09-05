#include "repository/UserRepository.h"

#include <utility>
#include <vector>

#include <workflow/MySQLResult.h>
#include <workflow/Workflow.h>
#include <workflow/mysql_types.h>

#include "log/Log.h"

namespace webdisk {
namespace repository {

namespace {

// 检查 Workflow 传输状态和 MySQL 错误包，并将数据库失败统一转换为 AppError。
std::optional<common::AppError> database_error(WFMySQLTask* task, const std::string& operation) {
    // Workflow 传输失败和 MySQL 返回错误包是两条独立的失败路径，
    // 读取结果游标前必须分别检查。
    if (task->get_state() != WFT_STATE_SUCCESS) {
        LOG_ERROR("MySQL transport failure during {}: {}", operation, task->get_error());
        return common::AppError{500, "Internal server error"};
    }
    const auto* resp = task->get_resp();
    if (resp->is_error_packet()) {
        LOG_ERROR("MySQL error during {}: code={}, state={}, message={}", operation, resp->get_error_code(),
                  resp->get_sql_state(), resp->get_error_msg());
        return common::AppError{500, "Internal server error"};
    }
    return std::nullopt;
}

// 解析单用户查询结果，区分数据库失败、结果为空和成功返回用户三种情况。
common::Result<std::optional<model::User>> read_one_user(WFMySQLTask* task, const std::string& operation) {
    if (auto error = database_error(task, operation)) {
        return common::Result<std::optional<model::User>>::failure(*error);
    }

    // protocol::MySQLResultCursor 是 Workflow 提供的一个类，用来解析和遍历 MySQL 返回的结果集
    // “游标”可以理解为指向结果集当前位置的迭代器
    protocol::MySQLResultCursor cursor{task->get_resp()};
    if (cursor.get_cursor_status() != MYSQL_STATUS_GET_RESULT) {
        LOG_ERROR("Unexpected MySQL result cursor status during {}: status={}", operation,
                  static_cast<int>(cursor.get_cursor_status()));
        return common::Result<std::optional<model::User>>::failure(500, "Internal server error");
    }

    std::vector<protocol::MySQLCell> row;
    if (!cursor.fetch_row(row)) {
        return common::Result<std::optional<model::User>>::success(std::nullopt);
    }

    model::User user;
    user.id = row[0].as_ulonglong();
    user.username = row[1].as_string();
    user.password_hash = row[2].as_string();
    user.created_at = row[3].as_datetime(); // YYYY-MM-DD HH:MM:SS.mmm
    user.updated_at = row[4].as_datetime();
    return common::Result<std::optional<model::User>>::success(std::move(user));
}

} // namespace

UserRepository::UserRepository(const db::MySqlClient& database)
    : database_{database} {}

WFMySQLTask* UserRepository::find_by_username(const std::string& username, FindCallback callback) const {
    const std::string sql = "SELECT id, username, password_hash, created_at, updated_at FROM tbl_user "
                            "WHERE username='" +
                            db::MySqlClient::escape(username) + "' LIMIT 1";
    return database_.create_task(sql, [callback = std::move(callback)](WFMySQLTask* task) {
        callback(read_one_user(task, "find user by username"));
    });
}

WFMySQLTask* UserRepository::find_by_id(uint64_t user_id, FindCallback callback) const {
    const std::string sql =
        "SELECT id, username, password_hash, created_at, updated_at FROM tbl_user WHERE id=" + std::to_string(user_id) +
        " LIMIT 1";
    return database_.create_task(
        sql, [callback = std::move(callback)](WFMySQLTask* task) { callback(read_one_user(task, "find user by id")); });
}

WFMySQLTask* UserRepository::create(const std::string& username, const std::string& password_hash,
                                    CreateCallback callback) const {
    const std::string sql = "INSERT INTO tbl_user(username, password_hash) VALUES('" +
                            db::MySqlClient::escape(username) + "','" + db::MySqlClient::escape(password_hash) + "')";

    return database_.create_task(sql, [callback = std::move(callback)](WFMySQLTask* task) {
        if (task->get_state() != WFT_STATE_SUCCESS) {
            LOG_ERROR("MySQL transport failure during create user: {}", task->get_error());
            callback(common::Result<uint64_t>::failure(500, "Internal server error"));
            return;
        }

        const auto* resp = task->get_resp();
        if (resp->is_error_packet()) {
            // 1062 才是 uk_user_username 唯一键冲突；连接或表结构错误
            // 不能被误报成“用户名已存在”。
            if (resp->get_error_code() == 1062) {
                callback(common::Result<uint64_t>::failure(409, "Username already exists"));
                return;
            }
            LOG_ERROR("MySQL error during create user: code={}, state={}, message={}", resp->get_error_code(),
                      resp->get_sql_state(), resp->get_error_msg());
            callback(common::Result<uint64_t>::failure(500, "Internal server error"));
            return;
        }
        callback(common::Result<uint64_t>::success(resp->get_last_insert_id()));
    });
}

WFMySQLTask* UserRepository::update_password_hash(uint64_t user_id, const std::string& password_hash,
                                                  UpdateCallback callback) const {
    const std::string sql = "UPDATE tbl_user SET password_hash='" + db::MySqlClient::escape(password_hash) +
                            "' WHERE id=" + std::to_string(user_id);
    return database_.create_task(sql, [callback = std::move(callback)](WFMySQLTask* task) {
        if (auto error = database_error(task, "update password hash")) {
            callback(common::Result<void>::failure(*error));
            return;
        }
        callback(common::Result<void>::success());
    });
}

} // namespace repository
} // namespace webdisk

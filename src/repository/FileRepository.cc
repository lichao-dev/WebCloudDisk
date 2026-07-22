#include "repository/FileRepository.h"

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
using model::FileInfo;

namespace {

std::optional<AppError> database_error(WFMySQLTask* task, const std::string& operation) {
    // 网络任务成功不代表 SQL 成功，响应中仍可能包含 MySQL 错误包。
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

FileInfo row_to_file(const std::vector<protocol::MySQLCell>& row) {
    FileInfo file;
    file.id = row[0].as_ulonglong();
    file.user_id = row[1].as_ulonglong();
    file.filename = row[2].as_string();
    file.hashcode = row[3].as_string();
    file.size = row[4].as_ulonglong();
    file.created_at = row[5].as_datetime();
    file.updated_at = row[6].as_datetime();
    return file;
}

} // namespace

FileRepository::FileRepository(const MySqlClient& database)
    : database_(database) {
}

WFMySQLTask* FileRepository::list_by_user(std::uint64_t user_id, ListCallback callback) const {
    const std::string sql = "SELECT id, uid, filename, hashcode, size, created_at, updated_at FROM tbl_file "
                            "WHERE uid=" +
                            std::to_string(user_id) + " ORDER BY created_at DESC, id DESC";
    return database_.create_task(sql, [callback = std::move(callback)](WFMySQLTask* task) mutable {
        if (auto error = database_error(task, "list files")) {
            callback(Result<std::vector<FileInfo>>::failure(error->status_code, error->message));
            return;
        }

        protocol::MySQLResultCursor cursor(task->get_resp());
        if (cursor.get_cursor_status() != MYSQL_STATUS_GET_RESULT) {
            LOG_ERROR("Unexpected MySQL result cursor status while listing files: status={}",
                      static_cast<int>(cursor.get_cursor_status()));
            callback(Result<std::vector<FileInfo>>::failure(500, "Internal server error"));
            return;
        }

        std::vector<FileInfo> files;
        std::vector<protocol::MySQLCell> row;
        while (cursor.fetch_row(row)) {
            files.push_back(row_to_file(row));
            row.clear();
        }
        callback(Result<std::vector<FileInfo>>::success(std::move(files)));
    });
}

WFMySQLTask* FileRepository::find_owned(std::uint64_t user_id, std::uint64_t file_id, FindCallback callback) const {
    // 把用户归属直接写进查询条件。只按文件 ID 查询再单独鉴权，
    // 更容易因遗漏检查而产生跨用户访问漏洞。
    const std::string sql = "SELECT id, uid, filename, hashcode, size, created_at, updated_at FROM tbl_file "
                            "WHERE id=" +
                            std::to_string(file_id) + " AND uid=" + std::to_string(user_id) + " LIMIT 1";
    return database_.create_task(sql, [callback = std::move(callback)](WFMySQLTask* task) mutable {
        if (auto error = database_error(task, "find owned file")) {
            callback(Result<std::optional<FileInfo>>::failure(error->status_code, error->message));
            return;
        }

        protocol::MySQLResultCursor cursor(task->get_resp());
        if (cursor.get_cursor_status() != MYSQL_STATUS_GET_RESULT) {
            LOG_ERROR("Unexpected MySQL result cursor status while finding a file: status={}",
                      static_cast<int>(cursor.get_cursor_status()));
            callback(Result<std::optional<FileInfo>>::failure(500, "Internal server error"));
            return;
        }

        std::vector<protocol::MySQLCell> row;
        if (!cursor.fetch_row(row)) {
            callback(Result<std::optional<FileInfo>>::success(std::nullopt));
            return;
        }
        callback(Result<std::optional<FileInfo>>::success(row_to_file(row)));
    });
}

WFMySQLTask* FileRepository::create(std::uint64_t user_id, const std::string& filename, const std::string& hashcode,
                                    std::uint64_t size, CreateCallback callback) const {
    const std::string sql = "INSERT INTO tbl_file(uid, filename, hashcode, size) VALUES(" + std::to_string(user_id) +
                            ",'" + MySqlClient::escape(filename) + "','" + MySqlClient::escape(hashcode) + "'," +
                            std::to_string(size) + ")";
    return database_.create_task(sql, [callback = std::move(callback)](WFMySQLTask* task) mutable {
        if (task->get_state() != WFT_STATE_SUCCESS) {
            LOG_ERROR("MySQL transport failure during create file: {}", task->get_error());
            callback(Result<std::uint64_t>::failure(500, "Internal server error"));
            return;
        }
        const auto* response = task->get_resp();
        if (response->is_error_packet()) {
            // 当前唯一键规定同一用户不能拥有两个同名文件。
            if (response->get_error_code() == 1062) {
                callback(Result<std::uint64_t>::failure(409, "Filename already exists"));
                return;
            }
            LOG_ERROR("MySQL error during create file: code={}, state={}, message={}", response->get_error_code(),
                      response->get_sql_state(), response->get_error_msg());
            callback(Result<std::uint64_t>::failure(500, "Internal server error"));
            return;
        }
        callback(Result<std::uint64_t>::success(response->get_last_insert_id()));
    });
}

} // namespace repository
} // namespace webdisk

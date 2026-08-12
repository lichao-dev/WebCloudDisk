#include "messaging/BackupTask.h"

#include <cctype>
#include <utility>

#include <nlohmann/json.hpp>

namespace webdisk {
namespace messaging {
namespace {

bool is_valid_hashcode(const std::string& hashcode) {
    if (hashcode.size() != 64) {
        return false;
    }
    for (unsigned char c : hashcode) {
        if (!std::isxdigit(c)) {
            return false;
        }
    }
    return true;
}

} // namespace

common::Result<std::string> serialize_backup_task(const BackupTask& task) {
    if (task.version != BackupTask::current_version || !is_valid_hashcode(task.hashcode)) {
        return common::Result<std::string>::failure(500, "Invalid backup task");
    }

    nlohmann::json body{
        {"version", task.version},
        {"hashcode", task.hashcode},
        {"size", task.size},
    };
    return common::Result<std::string>::success(body.dump());
}

common::Result<BackupTask> parse_backup_task(const std::string& body) {
    auto json = nlohmann::json::parse(body, nullptr, false);
    if (json.is_discarded() || !json.is_object() || !json.contains("version") || !json.contains("hashcode") ||
        !json.contains("size") || !json["version"].is_number_unsigned() || !json["hashcode"].is_string() ||
        !json["size"].is_number_unsigned()) {
        return common::Result<BackupTask>::failure(400, "Invalid backup task message");
    }

    BackupTask task;
    task.version = json["version"].get<uint32_t>();
    task.hashcode = json["hashcode"].get<std::string>();
    task.size = json["size"].get<uint64_t>();
    if (task.version != BackupTask::current_version || !is_valid_hashcode(task.hashcode)) {
        return common::Result<BackupTask>::failure(400, "Unsupported or invalid backup task message");
    }

    return common::Result<BackupTask>::success(std::move(task));
}

} // namespace messaging
} // namespace webdisk

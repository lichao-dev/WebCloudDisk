#pragma once

#include <cstdint>
#include <string>

#include "common/Result.h"

namespace webdisk {
namespace messaging {

struct BackupTask {
    static constexpr uint32_t current_version{1};

    uint32_t version{current_version};
    std::string hashcode;
    uint64_t size{0};
};

// 校验任务字段并序列化为 RabbitMQ 使用的 JSON 消息体。
common::Result<std::string> serialize_backup_task(const BackupTask& task);
// 将 JSON 消息体解析并校验为备份任务，格式或版本非法时返回错误。
common::Result<BackupTask> parse_backup_task(const std::string& body);

} // namespace messaging
} // namespace webdisk

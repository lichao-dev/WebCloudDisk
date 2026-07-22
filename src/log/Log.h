#pragma once

#include "common/Result.h"
#include "config/Config.h"

#include <spdlog/spdlog.h>

namespace webdisk {
namespace log {

class Log final {
public:
    static common::Result<void> init(const config::Config::Log& config);

    // 关闭 spdlog 日志系统，释放 spdlog 管理的资源
    static void shutdown() noexcept;

    Log() = delete;
};

} // namespace log
} // namespace webdisk

// 统一从这里暴露日志接口，业务代码无需直接依赖 spdlog 的调用形式。
#define LOG_TRACE(...) SPDLOG_TRACE(__VA_ARGS__)
#define LOG_DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
#define LOG_INFO(...) SPDLOG_INFO(__VA_ARGS__)
#define LOG_WARN(...) SPDLOG_WARN(__VA_ARGS__)
#define LOG_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
#define LOG_CRITICAL(...) SPDLOG_CRITICAL(__VA_ARGS__)

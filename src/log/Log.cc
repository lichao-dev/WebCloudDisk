#include "log/Log.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace webdisk {
namespace log {

namespace {

std::optional<spdlog::level::level_enum> parse_level(std::string_view level) {
    if (level == "trace") {
        return spdlog::level::trace;
    }
    if (level == "debug") {
        return spdlog::level::debug;
    }
    if (level == "info") {
        return spdlog::level::info;
    }
    if (level == "warn" || level == "warning") {
        return spdlog::level::warn;
    }
    if (level == "error" || level == "err") {
        return spdlog::level::err;
    }
    if (level == "critical") {
        return spdlog::level::critical;
    }
    if (level == "off") {
        return spdlog::level::off;
    }

    return std::nullopt;
}

} // namespace

common::Result<void> Log::init(const config::Config::Log& config) {
    const auto level = parse_level(config.level);
    if (!level.has_value()) {
        return common::Result<void>::failure(500, "Invalid log level: " + config.level);
    }

    try {
        std::vector<spdlog::sink_ptr> sinks;
        if (config.console) {
            sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
        }

        if (!config.file.empty()) {
            const std::filesystem::path parent = config.file.parent_path();
            if (!parent.empty()) {
                std::error_code ec;
                std::filesystem::create_directories(parent, ec);
                if (ec) {
                    return common::Result<void>::failure(500, "Failed to create log directory: " + parent.string());
                }
            }
            // 每次启动先归档上一次运行的非空日志，同时继续按大小轮转并限制历史文件数量。
            sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                config.file.string(), static_cast<std::size_t>(config.roll_size), config.roll_files, true));
        }

        if (sinks.empty()) {
            return common::Result<void>::failure(500, "Logging requires at least one output sink");
        }

        auto logger = std::make_shared<spdlog::logger>("webdisk", sinks.begin(), sinks.end());
        logger->set_level(*level);
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [thread %t] [%s:%#] %v");
        // 错误日志立即刷新，普通日志定期刷新，兼顾可观测性与文件 I/O 开销。
        logger->flush_on(spdlog::level::err);

        spdlog::set_default_logger(std::move(logger));
        spdlog::flush_every(std::chrono::seconds{3});
        LOG_INFO("Logging initialized");

        return common::Result<void>::success();
    } catch (const std::exception& e) {
        return common::Result<void>::failure(500, "Log initialization failed: " + std::string(e.what()));
    }
}

void Log::shutdown() noexcept {
    try {
        spdlog::shutdown();
    } catch (...) {
        // shutdown 用于退出路径，不能让日志库异常阻止服务正常结束。
    }
}

} // namespace log
} // namespace webdisk

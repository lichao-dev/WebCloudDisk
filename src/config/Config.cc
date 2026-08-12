#include "config/Config.h"

#include <charconv>
#include <limits>
#include <sstream>
#include <system_error>

#include <INIReader.h>

namespace webdisk {
namespace config {
namespace {

// INIReader::GetInteger() 会把 "9527x" 这类字符串解析成部分有效的数字。
// 这里自行进行严格转换，确保任意非法配置都会在启动阶段被发现。
common::Result<uint64_t> parse_unsigned(const std::string& value, const std::string& key, uint64_t min, uint64_t max) {
    if (value.empty()) {
        return common::Result<uint64_t>::failure(500, "Missing configuration option: " + key);
    }

    uint64_t parsed = 0;
    const char* begin = value.data();
    const char* end = begin + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end || parsed < min || parsed > max) {
        return common::Result<uint64_t>::failure(500, "Invalid configuration option: " + key);
    }

    return common::Result<uint64_t>::success(parsed);
}

common::Result<bool> parse_boolean(const std::string& value, const std::string& key) {
    if (value == "true" || value == "1") {
        return common::Result<bool>::success(true);
    }
    if (value == "false" || value == "0") {
        return common::Result<bool>::success(false);
    }

    return common::Result<bool>::failure(500, "Invalid configuration option: " + key);
}

std::filesystem::path resolve_path(const std::filesystem::path& working_dir, const std::string& value) {
    std::filesystem::path path(value);
    if (path.is_relative()) {
        path = working_dir / path;
    }
    return path.lexically_normal();
}

} // namespace

common::Result<Config> Config::load(const std::filesystem::path& path) {
    std::error_code ec;
    // lexically_normal() 意思是把路径进行字面上的规范化处理，比如去掉 . 和正确处理 ..
    const std::filesystem::path abs_path = std::filesystem::absolute(path, ec).lexically_normal();
    if (ec) {
        return common::Result<Config>::failure(500, "Failed to resolve configuration file path: " + path.string());
    }

    INIReader reader(abs_path.string());
    if (reader.ParseError() == -1) {
        return common::Result<Config>::failure(500, "Failed to open configuration file: " + abs_path.string());
    }
    if (reader.ParseError() != 0) {
        return common::Result<Config>::failure(500, "INI syntax error at line: " + std::to_string(reader.ParseError()));
    }

    const std::filesystem::path working_dir = std::filesystem::current_path(ec).lexically_normal();
    if (ec) {
        return common::Result<Config>::failure(500, "Failed to resolve the process working directory");
    }

    Config config;

    auto server_port = parse_unsigned(reader.Get("server", "port", "9527"), "server.port", 1, 65535);
    auto database_port = parse_unsigned(reader.Get("database", "port", "3306"), "database.port", 1, 65535);
    auto retry_max = parse_unsigned(reader.Get("database", "retry_max", "3"), "database.retry_max", 0, 10);
    auto token_ttl =
        parse_unsigned(reader.Get("auth", "token_ttl_seconds", "3600"), "auth.token_ttl_seconds", 60, 604800);
    auto password_iterations = parse_unsigned(reader.Get("auth", "password_iterations", "600000"),
                                              "auth.password_iterations", 600000, std::numeric_limits<int>::max());
    auto max_file_size = parse_unsigned(reader.Get("storage", "max_file_size_bytes", "104857600"),
                                        "storage.max_file_size_bytes", 1, std::numeric_limits<uint64_t>::max());
    auto oss_enabled = parse_boolean(reader.Get("oss", "enabled", "false"), "oss.enabled");
    auto rabbitmq_port = parse_unsigned(reader.Get("rabbitmq", "port", "5672"), "rabbitmq.port", 1, 65535);
    auto log_console = parse_boolean(reader.Get("log", "console", "true"), "log.console");
    auto log_roll_size = parse_unsigned(reader.Get("log", "roll_size", "100000000"), "log.roll_size", 1,
                                        static_cast<uint64_t>(std::numeric_limits<size_t>::max()));
    auto log_roll_files = parse_unsigned(reader.Get("log", "roll_files", "5"), "log.roll_files", 1, 1000);

    const common::Result<uint64_t>* numeric_values[] = {
        &server_port,   &database_port, &retry_max,     &token_ttl,      &password_iterations,
        &max_file_size, &rabbitmq_port, &log_roll_size, &log_roll_files,
    };
    for (const auto* result : numeric_values) {
        if (!result->ok()) {
            return common::Result<Config>::failure(result->error().status_code, result->error().message);
        }
    }
    if (!log_console.ok()) {
        return common::Result<Config>::failure(log_console.error().status_code, log_console.error().message);
    }
    if (!oss_enabled.ok()) {
        return common::Result<Config>::failure(oss_enabled.error().status_code, oss_enabled.error().message);
    }
    config.server.port = static_cast<uint16_t>(server_port.value());
    // 项目约定从项目根目录启动，所有相对路径都以进程启动工作目录为基准。
    config.server.web_root = resolve_path(working_dir, reader.Get("server", "web_root", "./www"));

    config.database.host = reader.Get("database", "host", "127.0.0.1");
    config.database.port = static_cast<uint16_t>(database_port.value());
    config.database.username = reader.Get("database", "username", "");
    config.database.password = reader.Get("database", "password", "");
    config.database.database = reader.Get("database", "database", "");
    config.database.retry_max = static_cast<int>(retry_max.value());

    config.auth.jwt_secret = reader.Get("auth", "jwt_secret", "");
    config.auth.jwt_issuer = reader.Get("auth", "jwt_issuer", "web-cloud-disk");
    config.auth.token_ttl = std::chrono::seconds(token_ttl.value());
    config.auth.password_iterations = static_cast<int>(password_iterations.value());

    config.storage.root = resolve_path(working_dir, reader.Get("storage", "root", "./upload"));
    config.storage.max_file_size = max_file_size.value();

    config.oss.enabled = oss_enabled.value();
    config.oss.region = reader.Get("oss", "region", "");
    config.oss.bucket = reader.Get("oss", "bucket", "");
    config.oss.key_prefix = reader.Get("oss", "key_prefix", "backup/sha256/");

    config.rabbitmq.host = reader.Get("rabbitmq", "host", "127.0.0.1");
    config.rabbitmq.port = static_cast<uint16_t>(rabbitmq_port.value());
    config.rabbitmq.username = reader.Get("rabbitmq", "username", "");
    config.rabbitmq.password = reader.Get("rabbitmq", "password", "");
    config.rabbitmq.vhost = reader.Get("rabbitmq", "vhost", "/");
    config.rabbitmq.queue = reader.Get("rabbitmq", "queue", "webdisk.oss.backup.v1");

    config.log.level = reader.Get("log", "level", "info");
    config.log.console = log_console.value();
    const std::string log_file = reader.Get("log", "file", "./log/server.log");
    if (!log_file.empty()) {
        config.log.file = resolve_path(working_dir, log_file);
    }
    const std::string worker_log_file = reader.Get("log", "worker_file", "./log/cloud_disk_backup_worker.log");
    if (!worker_log_file.empty()) {
        config.log.worker_file = resolve_path(working_dir, worker_log_file);
    }
    config.log.roll_size = log_roll_size.value();
    config.log.roll_files = static_cast<size_t>(log_roll_files.value());

    if (config.database.host.empty() || config.database.username.empty() || config.database.database.empty()) {
        return common::Result<Config>::failure(
            500, "database.host, database.username, and database.database must not be empty");
    }
    if (config.auth.jwt_secret.size() < 32) {
        return common::Result<Config>::failure(500, "auth.jwt_secret must contain at least 32 characters");
    }
    if (config.auth.jwt_issuer.empty()) {
        return common::Result<Config>::failure(500, "auth.jwt_issuer must not be empty");
    }
    if (config.oss.enabled) {
        if (config.oss.region.empty() || config.oss.bucket.empty()) {
            return common::Result<Config>::failure(
                500, "oss.region and oss.bucket must not be empty when OSS backup is enabled");
        }
        if (config.oss.key_prefix.empty() || config.oss.key_prefix.front() == '/') {
            return common::Result<Config>::failure(500, "oss.key_prefix must be a non-empty relative object prefix");
        }
        if (config.oss.key_prefix.back() != '/') {
            config.oss.key_prefix.push_back('/');
        }
    }
    if (config.oss.enabled &&
        (config.rabbitmq.host.empty() || config.rabbitmq.username.empty() || config.rabbitmq.password.empty() ||
         config.rabbitmq.vhost.empty() || config.rabbitmq.queue.empty())) {
        return common::Result<Config>::failure(
            500, "rabbitmq.host, username, password, vhost, and queue must not be empty when OSS backup is enabled");
    }
    if (!config.log.console && config.log.file.empty()) {
        return common::Result<Config>::failure(500, "log.console and log.file cannot both be disabled");
    }
    if (config.oss.enabled && !config.log.console && config.log.worker_file.empty()) {
        return common::Result<Config>::failure(500, "log.console and log.worker_file cannot both be disabled");
    }

    return common::Result<Config>::success(std::move(config));
}

std::string Config::to_string() const {
    std::ostringstream output;
    output << std::boolalpha << "server{port=" << server.port << ", web_root=" << server.web_root.string() << "} "
           << "database{host=" << database.host << ", port=" << database.port << ", database=" << database.database
           << ", retry_max=" << database.retry_max << ", username_configured=" << !database.username.empty()
           << ", password_configured=" << !database.password.empty() << "} "
           << "auth{jwt_issuer=" << auth.jwt_issuer << ", token_ttl_seconds=" << auth.token_ttl.count()
           << ", password_iterations=" << auth.password_iterations
           << ", jwt_secret_configured=" << !auth.jwt_secret.empty() << "} "
           << "storage{root=" << storage.root.string() << ", max_file_size=" << storage.max_file_size << "} "
           << "oss{enabled=" << oss.enabled << ", region=" << oss.region << ", bucket=" << oss.bucket
           << ", key_prefix=" << oss.key_prefix << ", credentials_provider=environment} "
           << "rabbitmq{host=" << rabbitmq.host << ", port=" << rabbitmq.port << ", vhost=" << rabbitmq.vhost
           << ", queue=" << rabbitmq.queue
           << ", username_configured=" << !rabbitmq.username.empty()
           << ", password_configured=" << !rabbitmq.password.empty() << "} "
           << "log{level=" << log.level << ", console=" << log.console
           << ", file=" << (log.file.empty() ? "disabled" : log.file.string()) << ", roll_size=" << log.roll_size
           << ", worker_file=" << (log.worker_file.empty() ? "disabled" : log.worker_file.string())
           << ", roll_files=" << log.roll_files << "}";
    return output.str();
}

} // namespace config
} // namespace webdisk

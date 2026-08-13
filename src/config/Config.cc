#include "config/Config.h"

#include <charconv>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>

#include <INIReader.h>

namespace webdisk {
namespace config {
namespace {

enum class ConsulScope {
    gateway,
    user_service,
    file_service,
};

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

bool valid_consul_url(const std::string& value) {
    size_t authority_begin = 0;
    if (value.compare(0, 7, "http://") == 0) {
        authority_begin = 7;
    } else if (value.compare(0, 8, "https://") == 0) {
        authority_begin = 8;
    } else {
        return false;
    }

    if (authority_begin == value.size() || value.find_first_of(" \t\r\n", authority_begin) != std::string::npos ||
        value.find('@', authority_begin) != std::string::npos) {
        return false;
    }

    const size_t suffix = value.find_first_of("/?#", authority_begin);
    return suffix == std::string::npos || (suffix == value.size() - 1 && value[suffix] == '/');
}

std::filesystem::path resolve_path(const std::filesystem::path& working_dir, const std::string& value) {
    std::filesystem::path path(value);
    if (path.is_relative()) {
        path = working_dir / path;
    }
    return path.lexically_normal();
}

template <typename T, typename U>
common::Result<T> forward_error(const common::Result<U>& result) {
    return common::Result<T>::failure(result.error().status_code, result.error().message);
}

template <typename T, typename Loader>
common::Result<T> load_config_file(const std::filesystem::path& path, Loader loader) {
    std::error_code error;
    const std::filesystem::path absolute_path = std::filesystem::absolute(path, error).lexically_normal();
    if (error) {
        return common::Result<T>::failure(500, "Failed to resolve configuration file path: " + path.string());
    }

    INIReader reader(absolute_path.string());
    if (reader.ParseError() == -1) {
        return common::Result<T>::failure(500, "Failed to open configuration file: " + absolute_path.string());
    }
    if (reader.ParseError() != 0) {
        return common::Result<T>::failure(500, "INI syntax error at line: " + std::to_string(reader.ParseError()));
    }

    // 项目约定从项目根目录启动，配置中的相对路径以进程工作目录为基准。
    const std::filesystem::path working_dir = std::filesystem::current_path(error).lexically_normal();
    if (error) {
        return common::Result<T>::failure(500, "Failed to resolve the process working directory");
    }

    return loader(reader, working_dir);
}

common::Result<Server> load_server(const INIReader& reader, const std::filesystem::path& working_dir) {
    auto port = parse_unsigned(reader.Get("server", "port", "9527"), "server.port", 1, 65535);
    if (!port) {
        return forward_error<Server>(port);
    }

    Server config;
    config.port = static_cast<uint16_t>(port.value());
    config.web_root = resolve_path(working_dir, reader.Get("server", "web_root", "./www"));
    return common::Result<Server>::success(std::move(config));
}

common::Result<Database> load_database(const INIReader& reader) {
    auto port = parse_unsigned(reader.Get("database", "port", "3306"), "database.port", 1, 65535);
    auto retry_max = parse_unsigned(reader.Get("database", "retry_max", "3"), "database.retry_max", 0, 10);
    if (!port) {
        return forward_error<Database>(port);
    }
    if (!retry_max) {
        return forward_error<Database>(retry_max);
    }

    Database config;
    config.host = reader.Get("database", "host", "127.0.0.1");
    config.port = static_cast<uint16_t>(port.value());
    config.username = reader.Get("database", "username", "");
    config.password = reader.Get("database", "password", "");
    config.database = reader.Get("database", "database", "");
    config.retry_max = static_cast<int>(retry_max.value());
    if (config.host.empty() || config.username.empty() || config.database.empty()) {
        return common::Result<Database>::failure(
            500, "database.host, database.username, and database.database must not be empty");
    }
    return common::Result<Database>::success(std::move(config));
}

common::Result<Auth> load_auth(const INIReader& reader, bool load_token_ttl, bool load_password_iterations) {
    Auth config;
    config.jwt_secret = reader.Get("auth", "jwt_secret", "");
    config.jwt_issuer = reader.Get("auth", "jwt_issuer", "web-cloud-disk");
    if (config.jwt_secret.size() < 32) {
        return common::Result<Auth>::failure(500, "auth.jwt_secret must contain at least 32 characters");
    }
    if (config.jwt_issuer.empty()) {
        return common::Result<Auth>::failure(500, "auth.jwt_issuer must not be empty");
    }

    if (load_token_ttl) {
        auto token_ttl =
            parse_unsigned(reader.Get("auth", "token_ttl_seconds", "3600"), "auth.token_ttl_seconds", 60, 604800);
        if (!token_ttl) {
            return forward_error<Auth>(token_ttl);
        }
        config.token_ttl = std::chrono::seconds(token_ttl.value());
    }
    if (load_password_iterations) {
        auto iterations = parse_unsigned(reader.Get("auth", "password_iterations", "600000"),
                                         "auth.password_iterations", 600000, std::numeric_limits<int>::max());
        if (!iterations) {
            return forward_error<Auth>(iterations);
        }
        config.password_iterations = static_cast<int>(iterations.value());
    }
    return common::Result<Auth>::success(std::move(config));
}

common::Result<Storage> load_storage(const INIReader& reader, const std::filesystem::path& working_dir, bool load_root,
                                     bool load_max_file_size) {
    Storage config;
    if (load_root) {
        config.root = resolve_path(working_dir, reader.Get("storage", "root", "./upload"));
    }
    if (load_max_file_size) {
        auto max_file_size = parse_unsigned(reader.Get("storage", "max_file_size_bytes", "104857600"),
                                            "storage.max_file_size_bytes", 1, std::numeric_limits<uint64_t>::max());
        if (!max_file_size) {
            return forward_error<Storage>(max_file_size);
        }
        config.max_file_size = max_file_size.value();
    }
    return common::Result<Storage>::success(std::move(config));
}

common::Result<Oss> load_oss(const INIReader& reader) {
    Oss config;
    config.region = reader.Get("oss", "region", "");
    config.bucket = reader.Get("oss", "bucket", "");
    config.key_prefix = reader.Get("oss", "key_prefix", "backup/sha256/");
    if (config.region.empty() || config.bucket.empty()) {
        return common::Result<Oss>::failure(500, "oss.region and oss.bucket must not be empty");
    }
    if (config.key_prefix.empty() || config.key_prefix.front() == '/') {
        return common::Result<Oss>::failure(500, "oss.key_prefix must be a non-empty relative object prefix");
    }
    if (config.key_prefix.back() != '/') {
        config.key_prefix.push_back('/');
    }
    return common::Result<Oss>::success(std::move(config));
}

common::Result<RabbitMq> load_rabbitmq(const INIReader& reader, bool required) {
    auto port = parse_unsigned(reader.Get("rabbitmq", "port", "5672"), "rabbitmq.port", 1, 65535);
    if (!port) {
        return forward_error<RabbitMq>(port);
    }

    RabbitMq config;
    config.host = reader.Get("rabbitmq", "host", "127.0.0.1");
    config.port = static_cast<uint16_t>(port.value());
    config.username = reader.Get("rabbitmq", "username", "");
    config.password = reader.Get("rabbitmq", "password", "");
    config.vhost = reader.Get("rabbitmq", "vhost", "/");
    config.queue = reader.Get("rabbitmq", "queue", "webdisk.oss.backup.v1");
    if (required && (config.host.empty() || config.username.empty() || config.password.empty() ||
                     config.vhost.empty() || config.queue.empty())) {
        return common::Result<RabbitMq>::failure(
            500, "rabbitmq.host, username, password, vhost, and queue must not be empty when backup is enabled");
    }
    return common::Result<RabbitMq>::success(std::move(config));
}

common::Result<Rpc> load_gateway_rpc(const INIReader& reader) {
    auto timeout =
        parse_unsigned(reader.Get("rpc", "request_timeout_ms", "120000"), "rpc.request_timeout_ms", 1000, 600000);
    if (!timeout) {
        return forward_error<Rpc>(timeout);
    }

    Rpc config;
    config.request_timeout_ms = static_cast<int>(timeout.value());
    return common::Result<Rpc>::success(std::move(config));
}

common::Result<Rpc> load_service_rpc(const INIReader& reader, bool user_service) {
    const std::string port_key = user_service ? "user_service_port" : "file_service_port";
    const std::string full_port_key = "rpc." + port_key;
    auto port = parse_unsigned(reader.Get("rpc", port_key, user_service ? "9601" : "9602"), full_port_key, 1, 65535);
    if (!port) {
        return forward_error<Rpc>(port);
    }

    Rpc config;
    if (user_service) {
        config.user_service_host = reader.Get("rpc", "user_service_host", "127.0.0.1");
        config.user_service_port = static_cast<uint16_t>(port.value());
        if (config.user_service_host.empty()) {
            return common::Result<Rpc>::failure(500, "rpc.user_service_host must not be empty");
        }
    } else {
        config.file_service_host = reader.Get("rpc", "file_service_host", "127.0.0.1");
        config.file_service_port = static_cast<uint16_t>(port.value());
        if (config.file_service_host.empty()) {
            return common::Result<Rpc>::failure(500, "rpc.file_service_host must not be empty");
        }
    }
    return common::Result<Rpc>::success(std::move(config));
}

common::Result<Consul> load_consul(const INIReader& reader, ConsulScope scope) {
    auto retry_max = parse_unsigned(reader.Get("consul", "retry_max", "3"), "consul.retry_max", 0, 10);
    if (!retry_max) {
        return forward_error<Consul>(retry_max);
    }

    Consul config;
    config.url = reader.Get("consul", "url", "http://127.0.0.1:8500");
    if (config.url.size() > 1 && config.url.back() == '/') {
        config.url.pop_back();
    }
    config.datacenter = reader.Get("consul", "datacenter", "dc1");
    config.token = reader.Get("consul", "token", "");
    config.retry_max = static_cast<int>(retry_max.value());
    if (!valid_consul_url(config.url)) {
        return common::Result<Consul>::failure(
            500, "consul.url must contain only an HTTP or HTTPS origin without credentials");
    }
    if (config.datacenter.empty()) {
        return common::Result<Consul>::failure(500, "consul.datacenter must not be empty");
    }

    if (scope == ConsulScope::gateway || scope == ConsulScope::user_service) {
        config.user_service_name = reader.Get("consul", "user_service_name", "webdisk-user-service");
        if (config.user_service_name.empty()) {
            return common::Result<Consul>::failure(500, "consul.user_service_name must not be empty");
        }
    }
    if (scope == ConsulScope::gateway || scope == ConsulScope::file_service) {
        config.file_service_name = reader.Get("consul", "file_service_name", "webdisk-file-service");
        if (config.file_service_name.empty()) {
            return common::Result<Consul>::failure(500, "consul.file_service_name must not be empty");
        }
    }

    if (scope != ConsulScope::gateway) {
        auto interval = parse_unsigned(reader.Get("consul", "health_check_interval_ms", "5000"),
                                       "consul.health_check_interval_ms", 1000, 600000);
        auto timeout = parse_unsigned(reader.Get("consul", "health_check_timeout_ms", "2000"),
                                      "consul.health_check_timeout_ms", 100, 600000);
        auto deregister = parse_unsigned(reader.Get("consul", "deregister_critical_after_ms", "600000"),
                                         "consul.deregister_critical_after_ms", 60000, 86400000);
        if (!interval) {
            return forward_error<Consul>(interval);
        }
        if (!timeout) {
            return forward_error<Consul>(timeout);
        }
        if (!deregister) {
            return forward_error<Consul>(deregister);
        }

        config.health_check_host = reader.Get("consul", "health_check_host", "host.docker.internal");
        config.health_check_interval_ms = static_cast<int>(interval.value());
        config.health_check_timeout_ms = static_cast<int>(timeout.value());
        config.deregister_critical_after_ms = static_cast<int>(deregister.value());
        if (config.health_check_host.empty()) {
            return common::Result<Consul>::failure(500, "consul.health_check_host must not be empty");
        }
        if (config.health_check_timeout_ms > config.health_check_interval_ms) {
            return common::Result<Consul>::failure(
                500, "consul.health_check_timeout_ms must not exceed health_check_interval_ms");
        }
    }

    return common::Result<Consul>::success(std::move(config));
}

common::Result<Log> load_log(const INIReader& reader, const std::filesystem::path& working_dir,
                             const std::string& default_file) {
    auto console = parse_boolean(reader.Get("log", "console", "true"), "log.console");
    auto roll_size = parse_unsigned(reader.Get("log", "roll_size", "100000000"), "log.roll_size", 1,
                                    static_cast<uint64_t>(std::numeric_limits<size_t>::max()));
    auto roll_files = parse_unsigned(reader.Get("log", "roll_files", "5"), "log.roll_files", 1, 1000);
    if (!console) {
        return forward_error<Log>(console);
    }
    if (!roll_size) {
        return forward_error<Log>(roll_size);
    }
    if (!roll_files) {
        return forward_error<Log>(roll_files);
    }

    Log config;
    config.level = reader.Get("log", "level", "info");
    config.console = console.value();
    const std::string file = reader.Get("log", "file", default_file);
    if (!file.empty()) {
        config.file = resolve_path(working_dir, file);
    }
    config.roll_size = roll_size.value();
    config.roll_files = static_cast<size_t>(roll_files.value());
    if (!config.console && config.file.empty()) {
        return common::Result<Log>::failure(500, "log.console and log.file cannot both be disabled");
    }
    return common::Result<Log>::success(std::move(config));
}

std::string database_summary(const Database& config) {
    std::ostringstream output;
    output << std::boolalpha << "database{host=" << config.host << ", port=" << config.port
           << ", database=" << config.database << ", retry_max=" << config.retry_max
           << ", username_configured=" << !config.username.empty()
           << ", password_configured=" << !config.password.empty() << "}";
    return output.str();
}

std::string auth_summary(const Auth& config, bool include_token_ttl, bool include_password_iterations) {
    std::ostringstream output;
    output << std::boolalpha << "auth{jwt_issuer=" << config.jwt_issuer;
    if (include_token_ttl) {
        output << ", token_ttl_seconds=" << config.token_ttl.count();
    }
    if (include_password_iterations) {
        output << ", password_iterations=" << config.password_iterations;
    }
    output << ", jwt_secret_configured=" << !config.jwt_secret.empty() << "}";
    return output.str();
}

std::string rabbitmq_summary(const RabbitMq& config) {
    std::ostringstream output;
    output << std::boolalpha << "rabbitmq{host=" << config.host << ", port=" << config.port
           << ", vhost=" << config.vhost << ", queue=" << config.queue
           << ", username_configured=" << !config.username.empty()
           << ", password_configured=" << !config.password.empty() << "}";
    return output.str();
}

std::string consul_summary(const Consul& config, ConsulScope scope) {
    std::ostringstream output;
    output << std::boolalpha << "consul{url=" << config.url << ", datacenter=" << config.datacenter
           << ", token_configured=" << !config.token.empty();
    if (scope == ConsulScope::gateway || scope == ConsulScope::user_service) {
        output << ", user_service_name=" << config.user_service_name;
    }
    if (scope == ConsulScope::gateway || scope == ConsulScope::file_service) {
        output << ", file_service_name=" << config.file_service_name;
    }
    if (scope != ConsulScope::gateway) {
        output << ", health_check_host=" << config.health_check_host
               << ", health_check_interval_ms=" << config.health_check_interval_ms
               << ", health_check_timeout_ms=" << config.health_check_timeout_ms
               << ", deregister_critical_after_ms=" << config.deregister_critical_after_ms;
    }
    output << ", retry_max=" << config.retry_max << "}";
    return output.str();
}

std::string log_summary(const Log& config) {
    std::ostringstream output;
    output << std::boolalpha << "log{level=" << config.level << ", console=" << config.console
           << ", file=" << (config.file.empty() ? "disabled" : config.file.string())
           << ", roll_size=" << config.roll_size << ", roll_files=" << config.roll_files << "}";
    return output.str();
}

} // namespace

common::Result<GatewayConfig> GatewayConfig::load(const std::filesystem::path& path) {
    return load_config_file<GatewayConfig>(path, [](const INIReader& reader, const std::filesystem::path& working_dir) {
        auto server = load_server(reader, working_dir);
        auto auth = load_auth(reader, false, false);
        auto storage = load_storage(reader, working_dir, false, true);
        auto rpc = load_gateway_rpc(reader);
        auto consul = load_consul(reader, ConsulScope::gateway);
        auto log = load_log(reader, working_dir, "./log/cloud_disk_api_gateway.log");
        if (!server)
            return forward_error<GatewayConfig>(server);
        if (!auth)
            return forward_error<GatewayConfig>(auth);
        if (!storage)
            return forward_error<GatewayConfig>(storage);
        if (!rpc)
            return forward_error<GatewayConfig>(rpc);
        if (!consul)
            return forward_error<GatewayConfig>(consul);
        if (!log)
            return forward_error<GatewayConfig>(log);

        GatewayConfig config;
        config.server = server.take_value();
        config.auth = auth.take_value();
        config.storage = storage.take_value();
        config.rpc = rpc.take_value();
        config.consul = consul.take_value();
        config.log = log.take_value();
        return common::Result<GatewayConfig>::success(std::move(config));
    });
}

common::Result<UserServiceConfig> UserServiceConfig::load(const std::filesystem::path& path) {
    return load_config_file<UserServiceConfig>(
        path, [](const INIReader& reader, const std::filesystem::path& working_dir) {
            auto database = load_database(reader);
            auto auth = load_auth(reader, true, true);
            auto rpc = load_service_rpc(reader, true);
            auto consul = load_consul(reader, ConsulScope::user_service);
            auto log = load_log(reader, working_dir, "./log/cloud_disk_user_service.log");
            if (!database)
                return forward_error<UserServiceConfig>(database);
            if (!auth)
                return forward_error<UserServiceConfig>(auth);
            if (!rpc)
                return forward_error<UserServiceConfig>(rpc);
            if (!consul)
                return forward_error<UserServiceConfig>(consul);
            if (!log)
                return forward_error<UserServiceConfig>(log);

            UserServiceConfig config;
            config.database = database.take_value();
            config.auth = auth.take_value();
            config.rpc = rpc.take_value();
            config.consul = consul.take_value();
            config.log = log.take_value();
            return common::Result<UserServiceConfig>::success(std::move(config));
        });
}

common::Result<FileServiceConfig> FileServiceConfig::load(const std::filesystem::path& path) {
    return load_config_file<FileServiceConfig>(
        path, [](const INIReader& reader, const std::filesystem::path& working_dir) {
            auto database = load_database(reader);
            auto storage = load_storage(reader, working_dir, true, true);
            auto backup_enabled = parse_boolean(reader.Get("backup", "enabled", "true"), "backup.enabled");
            if (!database)
                return forward_error<FileServiceConfig>(database);
            if (!storage)
                return forward_error<FileServiceConfig>(storage);
            if (!backup_enabled)
                return forward_error<FileServiceConfig>(backup_enabled);
            auto rpc = load_service_rpc(reader, false);
            auto consul = load_consul(reader, ConsulScope::file_service);
            auto log = load_log(reader, working_dir, "./log/cloud_disk_file_service.log");
            if (!rpc)
                return forward_error<FileServiceConfig>(rpc);
            if (!consul)
                return forward_error<FileServiceConfig>(consul);
            if (!log)
                return forward_error<FileServiceConfig>(log);

            FileServiceConfig config;
            config.database = database.take_value();
            config.storage = storage.take_value();
            config.backup_enabled = backup_enabled.value();
            // 备份关闭时完全不读取 RabbitMQ，避免未启用功能的残留配置阻止文件服务启动。
            if (config.backup_enabled) {
                auto rabbitmq = load_rabbitmq(reader, true);
                if (!rabbitmq)
                    return forward_error<FileServiceConfig>(rabbitmq);
                config.rabbitmq = rabbitmq.take_value();
            }
            config.rpc = rpc.take_value();
            config.consul = consul.take_value();
            config.log = log.take_value();
            return common::Result<FileServiceConfig>::success(std::move(config));
        });
}

common::Result<BackupWorkerConfig> BackupWorkerConfig::load(const std::filesystem::path& path) {
    return load_config_file<BackupWorkerConfig>(path, [](const INIReader& reader,
                                                         const std::filesystem::path& working_dir) {
        auto storage = load_storage(reader, working_dir, true, false);
        auto oss = load_oss(reader);
        if (!storage)
            return forward_error<BackupWorkerConfig>(storage);
        if (!oss)
            return forward_error<BackupWorkerConfig>(oss);
        auto rabbitmq = load_rabbitmq(reader, true);
        auto log = load_log(reader, working_dir, "./log/cloud_disk_backup_worker.log");
        if (!rabbitmq)
            return forward_error<BackupWorkerConfig>(rabbitmq);
        if (!log)
            return forward_error<BackupWorkerConfig>(log);

        BackupWorkerConfig config;
        config.storage = storage.take_value();
        config.oss = oss.take_value();
        config.rabbitmq = rabbitmq.take_value();
        config.log = log.take_value();
        return common::Result<BackupWorkerConfig>::success(std::move(config));
    });
}

std::string GatewayConfig::to_string() const {
    std::ostringstream output;
    output << std::boolalpha << "server{port=" << server.port << ", web_root=" << server.web_root.string() << "} "
           << auth_summary(auth, false, false) << " storage{max_file_size=" << storage.max_file_size << "} "
           << "rpc{request_timeout_ms=" << rpc.request_timeout_ms << "} "
           << consul_summary(consul, ConsulScope::gateway) << " " << log_summary(log);
    return output.str();
}

std::string UserServiceConfig::to_string() const {
    std::ostringstream output;
    output << std::boolalpha << database_summary(database) << " " << auth_summary(auth, true, true)
           << " rpc{user_service=" << rpc.user_service_host << ':' << rpc.user_service_port << "} "
           << consul_summary(consul, ConsulScope::user_service) << " " << log_summary(log);
    return output.str();
}

std::string FileServiceConfig::to_string() const {
    std::ostringstream output;
    output << std::boolalpha << database_summary(database) << " storage{root=" << storage.root.string()
           << ", max_file_size=" << storage.max_file_size << "} backup{enabled=" << backup_enabled << "} ";
    if (backup_enabled) {
        output << rabbitmq_summary(rabbitmq) << " ";
    }
    output << "rpc{file_service=" << rpc.file_service_host << ':' << rpc.file_service_port << "} "
           << consul_summary(consul, ConsulScope::file_service) << " " << log_summary(log);
    return output.str();
}

std::string BackupWorkerConfig::to_string() const {
    std::ostringstream output;
    output << std::boolalpha << "storage{root=" << storage.root.string() << "} "
           << "oss{region=" << oss.region << ", bucket=" << oss.bucket
           << ", key_prefix=" << oss.key_prefix << ", credentials_provider=environment} " << rabbitmq_summary(rabbitmq)
           << " " << log_summary(log);
    return output.str();
}

} // namespace config
} // namespace webdisk

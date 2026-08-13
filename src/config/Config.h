#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include "common/Result.h"

namespace webdisk {
namespace config {

// 各进程复用这些配置段；具体加载和校验由对应的进程配置负责。
struct Server {
    uint16_t port{9527};
    std::filesystem::path web_root; // Web 静态文件根目录
};

struct Database {
    std::string host{"127.0.0.1"};
    uint16_t port{3306};
    std::string username;
    std::string password;
    std::string database;
    int retry_max{3};
};

struct Auth {
    std::string jwt_secret; // JWT 签名密钥
    std::string jwt_issuer{"web-cloud-disk"}; // JWT 签发者
    std::chrono::seconds token_ttl{3600}; // Token 有效时间
    int password_iterations{600000}; // PBKDF2-HMAC-SHA256 密码哈希迭代次数
};

struct Storage {
    std::filesystem::path root; // 上传文件存储根目录
    uint64_t max_file_size{100ULL * 1024ULL * 1024ULL}; // 单个文件最大上传大小
};

struct Oss {
    std::string region;
    std::string bucket;
    std::string key_prefix{"backup/sha256/"}; // OSS 对象名前缀
};

struct RabbitMq {
    std::string host{"127.0.0.1"};
    uint16_t port{5672};
    std::string username;
    std::string password;
    std::string vhost{"/"};
    std::string queue{"webdisk.oss.backup.v1"};
};

struct Rpc {
    std::string user_service_host{"127.0.0.1"};
    uint16_t user_service_port{9601};
    std::string file_service_host{"127.0.0.1"};
    uint16_t file_service_port{9602};
    int request_timeout_ms{120000};
};

struct Consul {
    std::string url{"http://127.0.0.1:8500"}; // Consul HTTP API 地址
    std::string datacenter{"dc1"}; // Consul 数据中心名称
    std::string token; // Consul ACL Token；未启用 ACL 时可留空
    std::string user_service_name{"webdisk-user-service"};
    std::string file_service_name{"webdisk-file-service"};
    // Docker 中的 Consul Agent 通过该主机名检查 macOS 宿主机上的 RPC 端口。
    std::string health_check_host{"host.docker.internal"};
    int retry_max{3};
    int health_check_interval_ms{5000};
    int health_check_timeout_ms{2000};
    int deregister_critical_after_ms{600000};
};

struct Log {
    std::string level{"info"};
    bool console{true};
    std::filesystem::path file; // 当前进程独占的滚动日志文件
    uint64_t roll_size{100000000};
    size_t roll_files{5};
};

class GatewayConfig {
public:
    static common::Result<GatewayConfig> load(const std::filesystem::path& path);
    // 只输出网关使用的配置，凭据和签名密钥仅显示是否已配置。
    std::string to_string() const;

    Server server;
    Auth auth;
    Storage storage;
    Rpc rpc;
    Consul consul;
    Log log;
};

class UserServiceConfig {
public:
    static common::Result<UserServiceConfig> load(const std::filesystem::path& path);
    std::string to_string() const;

    Database database;
    Auth auth;
    Rpc rpc;
    Consul consul;
    Log log;
};

class FileServiceConfig {
public:
    static common::Result<FileServiceConfig> load(const std::filesystem::path& path);
    std::string to_string() const;

    Database database;
    Storage storage;
    bool backup_enabled{true};
    RabbitMq rabbitmq;
    Rpc rpc;
    Consul consul;
    Log log;
};

class BackupWorkerConfig {
public:
    static common::Result<BackupWorkerConfig> load(const std::filesystem::path& path);
    std::string to_string() const;

    Storage storage;
    Oss oss;
    RabbitMq rabbitmq;
    Log log;
};

} // namespace config
} // namespace webdisk

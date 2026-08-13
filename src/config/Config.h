#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include "common/Result.h"

namespace webdisk {
namespace config {

// 配置类
class Config {
public:
    struct Server {
        uint16_t port{9527};
        std::filesystem::path web_root; // web静态文件根目录
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
        bool enabled{false}; // 是否通过 RabbitMQ 异步备份本地文件到 OSS
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
        std::string url{"http://127.0.0.1:8500"}; // Consul HTTP API 地址。
        std::string datacenter{"dc1"}; // Consul 数据中心名称，用于服务注册与查询
        std::string token; // Consul ACL Token；未启用 ACL 时可留空
        std::string user_service_name{"webdisk-user-service"}; // 用户服务在 Consul 中注册的服务名称
        std::string file_service_name{"webdisk-file-service"}; // 文件服务在 Consul 中注册的服务名称
        // Docker 中的 Consul Agent 通过该主机名检查 macOS 宿主机上的 RPC 端口。
        std::string health_check_host{"host.docker.internal"};
        int retry_max{3}; // Consul 请求失败后的最大重试次数
        int health_check_interval_ms{5000}; // 健康检查执行间隔，单位：毫秒
        int health_check_timeout_ms{2000}; // 单次健康检查的超时时间，单位：毫秒
        int deregister_critical_after_ms{600000}; // 服务持续处于 critical 状态超过该时间后自动注销，单位：毫秒
    };

    struct Log {
        std::string level{"info"};
        bool console{true};
        // Log::init() 使用的当前进程输出路径，由各进程从下面的专用路径中选择。
        std::filesystem::path file;
        std::filesystem::path worker_file;
        std::filesystem::path gateway_file;
        std::filesystem::path user_service_file;
        std::filesystem::path file_service_file;
        uint64_t roll_size{100000000}; // 日志文件滚动大小
        size_t roll_files{5}; // 日志文件滚动数量
    };

    static common::Result<Config> load(const std::filesystem::path& path);
    // 返回适合写入日志的配置摘要，凭据和签名密钥只显示是否已配置。
    std::string to_string() const;

    Server server;
    Database database;
    Auth auth;
    Storage storage;
    Oss oss;
    RabbitMq rabbitmq;
    Rpc rpc;
    Consul consul;
    Log log;
};

} // namespace config
} // namespace webdisk

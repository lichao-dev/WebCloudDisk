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
        bool enabled{false}; // 是否同步备份本地文件到 OSS
        std::string region;
        std::string bucket;
        std::string key_prefix{"backup/sha256/"}; // OSS 对象名前缀
    };

    struct Log {
        std::string level{"info"};
        bool console{true};
        std::filesystem::path file;
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
    Log log;
};

} // namespace config
} // namespace webdisk

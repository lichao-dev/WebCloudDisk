#include <csignal>
#include <filesystem>
#include <iostream>
#include <string_view>

#include <workflow/WFFacilities.h>

#include "config/Config.h"
#include "database/MySqlClient.h"
#include "discovery/ConsulServiceRegistrar.h"
#include "log/LogShutdownGuard.h"
#include "repository/UserRepository.h"
#include "rpc/UserRpcServiceImpl.h"
#include "security/JwtService.h"
#include "security/PasswordHasher.h"
#include "service/AuthService.h"
#include "service/UserService.h"

namespace {

WFFacilities::WaitGroup wait_group{1};

void signal_handler(int) {
    wait_group.done();
}

webdisk::common::Result<std::filesystem::path> parse_config_path(int argc, char* argv[]) {
    if (argc != 3 || std::string_view(argv[1]) != "--config" || std::string_view(argv[2]).empty()) {
        return webdisk::common::Result<std::filesystem::path>::failure(
            500, "Usage: cloud_disk_user_service --config <user-service.ini>");
    }
    return webdisk::common::Result<std::filesystem::path>::success(argv[2]);
}

} // namespace

int main(int argc, char* argv[]) {
    auto config_path = parse_config_path(argc, argv);
    if (!config_path) {
        std::cerr << config_path.error().message << '\n';
        return 1;
    }

    auto config_result = webdisk::config::UserServiceConfig::load(config_path.value());
    if (!config_result) {
        std::cerr << config_result.error().message << '\n';
        return 1;
    }
    webdisk::config::UserServiceConfig config = config_result.take_value();

    auto log_result = webdisk::log::Log::init(config.log);
    if (!log_result) {
        std::cerr << log_result.error().message << '\n';
        return 1;
    }
    webdisk::log::LogShutdownGuard log_shutdown_guard;
    LOG_INFO("Configuration: {}", config.to_string());

    // 初始化用户服务所依赖的数据库、Repository、安全组件和业务服务
    webdisk::db::MySqlClient database{config.database};
    webdisk::repository::UserRepository users{database};
    webdisk::security::PasswordHasher password_hasher{config.auth.password_iterations};
    webdisk::security::JwtService jwt_service{config.auth.jwt_secret, config.auth.jwt_issuer, config.auth.token_ttl};
    webdisk::service::AuthService auth_service{users, password_hasher, jwt_service};
    webdisk::service::UserService user_service{users};
    webdisk::rpcserver::UserRpcServiceImpl rpc_service{auth_service, user_service};

    srpc::RPCServerParams server_params = srpc::RPC_SERVER_PARAMS_DEFAULT;
    // 用户相关 RPC 请求体较小，限制单次请求最大为 1 MiB
    server_params.request_size_limit = 1024ULL * 1024ULL;
    srpc::SRPCServer server{&server_params};
    if (server.add_service(&rpc_service) != 0) {
        LOG_ERROR("Failed to register user RPC service");
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (server.start(config.rpc.user_service_port) != 0) {
        LOG_ERROR("User RPC service failed to start on port {}", config.rpc.user_service_port);
        return 1;
    }

    LOG_INFO("User RPC service listening on port {}", config.rpc.user_service_port);

    // RPC 服务启动成功后再注册到 Consul，避免不可用实例被其他服务提前发现
    auto registrar_result = webdisk::discovery::ConsulServiceRegistrar::create(config.consul);
    if (!registrar_result) {
        LOG_ERROR("User RPC service Consul client initialization failed: status={}",
                  registrar_result.error().status_code);
        server.stop();
        return 1;
    }
    auto registrar = registrar_result.take_value();
    auto registration = registrar->register_service(config.consul.user_service_name, config.rpc.user_service_host,
                                                    config.rpc.user_service_port);
    if (!registration) {
        LOG_ERROR("User RPC service Consul registration failed: status={}, error={}", registration.error().status_code,
                  registration.error().message);
        server.stop();
        return 1;
    }
    LOG_INFO("User RPC service registered with Consul: service={}, instance={}", config.consul.user_service_name,
             registrar->instance_id());

    wait_group.wait();

    // 退出时先从 Consul 注销，避免其他服务继续发现正在关闭的实例
    auto deregistration = registrar->deregister_service();
    if (!deregistration) {
        LOG_WARN("User RPC service Consul deregistration failed: status={}, error={}",
                 deregistration.error().status_code, deregistration.error().message);
    } else {
        LOG_INFO("User RPC service deregistered from Consul");
    }

    // Consul 注销完成后再停止 RPC 服务
    server.stop();
    LOG_INFO("User RPC service stopped");

    return 0;
}

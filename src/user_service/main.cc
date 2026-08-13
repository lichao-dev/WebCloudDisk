#include <csignal>
#include <filesystem>
#include <iostream>
#include <string_view>

#include <workflow/WFFacilities.h>

#include "config/Config.h"
#include "database/MySqlClient.h"
#include "log/Log.h"
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
            500, "Usage: cloud_disk_user_service --config <server.ini>");
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

    auto config_result = webdisk::config::Config::load(config_path.value());
    if (!config_result) {
        std::cerr << config_result.error().message << '\n';
        return 1;
    }
    webdisk::config::Config config = config_result.take_value();

    webdisk::config::Config::Log log_config = config.log;
    log_config.file = log_config.user_service_file;
    auto log_result = webdisk::log::Log::init(log_config);
    if (!log_result) {
        std::cerr << log_result.error().message << '\n';
        return 1;
    }
    LOG_INFO("Configuration: {}", config.to_string());

    webdisk::db::MySqlClient database{config.database};
    webdisk::repository::UserRepository users{database};
    webdisk::security::PasswordHasher password_hasher{config.auth.password_iterations};
    webdisk::security::JwtService jwt_service{config.auth.jwt_secret, config.auth.jwt_issuer, config.auth.token_ttl};
    webdisk::service::AuthService auth_service{users, password_hasher, jwt_service};
    webdisk::service::UserService user_service{users};
    webdisk::rpcserver::UserRpcServiceImpl rpc_service{auth_service, user_service};

    srpc::RPCServerParams server_params = srpc::RPC_SERVER_PARAMS_DEFAULT;
    server_params.request_size_limit = 1024ULL * 1024ULL;
    srpc::SRPCServer server{&server_params};
    if (server.add_service(&rpc_service) != 0) {
        LOG_ERROR("Failed to register user RPC service");
        webdisk::log::Log::shutdown();
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    if (server.start(config.rpc.user_service_port) != 0) {
        LOG_ERROR("User RPC service failed to start on port {}", config.rpc.user_service_port);
        webdisk::log::Log::shutdown();
        return 1;
    }

    LOG_INFO("User RPC service listening on port {}", config.rpc.user_service_port);
    wait_group.wait();
    server.stop();
    LOG_INFO("User RPC service stopped");
    webdisk::log::Log::shutdown();
    return 0;
}

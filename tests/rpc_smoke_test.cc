#include <filesystem>
#include <iostream>
#include <string_view>

#include "config/Config.h"
#include "file_service.srpc.h"
#include "user_service.srpc.h"

namespace {

struct ConfigPaths {
    std::filesystem::path user_service;
    std::filesystem::path file_service;
};

webdisk::common::Result<ConfigPaths> parse_config_paths(int argc, char* argv[]) {
    if (argc != 5 || std::string_view(argv[1]) != "--user-config" || std::string_view(argv[2]).empty() ||
        std::string_view(argv[3]) != "--file-config" || std::string_view(argv[4]).empty()) {
        return webdisk::common::Result<ConfigPaths>::failure(
            500, "Usage: cloud_disk_rpc_smoke_test --user-config <user-service.ini> "
                 "--file-config <file-service.ini>");
    }
    return webdisk::common::Result<ConfigPaths>::success({argv[2], argv[4]});
}

} // namespace

int main(int argc, char* argv[]) {
    auto config_paths = parse_config_paths(argc, argv);
    if (!config_paths) {
        std::cerr << config_paths.error().message << '\n';
        return 1;
    }
    auto user_config = webdisk::config::UserServiceConfig::load(config_paths.value().user_service);
    if (!user_config) {
        std::cerr << user_config.error().message << '\n';
        return 1;
    }
    auto file_config = webdisk::config::FileServiceConfig::load(config_paths.value().file_service);
    if (!file_config) {
        std::cerr << file_config.error().message << '\n';
        return 1;
    }

    webdisk::rpc::UserRpcService::SRPCClient user_client{user_config.value().rpc.user_service_host.c_str(),
                                                         user_config.value().rpc.user_service_port};
    webdisk::rpc::RegisterRequest register_request;
    webdisk::rpc::RegisterResponse register_response;
    srpc::RPCSyncContext user_context;
    user_client.Register(&register_request, &register_response, &user_context);
    if (!user_context.success || register_response.status().code() != 400) {
        std::cerr << "User RPC smoke check failed\n";
        return 1;
    }

    webdisk::rpc::FileRpcService::SRPCClient file_client{file_config.value().rpc.file_service_host.c_str(),
                                                         file_config.value().rpc.file_service_port};
    webdisk::rpc::ListFilesRequest list_request;
    webdisk::rpc::ListFilesResponse list_response;
    srpc::RPCSyncContext file_context;
    file_client.ListFiles(&list_request, &list_response, &file_context);
    if (!file_context.success || list_response.status().code() != 400) {
        std::cerr << "File RPC smoke check failed\n";
        return 1;
    }

    std::cout << "User and file RPC services are reachable\n";
    return 0;
}

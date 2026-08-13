#include <filesystem>
#include <iostream>
#include <string_view>

#include "config/Config.h"
#include "file_service.srpc.h"
#include "user_service.srpc.h"

namespace {

webdisk::common::Result<std::filesystem::path> parse_config_path(int argc, char* argv[]) {
    if (argc != 3 || std::string_view(argv[1]) != "--config" || std::string_view(argv[2]).empty()) {
        return webdisk::common::Result<std::filesystem::path>::failure(
            500, "Usage: cloud_disk_rpc_smoke_test --config <server.ini>");
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
    auto config = webdisk::config::Config::load(config_path.value());
    if (!config) {
        std::cerr << config.error().message << '\n';
        return 1;
    }

    webdisk::rpc::UserRpcService::SRPCClient user_client{config.value().rpc.user_service_host.c_str(),
                                                         config.value().rpc.user_service_port};
    webdisk::rpc::RegisterRequest register_request;
    webdisk::rpc::RegisterResponse register_response;
    srpc::RPCSyncContext user_context;
    user_client.Register(&register_request, &register_response, &user_context);
    if (!user_context.success || register_response.status().code() != 400) {
        std::cerr << "User RPC smoke check failed\n";
        return 1;
    }

    webdisk::rpc::FileRpcService::SRPCClient file_client{config.value().rpc.file_service_host.c_str(),
                                                         config.value().rpc.file_service_port};
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

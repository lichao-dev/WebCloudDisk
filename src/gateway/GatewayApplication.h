#pragma once

#include <cstdint>

#include <wfrest/HttpServer.h>

#include "config/Config.h"
#include "file_service.srpc.h"
#include "http/AuthMiddleware.h"
#include "security/JwtService.h"
#include "user_service.srpc.h"

namespace webdisk {
namespace gateway {

class GatewayApplication {
public:
    explicit GatewayApplication(config::Config config);

    common::Result<void> init();
    int start();
    void stop();

private:
    void register_routes();
    void register_user(const wfrest::HttpReq* request, wfrest::HttpResp* response);
    void login(const wfrest::HttpReq* request, wfrest::HttpResp* response);
    void current_user(const wfrest::HttpReq* request, wfrest::HttpResp* response);
    void list_files(const wfrest::HttpReq* request, wfrest::HttpResp* response);
    void upload_file(const wfrest::HttpReq* request, wfrest::HttpResp* response);
    void download_file(const wfrest::HttpReq* request, wfrest::HttpResp* response);

    config::Config config_;
    security::JwtService jwt_service_;
    http::AuthMiddleware auth_middleware_;
    rpc::UserRpcService::SRPCClient user_client_;
    rpc::FileRpcService::SRPCClient file_client_;
    wfrest::HttpServer server_;
    bool routes_registered_{false};
};

} // namespace gateway
} // namespace webdisk

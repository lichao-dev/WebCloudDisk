#pragma once

#include <memory>

#include <wfrest/HttpServer.h>

#include "common/Result.h"
#include "config/Config.h"
#include "discovery/ConsulServiceDiscovery.h"
#include "discovery/RoundRobinEndpointSelector.h"
#include "http/AuthMiddleware.h"
#include "security/JwtService.h"

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
    std::unique_ptr<discovery::ConsulServiceDiscovery> service_discovery_;
    // 两个逻辑服务分别维护轮询序号，避免彼此的请求数量影响实例选择顺序。
    discovery::RoundRobinEndpointSelector user_endpoint_selector_;
    discovery::RoundRobinEndpointSelector file_endpoint_selector_;
    wfrest::HttpServer server_;
    bool routes_registered_{false};
};

} // namespace gateway
} // namespace webdisk

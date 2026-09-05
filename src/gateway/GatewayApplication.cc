#include "gateway/GatewayApplication.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>
#include <workflow/StringUtil.h>

#include "file_service.srpc.h"
#include "http/ApiResponse.h"
#include "log/Log.h"
#include "user_service.srpc.h"

namespace webdisk {
namespace gateway {

namespace {

common::Result<nlohmann::json> parse_json_object(const wfrest::HttpReq* request) {
    if (request->content_type() != wfrest::APPLICATION_JSON) {
        return common::Result<nlohmann::json>::failure(400, "Invalid request format");
    }

    nlohmann::json body = nlohmann::json::parse(request->body(), nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        return common::Result<nlohmann::json>::failure(400, "Invalid request format");
    }
    return common::Result<nlohmann::json>::success(std::move(body));
}

std::string string_field(const nlohmann::json& body, const char* field) {
    const auto it = body.find(field);
    return it != body.end() && it->is_string() ? it->get<std::string>() : "";
}

common::Result<uint64_t> parse_file_id(const std::string& value) {
    uint64_t file_id = 0;
    const char* begin = value.data();
    const char* end = begin + value.size();
    const auto parsed = std::from_chars(begin, end, file_id);
    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != end || file_id == 0) {
        return common::Result<uint64_t>::failure(400, "Invalid file ID");
    }
    return common::Result<uint64_t>::success(file_id);
}

bool rpc_transport_ok(srpc::RPCContext* context, wfrest::HttpResp* response, const char* service_name) {
    if (context != nullptr && context->success()) {
        return true;
    }

    const int status = context == nullptr ? -1 : context->get_status_code();
    const int error = context == nullptr ? -1 : context->get_error();
    LOG_WARN("RPC call failed: service={}, status={}, error={}", service_name, status, error);
    http::error(response, 502, "Upstream service unavailable");
    return false;
}

bool rpc_application_ok(const rpc::RpcStatus& status, wfrest::HttpResp* response) {
    if (status.code() == 0) {
        return true;
    }

    const int status_code = status.code() >= 400 && status.code() <= 599 ? status.code() : 500;
    http::error(response, status_code, status.message().empty() ? "RPC service error" : status.message());
    return false;
}

template <typename Client, typename CreateTask>
void add_discovered_rpc_task(discovery::ConsulServiceDiscovery& service_discovery,
                             discovery::RoundRobinEndpointSelector& endpoint_selector, const std::string& service_name,
                             int request_timeout_ms, wfrest::HttpResp* response, CreateTask create_task) {
    auto* discovery_task = service_discovery.create_discover_task(
        service_name,
        [&endpoint_selector, service_name, request_timeout_ms, response,
         create_task = std::move(create_task)](discovery::ConsulServiceDiscovery::DiscoverResult endpoints) mutable {
            if (!endpoints) {
                LOG_WARN("Consul discovery failed: service={}, status={}, error={}", service_name,
                         endpoints.error().status_code, endpoints.error().message);
                http::error(response, endpoints.error());
                return;
            }

            auto endpoint = endpoint_selector.select(endpoints.value());
            if (!endpoint) {
                LOG_WARN("RPC endpoint selection failed: service={}, status={}, error={}", service_name,
                         endpoint.error().status_code, endpoint.error().message);
                http::error(response, endpoint.error());
                return;
            }

            LOG_DEBUG("Selected RPC service instance: service={}, instance={}, address={}:{}", service_name,
                      endpoint.value().instance_id, endpoint.value().host, endpoint.value().port);

            Client client{endpoint.value().host.c_str(), endpoint.value().port};
            client.set_watch_timeout(request_timeout_ms);
            // sRPC 在创建任务时会把客户端地址和参数复制到任务中，因此局部客户端可以在本回调结束时销毁。
            auto* rpc_task = create_task(client);
            // 回调运行在当前 HTTP SeriesWork 中；追加 RPC 任务可保证发现完成后才发起远程调用。
            response->add_task(rpc_task);
        });
    // 发现任务先加入 HTTP 序列，回调再把选中实例对应的 RPC 任务追加到同一序列。
    response->add_task(discovery_task);
}

} // namespace

GatewayApplication::GatewayApplication(config::GatewayConfig config)
    : config_{std::move(config)},
      jwt_service_{config_.auth.jwt_secret, config_.auth.jwt_issuer, config_.auth.token_ttl},
      auth_middleware_{jwt_service_} {}

common::Result<void> GatewayApplication::init() {
    auto discovery = discovery::ConsulServiceDiscovery::create(config_.consul);
    if (!discovery) {
        return common::Result<void>::failure(discovery.error());
    }
    service_discovery_ = discovery.take_value();
    register_routes();
    return common::Result<void>::success();
}

void GatewayApplication::register_routes() {
    if (routes_registered_) {
        return;
    }

    server_.POST("/api/v1/auth/register", [this](const wfrest::HttpReq* request, wfrest::HttpResp* response) {
        register_user(request, response);
    });
    server_.POST("/api/v1/auth/login",
                 [this](const wfrest::HttpReq* request, wfrest::HttpResp* response) { login(request, response); });
    server_.GET("/api/v1/user/me", [this](const wfrest::HttpReq* request, wfrest::HttpResp* response) {
        current_user(request, response);
    });
    server_.GET("/api/v1/files",
                [this](const wfrest::HttpReq* request, wfrest::HttpResp* response) { list_files(request, response); });
    // multipart 解析、Protobuf bytes 复制和序列化会占用 CPU，放入计算队列执行。
    const wfrest::Handler upload_handler = [this](const wfrest::HttpReq* request, wfrest::HttpResp* response) {
        upload_file(request, response);
    };
    server_.POST("/api/v1/files", 0, upload_handler);
    server_.GET("/api/v1/file/{id}", [this](const wfrest::HttpReq* request, wfrest::HttpResp* response) {
        download_file(request, response);
    });

    const std::string index_file = (config_.server.web_root / "index.html").string();
    const std::string static_root = (config_.server.web_root / "static").string();
    server_.Static("/", index_file.c_str());
    server_.Static("/static", static_root.c_str());

    const uint64_t overhead = 1024ULL * 1024ULL;
    const uint64_t request_limit = config_.storage.max_file_size > std::numeric_limits<uint64_t>::max() - overhead
                                       ? config_.storage.max_file_size
                                       : config_.storage.max_file_size + overhead;
    server_.request_size_limit(
        static_cast<size_t>(std::min<uint64_t>(request_limit, std::numeric_limits<size_t>::max())));
    routes_registered_ = true;
}

void GatewayApplication::register_user(const wfrest::HttpReq* request, wfrest::HttpResp* response) {
    auto body = parse_json_object(request);
    if (!body) {
        http::error(response, body.error());
        return;
    }

    // 1. 创建 Protobuf 注册请求对象。
    rpc::RegisterRequest rpc_request;
    // 2. 将 HTTP JSON 字段写入 RPC 请求。
    rpc_request.set_username(string_field(body.value(), "username"));
    rpc_request.set_password(string_field(body.value(), "password"));
    rpc_request.set_confirm(string_field(body.value(), "confirm"));

    // 3. 先发现健康用户服务实例，再为选中的端点创建本次注册 RPC 任务。
    add_discovered_rpc_task<rpc::UserRpcService::SRPCClient>(
        *service_discovery_, user_endpoint_selector_, config_.consul.user_service_name, config_.rpc.request_timeout_ms,
        response, [rpc_request = std::move(rpc_request), response](rpc::UserRpcService::SRPCClient& client) mutable {
            auto* task =
                client.create_Register_task([response](rpc::RegisterResponse* rpc_response, srpc::RPCContext* context) {
                    if (!rpc_transport_ok(context, response, "user") ||
                        !rpc_application_ok(rpc_response->status(), response)) {
                        return;
                    }
                    nlohmann::json data{
                        {"userId", rpc_response->user().user_id()},
                        {"username", rpc_response->user().username()},
                    };
                    http::success(response, 201, "Registration successful", data);
                });
            // 发现回调结束前完成序列化，RPC 任务不再依赖局部 Protobuf 请求对象。
            task->serialize_input(&rpc_request);
            return task;
        });
}

void GatewayApplication::login(const wfrest::HttpReq* request, wfrest::HttpResp* response) {
    auto body = parse_json_object(request);
    if (!body) {
        http::error(response, body.error());
        return;
    }

    // 1. 创建 Protobuf 登录请求对象。
    rpc::LoginRequest rpc_request;
    // 2. 将 HTTP JSON 字段写入 RPC 请求。
    rpc_request.set_username(string_field(body.value(), "username"));
    rpc_request.set_password(string_field(body.value(), "password"));

    // 3. 先发现健康用户服务实例，再为选中的端点创建本次登录 RPC 任务。
    add_discovered_rpc_task<rpc::UserRpcService::SRPCClient>(
        *service_discovery_, user_endpoint_selector_, config_.consul.user_service_name, config_.rpc.request_timeout_ms,
        response, [rpc_request = std::move(rpc_request), response](rpc::UserRpcService::SRPCClient& client) mutable {
            auto* task =
                client.create_Login_task([response](rpc::LoginResponse* rpc_response, srpc::RPCContext* context) {
                    if (!rpc_transport_ok(context, response, "user") ||
                        !rpc_application_ok(rpc_response->status(), response)) {
                        return;
                    }
                    nlohmann::json data{
                        {"accessToken", rpc_response->access_token()},
                        {"tokenType", "Bearer"},
                        {"user",
                         {
                             {"userId", rpc_response->user().user_id()},
                             {"username", rpc_response->user().username()},
                         }},
                    };
                    http::success(response, 200, "Login successful", data);
                });
            task->serialize_input(&rpc_request);
            return task;
        });
}

void GatewayApplication::current_user(const wfrest::HttpReq* request, wfrest::HttpResp* response) {
    auto context = auth_middleware_.authenticate(request);
    if (!context) {
        http::error(response, context.error());
        return;
    }

    // 1. 创建 Protobuf 当前用户请求对象。
    rpc::GetCurrentUserRequest rpc_request;
    // 2. 将认证得到的用户 ID 写入 RPC 请求。
    rpc_request.set_user_id(context.value().user_id);
    // 3. 先发现健康用户服务实例，再创建当前用户查询任务。
    add_discovered_rpc_task<rpc::UserRpcService::SRPCClient>(
        *service_discovery_, user_endpoint_selector_, config_.consul.user_service_name, config_.rpc.request_timeout_ms,
        response, [rpc_request = std::move(rpc_request), response](rpc::UserRpcService::SRPCClient& client) mutable {
            auto* task = client.create_GetCurrentUser_task(
                [response](rpc::GetCurrentUserResponse* rpc_response, srpc::RPCContext* rpc_context) {
                    if (!rpc_transport_ok(rpc_context, response, "user") ||
                        !rpc_application_ok(rpc_response->status(), response)) {
                        return;
                    }
                    nlohmann::json data{
                        {"userId", rpc_response->user().user_id()},
                        {"username", rpc_response->user().username()},
                        {"createdAt", rpc_response->user().created_at()},
                    };
                    http::success(response, 200, "User profile retrieved successfully", data);
                });
            task->serialize_input(&rpc_request);
            return task;
        });
}

void GatewayApplication::list_files(const wfrest::HttpReq* request, wfrest::HttpResp* response) {
    auto context = auth_middleware_.authenticate(request);
    if (!context) {
        http::error(response, context.error());
        return;
    }

    // 1. 创建 Protobuf 文件列表请求对象。
    rpc::ListFilesRequest rpc_request;
    // 2. 将认证得到的用户 ID 写入 RPC 请求。
    rpc_request.set_user_id(context.value().user_id);
    // 3. 先发现健康文件服务实例，再创建文件列表 RPC 任务。
    add_discovered_rpc_task<rpc::FileRpcService::SRPCClient>(
        *service_discovery_, file_endpoint_selector_, config_.consul.file_service_name, config_.rpc.request_timeout_ms,
        response, [rpc_request = std::move(rpc_request), response](rpc::FileRpcService::SRPCClient& client) mutable {
            auto* task = client.create_ListFiles_task(
                [response](rpc::ListFilesResponse* rpc_response, srpc::RPCContext* rpc_context) {
                    if (!rpc_transport_ok(rpc_context, response, "file") ||
                        !rpc_application_ok(rpc_response->status(), response)) {
                        return;
                    }

                    auto files = nlohmann::json::array();
                    for (const auto& file : rpc_response->files()) {
                        files.push_back({
                            {"fileId", file.file_id()},
                            {"filename", file.filename()},
                            {"size", file.size()},
                            {"createdAt", file.created_at()},
                            {"updatedAt", file.updated_at()},
                        });
                    }
                    http::success(response, 200, "File list retrieved successfully",
                                  nlohmann::json{{"files", std::move(files)}});
                });
            task->serialize_input(&rpc_request);
            return task;
        });
}

void GatewayApplication::upload_file(const wfrest::HttpReq* request, wfrest::HttpResp* response) {
    auto context = auth_middleware_.authenticate(request);
    if (!context) {
        http::error(response, context.error());
        return;
    }
    if (request->content_type() != wfrest::MULTIPART_FORM_DATA) {
        http::error(response, 400, "Invalid request format");
        return;
    }

    const wfrest::Form& form = request->form();
    const auto it = form.find("file");
    if (it == form.end() || it->second.first.empty()) {
        http::error(response, 400, "No uploaded file found");
        return;
    }

    // 1. 创建 Protobuf 文件上传请求对象。
    rpc::UploadFileRequest rpc_request;
    // 2. 将认证信息和 HTTP 表单中的文件写入 RPC 请求。
    rpc_request.set_user_id(context.value().user_id);
    rpc_request.set_filename(it->second.first);
    rpc_request.set_content(it->second.second);
    // 3. 先发现健康文件服务实例，再创建上传 RPC 任务。
    add_discovered_rpc_task<rpc::FileRpcService::SRPCClient>(
        *service_discovery_, file_endpoint_selector_, config_.consul.file_service_name, config_.rpc.request_timeout_ms,
        response, [rpc_request = std::move(rpc_request), response](rpc::FileRpcService::SRPCClient& client) mutable {
            auto* task = client.create_UploadFile_task(
                [response](rpc::UploadFileResponse* rpc_response, srpc::RPCContext* rpc_context) {
                    if (!rpc_transport_ok(rpc_context, response, "file") ||
                        !rpc_application_ok(rpc_response->status(), response)) {
                        return;
                    }
                    nlohmann::json data{
                        {"fileId", rpc_response->file_id()},
                        {"filename", rpc_response->filename()},
                    };
                    http::success(response, 201, "Upload successful", data);
                });
            task->serialize_input(&rpc_request);
            return task;
        });
}

void GatewayApplication::download_file(const wfrest::HttpReq* request, wfrest::HttpResp* response) {
    auto context = auth_middleware_.authenticate(request);
    if (!context) {
        http::error(response, context.error());
        return;
    }
    auto file_id = parse_file_id(request->param("id"));
    if (!file_id) {
        http::error(response, file_id.error());
        return;
    }

    // 1. 创建 Protobuf 文件下载请求对象。
    rpc::DownloadFileRequest rpc_request;
    // 2. 将认证得到的用户 ID 和 HTTP 路径中的文件 ID 写入 RPC 请求。
    rpc_request.set_user_id(context.value().user_id);
    rpc_request.set_file_id(file_id.value());
    // 3. 先发现健康文件服务实例，再创建下载 RPC 任务。
    add_discovered_rpc_task<rpc::FileRpcService::SRPCClient>(
        *service_discovery_, file_endpoint_selector_, config_.consul.file_service_name, config_.rpc.request_timeout_ms,
        response, [rpc_request = std::move(rpc_request), response](rpc::FileRpcService::SRPCClient& client) mutable {
            auto* task = client.create_DownloadFile_task(
                [response](rpc::DownloadFileResponse* rpc_response, srpc::RPCContext* rpc_context) {
                    if (!rpc_transport_ok(rpc_context, response, "file") ||
                        !rpc_application_ok(rpc_response->status(), response)) {
                        return;
                    }

                    const std::string encoded_filename = StringUtil::url_encode_component(rpc_response->filename());
                    response->add_header("Content-Type", "application/octet-stream");
                    response->add_header("Content-Disposition",
                                         "attachment; filename=\"download\"; filename*=UTF-8''" + encoded_filename);
                    response->String(rpc_response->content());
                });
            task->serialize_input(&rpc_request);
            return task;
        });
}

int GatewayApplication::start() {
    return server_.start(config_.server.port);
}

void GatewayApplication::stop() {
    server_.stop();
}

} // namespace gateway
} // namespace webdisk

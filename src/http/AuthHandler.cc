#include "http/AuthHandler.h"

#include <string>

#include <nlohmann/json.hpp>

#include "common/Result.h"
#include "http/ApiResponse.h"

namespace webdisk {
namespace http {

namespace {

common::Result<nlohmann::json> parse_json_object(const wfrest::HttpReq* request) {
    // 非法 JSON 和错误 Content-Type 都统一映射到文档约定的 400 响应。
    if (request->content_type() != wfrest::APPLICATION_JSON) {
        return common::Result<nlohmann::json>::failure(400, "Invalid request format");
    }
    // 禁止解析器抛出异常；语法错误会返回 discarded 状态，由普通分支处理。
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

} // namespace

AuthHandler::AuthHandler(const service::AuthService& service)
    : service_{service} {
}

void AuthHandler::register_user(const wfrest::HttpReq* request, wfrest::HttpResp* response) const {
    auto body = parse_json_object(request);
    if (!body) {
        error(response, body.error());
        return;
    }

    service_.register_user(string_field(body.value(), "username"), string_field(body.value(), "password"),
                           string_field(body.value(), "confirm"), response,
                           [response](common::Result<service::RegisteredUser> result) {
                               if (!result) {
                                   error(response, result.error());
                                   return;
                               }
                               nlohmann::json data{
                                   {"userId", result.value().user_id},
                                   {"username", result.value().username},
                               };
                               success(response, 201, "Registration successful", data);
                           });
}

void AuthHandler::login(const wfrest::HttpReq* request, wfrest::HttpResp* response) const {
    auto body = parse_json_object(request);
    if (!body) {
        error(response, body.error());
        return;
    }

    service_.login(string_field(body.value(), "username"), string_field(body.value(), "password"), response,
                   [response](common::Result<service::LoginResult> result) {
                       if (!result) {
                           error(response, result.error());
                           return;
                       }
                       nlohmann::json data{
                           {"accessToken", result.value().access_token},
                           {"tokenType", "Bearer"},
                           {"user",
                            {
                                {"userId", result.value().user.id},
                                {"username", result.value().user.username},
                            }},
                       };
                       success(response, 200, "Login successful", data);
                   });
}

} // namespace http
} // namespace webdisk

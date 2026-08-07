#include "http/AuthMiddleware.h"

#include <string>

namespace webdisk {
namespace http {

AuthMiddleware::AuthMiddleware(const security::JwtService& jwt_service)
    : jwt_service_{jwt_service} {}

common::Result<model::AuthContext> AuthMiddleware::authenticate(const wfrest::HttpReq* request) const {
    const std::string authorization = request->header("Authorization");
    constexpr const char* prefix = "Bearer ";
    // rfind(prefix, 0) 只允许在位置 0 匹配，用于检查请求头前缀；长度条件确保前缀后存在 Token。
    if (authorization.rfind(prefix, 0) != 0 || authorization.size() <= 7) {
        return common::Result<model::AuthContext>::failure(401, "Invalid access token");
    }

    const std::string token = authorization.substr(7);
    // 拒绝令牌内部的空白字符，避免下游 JWT 解析器接受被裁剪或含义不明确的凭证。
    if (token.find_first_of(" \t\r\n") != std::string::npos) {
        return common::Result<model::AuthContext>::failure(401, "Invalid access token");
    }
    return jwt_service_.verify(token);
}

} // namespace http
} // namespace webdisk

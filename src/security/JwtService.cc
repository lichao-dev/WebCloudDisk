#include "security/JwtService.h"

#include "log/Log.h"

#include <jwt-cpp/jwt.h>

#include <charconv>
#include <exception>

namespace webdisk {
namespace security {

JwtService::JwtService(std::string secret, std::string issuer, std::chrono::seconds token_ttl)
    : secret_{std::move(secret)}
    , issuer_{std::move(issuer)}
    , token_ttl_{token_ttl} {
}

common::Result<std::string> JwtService::generate(uint64_t user_id) const {
    try {
        const auto now = std::chrono::system_clock::now();
        std::string token = jwt::create()
                                .set_type("JWT")
                                .set_issuer(issuer_)
                                .set_subject("access-token")
                                .set_issued_at(now)
                                .set_expires_at(now + token_ttl_)
                                // jwt-cpp 默认 JSON 类型只提供有符号 64 位整数，
                                // 因此把数据库的无符号 ID 以字符串形式保存。
                                .set_payload_claim("uid", jwt::claim(std::to_string(user_id)))
                                .sign(jwt::algorithm::hs256(secret_));

        return common::Result<std::string>::success(std::move(token));
    } catch (const std::exception& exception) {
        LOG_ERROR("Failed to generate access token: user_id={}, error={}", user_id, exception.what());
        return common::Result<std::string>::failure(500, "Failed to generate access token");
    }
}

common::Result<model::AuthContext> JwtService::verify(const std::string& token) const {
    try {
        const auto decoded = jwt::decode(token);
        jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256(secret_))
            .with_issuer(issuer_)
            .with_subject("access-token")
            .verify(decoded);

        if (!decoded.has_payload_claim("uid")) {
            return common::Result<model::AuthContext>::failure(401, "Invalid access token");
        }

        const std::string user_id_text = decoded.get_payload_claim("uid").as_string();
        uint64_t user_id = 0;
        const char* begin = user_id_text.data();
        const char* end = begin + user_id_text.size();
        const auto parsed = std::from_chars(begin, end, user_id);

        if (parsed.ec != std::errc{} || parsed.ptr != end || user_id == 0) {
            return common::Result<model::AuthContext>::failure(401, "Invalid access token");
        }

        return common::Result<model::AuthContext>::success(model::AuthContext{user_id});
    } catch (const std::exception&) {
        return common::Result<model::AuthContext>::failure(401, "Invalid access token");
    }
}

} // namespace security
} // namespace webdisk

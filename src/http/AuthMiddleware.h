#pragma once

#include "common/Result.h"
#include "model/AuthContext.h"
#include "security/JwtService.h"

#include <wfrest/HttpServer.h>

namespace webdisk {
namespace http {

class AuthMiddleware {
public:
    explicit AuthMiddleware(const security::JwtService& jwt_service);

    common::Result<model::AuthContext> authenticate(const wfrest::HttpReq* request) const;

private:
    const security::JwtService& jwt_service_;
};

} // namespace http
} // namespace webdisk

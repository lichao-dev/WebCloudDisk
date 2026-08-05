#pragma once

#include <wfrest/HttpServer.h>

#include "http/AuthMiddleware.h"
#include "service/UserService.h"

namespace webdisk {
namespace http {

class UserHandler {
public:
    UserHandler(const AuthMiddleware& auth, const service::UserService& service);

    void current_user(const wfrest::HttpReq* request, wfrest::HttpResp* response) const;

private:
    const AuthMiddleware& auth_;
    const service::UserService& service_;
};

} // namespace http
} // namespace webdisk

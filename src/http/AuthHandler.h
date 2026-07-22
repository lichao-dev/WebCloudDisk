#pragma once

#include "service/AuthService.h"

#include <wfrest/HttpServer.h>

namespace webdisk {
namespace http {

class AuthHandler {
public:
    explicit AuthHandler(const service::AuthService& service);

    void register_user(const wfrest::HttpReq* request, wfrest::HttpResp* response) const;
    void login(const wfrest::HttpReq* request, wfrest::HttpResp* response) const;

private:
    const service::AuthService& service_;
};

} // namespace http
} // namespace webdisk

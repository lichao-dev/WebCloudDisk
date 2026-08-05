#pragma once

#include "service/AuthService.h"

#include <wfrest/HttpServer.h>

namespace webdisk {
namespace http {

class AuthHandler {
public:
    explicit AuthHandler(const service::AuthService& service);

    // 处理用户注册请求并将结果写入响应对象
    void register_user(const wfrest::HttpReq* request, wfrest::HttpResp* response) const;
    // 处理用户登录请求并将结果写入响应对象
    void login(const wfrest::HttpReq* request, wfrest::HttpResp* response) const;

private:
    const service::AuthService& service_;
};

} // namespace http
} // namespace webdisk

#include "http/ApiResponse.h"

namespace webdisk {
namespace http {

void success(wfrest::HttpResp* response, int status_code, const std::string& message, const nlohmann::json& data) {
    nlohmann::json body{
        {"status", "success"},
        {"message", message},
        {"data", data},
    };

    // 设置响应状态码
    response->set_status(status_code);
    // 设置响应头 Content-Type
    response->add_header("Content-Type", "application/json; charset=utf-8");
    // 设置响应体
    response->String(body.dump());
}

void error(wfrest::HttpResp* response, int status_code, const std::string& message) {
    nlohmann::json body{
        {"status", "error"},
        {"message", message},
    };

    // 设置响应状态码
    response->set_status(status_code);
    // 设置响应头 Content-Type
    response->add_header("Content-Type", "application/json; charset=utf-8");
    // 设置响应体
    response->String(body.dump());
}

void error(wfrest::HttpResp* response, const common::AppError& app_error) {
    error(response, app_error.status_code, app_error.message);
}

} // namespace http
} // namespace webdisk

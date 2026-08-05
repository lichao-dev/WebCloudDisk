#pragma once

#include <string>

#include <nlohmann/json.hpp>
#include <wfrest/HttpServer.h>

#include "common/Result.h"

namespace webdisk {
namespace http {

void success(wfrest::HttpResp* response, int status_code, const std::string& message, const nlohmann::json& data);

void error(wfrest::HttpResp* response, int status_code, const std::string& message);
void error(wfrest::HttpResp* response, const common::AppError& error);

} // namespace http
} // namespace webdisk

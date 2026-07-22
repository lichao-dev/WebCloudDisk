#pragma once

#include "http/AuthMiddleware.h"
#include "service/FileService.h"

#include <wfrest/HttpServer.h>

namespace webdisk {
namespace http {

class FileHandler {
public:
    FileHandler(const AuthMiddleware& auth, const service::FileService& service);

    void list(const wfrest::HttpReq* request, wfrest::HttpResp* response) const;
    void upload(const wfrest::HttpReq* request, wfrest::HttpResp* response) const;
    void download(const wfrest::HttpReq* request, wfrest::HttpResp* response) const;

private:
    const AuthMiddleware& auth_;
    const service::FileService& service_;
};

} // namespace http
} // namespace webdisk

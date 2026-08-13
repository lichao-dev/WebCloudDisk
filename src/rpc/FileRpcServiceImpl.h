#pragma once

#include "file_service.srpc.h"
#include "service/FileService.h"

namespace webdisk {
namespace rpcserver {

class FileRpcServiceImpl final : public rpc::FileRpcService::Service {
public:
    explicit FileRpcServiceImpl(const service::FileService& file_service);

    void ListFiles(rpc::ListFilesRequest* request, rpc::ListFilesResponse* response,
                   srpc::RPCContext* context) override;
    void UploadFile(rpc::UploadFileRequest* request, rpc::UploadFileResponse* response,
                    srpc::RPCContext* context) override;
    void DownloadFile(rpc::DownloadFileRequest* request, rpc::DownloadFileResponse* response,
                      srpc::RPCContext* context) override;

private:
    const service::FileService& file_service_;
};

} // namespace rpcserver
} // namespace webdisk

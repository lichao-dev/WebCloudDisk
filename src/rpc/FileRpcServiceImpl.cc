#include "rpc/FileRpcServiceImpl.h"

#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <utility>

#include <workflow/WFTaskFactory.h>

#include "common/TaskScheduler.h"

namespace webdisk {
namespace rpcserver {

namespace {

common::TaskScheduler make_task_scheduler(SeriesWork* series) {
    return common::TaskScheduler{
        [series](SubTask* task) { series->push_back(task); },
        [series](std::function<void()> function) {
            series->push_back(WFTaskFactory::create_go_task("webdisk-rpc", std::move(function)));
        },
    };
}

template <typename Response>
void set_error(Response* response, const common::AppError& error) {
    response->mutable_status()->set_code(error.status_code);
    response->mutable_status()->set_message(error.message);
}

} // namespace

FileRpcServiceImpl::FileRpcServiceImpl(const service::FileService& file_service)
    : file_service_{file_service} {}

void FileRpcServiceImpl::ListFiles(rpc::ListFilesRequest* request, rpc::ListFilesResponse* response,
                                   srpc::RPCContext* context) {
    if (request->user_id() == 0) {
        set_error(response, common::AppError{400, "Invalid user ID"});
        return;
    }

    file_service_.list(
        request->user_id(), make_task_scheduler(context->get_series()),
        [response](common::Result<std::vector<model::FileInfo>> result) {
            if (!result) {
                set_error(response, result.error());
                return;
            }
            response->mutable_status()->set_code(0);
            for (const auto& file : result.value()) {
                rpc::FileInfo* output = response->add_files();
                output->set_file_id(file.id);
                output->set_filename(file.filename);
                output->set_size(file.size);
                output->set_created_at(file.created_at);
                output->set_updated_at(file.updated_at);
            }
        });
}

void FileRpcServiceImpl::UploadFile(rpc::UploadFileRequest* request, rpc::UploadFileResponse* response,
                                    srpc::RPCContext* context) {
    if (request->user_id() == 0) {
        set_error(response, common::AppError{400, "Invalid user ID"});
        return;
    }

    const uint64_t user_id = request->user_id();
    const std::string filename = request->filename();
    const std::string content = request->content();
    const common::TaskScheduler scheduler = make_task_scheduler(context->get_series());

    // 文件哈希和同步写盘都在计算队列执行，避免阻塞 RPC 网络线程。
    scheduler.add_compute_task([this, user_id, filename, content, scheduler, response] {
        file_service_.upload(
            user_id, filename, content, scheduler, [response](common::Result<service::UploadedFile> result) {
                if (!result) {
                    set_error(response, result.error());
                    return;
                }
                response->mutable_status()->set_code(0);
                response->set_file_id(result.value().file_id);
                response->set_filename(result.value().filename);
            });
    });
}

void FileRpcServiceImpl::DownloadFile(rpc::DownloadFileRequest* request, rpc::DownloadFileResponse* response,
                                      srpc::RPCContext* context) {
    if (request->user_id() == 0 || request->file_id() == 0) {
        set_error(response, common::AppError{400, "Invalid user or file ID"});
        return;
    }

    const common::TaskScheduler scheduler = make_task_scheduler(context->get_series());
    file_service_.find_download(
        request->user_id(), request->file_id(), scheduler,
        [scheduler, response](common::Result<service::DownloadFile> result) {
            if (!result) {
                set_error(response, result.error());
                return;
            }

            const std::string filename = result.value().filename;
            const std::filesystem::path path = result.value().path;
            scheduler.add_compute_task([filename, path, response] {
                std::ifstream input(path, std::ios::binary);
                if (!input) {
                    set_error(response, common::AppError{404, "File not found"});
                    return;
                }

                std::string content{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
                if (input.bad()) {
                    set_error(response, common::AppError{500, "Failed to read stored file"});
                    return;
                }

                response->mutable_status()->set_code(0);
                response->set_filename(filename);
                response->set_content(std::move(content));
            });
        });
}

} // namespace rpcserver
} // namespace webdisk

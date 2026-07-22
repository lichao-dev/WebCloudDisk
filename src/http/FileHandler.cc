#include "http/FileHandler.h"

#include "http/ApiResponse.h"

#include <nlohmann/json.hpp>
#include <workflow/StringUtil.h>

#include <charconv>
#include <cstdint>
#include <string>

namespace webdisk {
namespace http {

using common::Result;
using model::FileInfo;
using service::DownloadFile;
using service::FileService;
using service::UploadedFile;

namespace {

Result<std::uint64_t> parse_file_id(const std::string& value) {
    std::uint64_t file_id = 0;
    const char* begin = value.data();
    const char* end = begin + value.size();
    const auto parsed = std::from_chars(begin, end, file_id);
    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != end || file_id == 0) {
        return Result<std::uint64_t>::failure(400, "Invalid file ID");
    }
    return Result<std::uint64_t>::success(file_id);
}

} // namespace

FileHandler::FileHandler(const AuthMiddleware& auth, const FileService& service)
    : auth_(auth)
    , service_(service) {
}

void FileHandler::list(const wfrest::HttpReq* request, wfrest::HttpResp* response) const {
    auto context = auth_.authenticate(request);
    if (!context) {
        error(response, context.error());
        return;
    }

    service_.list(context.value().user_id, response, [response](Result<std::vector<FileInfo>> result) {
        if (!result) {
            error(response, result.error());
            return;
        }
        nlohmann::json files = nlohmann::json::array();
        for (const auto& file : result.value()) {
            files.push_back({
                {"fileId", file.id},
                {"filename", file.filename},
                {"size", file.size},
                {"createdAt", file.created_at},
                {"updatedAt", file.updated_at},
            });
        }
        success(response, 200, "File list retrieved successfully",
                        nlohmann::json{{"files", std::move(files)}});
    });
}

void FileHandler::upload(const wfrest::HttpReq* request, wfrest::HttpResp* response) const {
    auto context = auth_.authenticate(request);
    if (!context) {
        error(response, context.error());
        return;
    }
    if (request->content_type() != wfrest::MULTIPART_FORM_DATA) {
        error(response, 400, "Invalid request format");
        return;
    }

    const wfrest::Form& form = request->form();
    const auto iterator = form.find("file");
    if (iterator == form.end() || iterator->second.first.empty()) {
        error(response, 400, "No uploaded file found");
        return;
    }

    service_.upload(context.value().user_id, iterator->second.first, iterator->second.second, response,
                    [response](Result<UploadedFile> result) {
                        if (!result) {
                            error(response, result.error());
                            return;
                        }
                        nlohmann::json data = {
                            {"fileId", result.value().file_id},
                            {"filename", result.value().filename},
                        };
                        success(response, 201, "Upload successful", data);
                    });
}

void FileHandler::download(const wfrest::HttpReq* request, wfrest::HttpResp* response) const {
    auto context = auth_.authenticate(request);
    if (!context) {
        error(response, context.error());
        return;
    }

    auto file_id = parse_file_id(request->param("id"));
    if (!file_id) {
        error(response, file_id.error());
        return;
    }

    service_.find_download(context.value().user_id, file_id.value(), response, [response](Result<DownloadFile> result) {
        if (!result) {
            error(response, result.error());
            return;
        }

        // filename* 用编码后的形式传递原始 UTF-8 文件名，避免 CRLF 或引号
        // 改变响应头结构。
        const std::string encoded_filename = StringUtil::url_encode_component(result.value().filename);
        response->add_header("Content-Type", "application/octet-stream");
        response->add_header("Content-Disposition",
                             "attachment; filename=\"download\"; filename*=UTF-8''" + encoded_filename);
        response->File(result.value().path.string());
    });
}

} // namespace http
} // namespace webdisk

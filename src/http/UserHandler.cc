#include "http/UserHandler.h"

#include <nlohmann/json.hpp>

#include "http/ApiResponse.h"

namespace webdisk {
namespace http {

UserHandler::UserHandler(const AuthMiddleware& auth, const service::UserService& service)
    : auth_{auth}
    , service_{service} {
}

void UserHandler::current_user(const wfrest::HttpReq* request, wfrest::HttpResp* response) const {
    auto context = auth_.authenticate(request);
    if (!context) {
        error(response, context.error());
        return;
    }

    service_.get_current_user(context.value().user_id, response, [response](common::Result<model::User> result) {
        if (!result) {
            error(response, result.error());
            return;
        }
        nlohmann::json data{
            {"userId", result.value().id},
            {"username", result.value().username},
            {"createdAt", result.value().created_at},
        };
        success(response, 200, "User profile retrieved successfully", data);
    });
}

} // namespace http
} // namespace webdisk

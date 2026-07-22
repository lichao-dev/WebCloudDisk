#include "http/UserHandler.h"

#include "http/ApiResponse.h"

#include <nlohmann/json.hpp>

namespace webdisk {
namespace http {

using common::Result;
using model::User;
using service::UserService;

UserHandler::UserHandler(const AuthMiddleware& auth, const UserService& service)
    : auth_(auth)
    , service_(service) {
}

void UserHandler::current_user(const wfrest::HttpReq* request, wfrest::HttpResp* response) const {
    auto context = auth_.authenticate(request);
    if (!context) {
        error(response, context.error());
        return;
    }

    service_.get_current_user(context.value().user_id, response, [response](Result<User> result) {
        if (!result) {
            error(response, result.error());
            return;
        }
        nlohmann::json data = {
            {"userId", result.value().id},
            {"username", result.value().username},
            {"createdAt", result.value().created_at},
        };
        success(response, 200, "User profile retrieved successfully", data);
    });
}

} // namespace http
} // namespace webdisk

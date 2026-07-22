#include "service/UserService.h"

#include <utility>

namespace webdisk {
namespace service {

using common::Result;
using model::User;
using repository::UserRepository;

UserService::UserService(const UserRepository& users)
    : users_(users) {
}

void UserService::get_current_user(std::uint64_t user_id, wfrest::HttpResp* response, Callback callback) const {
    WFMySQLTask* task =
        users_.find_by_id(user_id, [callback = std::move(callback)](Result<std::optional<User>> result) mutable {
            if (!result) {
                callback(Result<User>::failure(result.error().status_code, result.error().message));
                return;
            }
            if (!result.value().has_value()) {
                callback(Result<User>::failure(404, "User not found"));
                return;
            }
            callback(Result<User>::success(*result.value()));
        });
    response->add_task(task);
}

} // namespace service
} // namespace webdisk

#include "service/UserService.h"

#include <utility>

namespace webdisk {
namespace service {

UserService::UserService(const repository::UserRepository& users)
    : users_{users} {
}

void UserService::get_current_user(uint64_t user_id, wfrest::HttpResp* response, Callback callback) const {
    WFMySQLTask* task =
        users_.find_by_id(user_id, [callback = std::move(callback)](common::Result<std::optional<model::User>> result) {
            if (!result) {
                callback(common::Result<model::User>::failure(result.error().status_code, result.error().message));
                return;
            }
            if (!result.value().has_value()) {
                callback(common::Result<model::User>::failure(404, "User not found"));
                return;
            }
            callback(common::Result<model::User>::success(*result.value()));
        });
    response->add_task(task);
}

} // namespace service
} // namespace webdisk

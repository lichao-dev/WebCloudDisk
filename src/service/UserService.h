#pragma once

#include "common/Result.h"
#include "model/User.h"
#include "repository/UserRepository.h"

#include <wfrest/HttpServer.h>

#include <cstdint>
#include <functional>

namespace webdisk {
namespace service {

class UserService {
public:
    using Callback = std::function<void(common::Result<model::User>)>;

    explicit UserService(const repository::UserRepository& users);
    void get_current_user(std::uint64_t user_id, wfrest::HttpResp* response, Callback callback) const;

private:
    const repository::UserRepository& users_;
};

} // namespace service
} // namespace webdisk

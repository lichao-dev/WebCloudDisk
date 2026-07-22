#pragma once

#include "common/Result.h"
#include "database/MySqlClient.h"
#include "model/User.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace webdisk {
namespace repository {

class UserRepository {
public:
    using FindCallback = std::function<void(common::Result<std::optional<model::User>>)>;
    using CreateCallback = std::function<void(common::Result<uint64_t>)>;
    using UpdateCallback = std::function<void(common::Result<void>)>;

    explicit UserRepository(const db::MySqlClient& database);

    WFMySQLTask* find_by_username(const std::string& username, FindCallback callback) const;
    WFMySQLTask* find_by_id(uint64_t user_id, FindCallback callback) const;
    WFMySQLTask* create(const std::string& username, const std::string& password_hash, CreateCallback callback) const;
    WFMySQLTask* update_password_hash(uint64_t user_id, const std::string& password_hash,
                                      UpdateCallback callback) const;

private:
    const db::MySqlClient& database_;
};

} // namespace repository
} // namespace webdisk

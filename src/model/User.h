#pragma once

#include <cstdint>
#include <string>

namespace webdisk {
namespace model {

struct User {
    std::uint64_t id{};
    std::string username;
    std::string password_hash;
    std::string created_at;
    std::string updated_at;
};

} // namespace model
} // namespace webdisk

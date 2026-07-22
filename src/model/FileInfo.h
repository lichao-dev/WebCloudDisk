#pragma once

#include <cstdint>
#include <string>

namespace webdisk {
namespace model {

struct FileInfo {
    uint64_t id{};
    uint64_t user_id{};
    std::string filename;
    std::string hashcode;
    uint64_t size{};
    std::string created_at;
    std::string updated_at;
};

} // namespace model
} // namespace webdisk

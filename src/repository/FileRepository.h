#pragma once

#include "common/Result.h"
#include "database/MySqlClient.h"
#include "model/FileInfo.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace webdisk {
namespace repository {

class FileRepository {
public:
    using ListCallback = std::function<void(common::Result<std::vector<model::FileInfo>>)>;
    using FindCallback = std::function<void(common::Result<std::optional<model::FileInfo>>)>;
    using CreateCallback = std::function<void(common::Result<std::uint64_t>)>;

    explicit FileRepository(const db::MySqlClient& database);

    WFMySQLTask* list_by_user(std::uint64_t user_id, ListCallback callback) const;
    WFMySQLTask* find_owned(std::uint64_t user_id, std::uint64_t file_id, FindCallback callback) const;
    WFMySQLTask* create(std::uint64_t user_id, const std::string& filename, const std::string& hashcode,
                        std::uint64_t size, CreateCallback callback) const;

private:
    const db::MySqlClient& database_;
};

} // namespace repository
} // namespace webdisk

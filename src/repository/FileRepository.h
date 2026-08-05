#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "common/Result.h"
#include "database/MySqlClient.h"
#include "model/FileInfo.h"

namespace webdisk {
namespace repository {

class FileRepository {
public:
    using ListCallback = std::function<void(common::Result<std::vector<model::FileInfo>>)>;
    using FindCallback = std::function<void(common::Result<std::optional<model::FileInfo>>)>;
    using CreateCallback = std::function<void(common::Result<uint64_t>)>;

    explicit FileRepository(const db::MySqlClient& database);

    WFMySQLTask* list_by_user(uint64_t user_id, ListCallback callback) const;
    WFMySQLTask* find_owned(uint64_t user_id, uint64_t file_id, FindCallback callback) const;
    WFMySQLTask* create(uint64_t user_id, const std::string& filename, const std::string& hashcode,
                        uint64_t size, CreateCallback callback) const;

private:
    const db::MySqlClient& database_;
};

} // namespace repository
} // namespace webdisk

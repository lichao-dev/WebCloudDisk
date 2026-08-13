#pragma once

#include <functional>
#include <string>

#include <workflow/WFTaskFactory.h>

#include "config/Config.h"

namespace webdisk {
namespace db {

class MySqlClient {
public:
    using Callback = std::function<void(WFMySQLTask*)>;

    explicit MySqlClient(const config::Database& config);

    WFMySQLTask* create_task(const std::string& sql, Callback callback) const;
    // 转义即将拼接进 SQL 字符串字面量的特殊字符，避免输入破坏 SQL 结构；返回值不包含外层单引号。
    static std::string escape(const std::string& value);

private:
    std::string url_;
    int retry_max_;
};

} // namespace db
} // namespace webdisk

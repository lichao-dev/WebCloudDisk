#include "database/MySqlClient.h"

#include <workflow/MySQLUtil.h>
#include <workflow/StringUtil.h>

namespace webdisk {
namespace db {

MySqlClient::MySqlClient(const config::Config::Database& config)
    : retry_max_{config.retry_max} {
    // Workflow 通过 URL 接收 MySQL 连接信息。必须编码用户名和密码，
    // 避免其中的 '@'、':' 等字符破坏 URL 结构。
    url_ = "mysql://" + StringUtil::url_encode_component(config.username) + ":" +
           StringUtil::url_encode_component(config.password) + "@" + config.host + ":" + std::to_string(config.port) +
           "/" + StringUtil::url_encode_component(config.database);
}

WFMySQLTask* MySqlClient::create_task(const std::string& sql, Callback callback) const {
    // Repository 返回任务，由调用者加入 wfrest 当前请求的任务序列，
    // 从而保持数据库访问的异步执行方式。
    WFMySQLTask* task = WFTaskFactory::create_mysql_task(url_, retry_max_, std::move(callback));
    task->get_req()->set_query(sql);
    return task;
}

std::string MySqlClient::escape(const std::string& value) {
    return protocol::MySQLUtil::escape_string(value);
}

} // namespace db
} // namespace webdisk

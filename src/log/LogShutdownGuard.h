#pragma once

#include "log/Log.h"

namespace webdisk {
namespace log {

// 日志系统关闭守卫。
// 在对象离开作用域时自动调用 Log::shutdown()，
// 确保 main 函数无论正常返回还是提前退出，都能正确释放日志资源。
class LogShutdownGuard final {
public:
    LogShutdownGuard() = default;

    ~LogShutdownGuard() { Log::shutdown(); }

    // Guard 独占日志关闭职责，禁止复制，避免多个对象重复执行 shutdown。
    LogShutdownGuard(const LogShutdownGuard&) = delete;
    LogShutdownGuard& operator=(const LogShutdownGuard&) = delete;
};

} // namespace log
} // namespace webdisk

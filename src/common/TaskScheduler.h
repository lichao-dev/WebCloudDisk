#pragma once

#include <functional>
#include <utility>

class SubTask;

namespace webdisk {
namespace common {

// 业务层只描述“追加异步任务”和“进入计算队列”，具体任务序列由 RPC 调用方提供。
class TaskScheduler {
public:
    // 由调用边界注入：把已经创建的 Workflow 子任务追加到当前请求序列。
    using AddTask = std::function<void(SubTask*)>;
    // 由调用边界注入：把同步函数包装为计算任务并追加到当前请求序列。
    using AddComputeTask = std::function<void(std::function<void()>)>;

    // 只保存任务接入方式；回调捕获的请求序列必须在本对象使用期间保持有效。
    TaskScheduler(AddTask add_task, AddComputeTask add_compute_task)
        : add_task_{std::move(add_task)},
          add_compute_task_{std::move(add_compute_task)} {}

    // 不接管或直接启动 task，只把它交给调用方提供的追加函数。
    void add_task(SubTask* task) const { add_task_(task); }
    // 将函数所有权移交给调用方，由其决定使用哪个 Workflow 计算队列。
    void add_compute_task(std::function<void()> function) const { add_compute_task_(std::move(function)); }

private:
    AddTask add_task_;
    AddComputeTask add_compute_task_;
};

} // namespace common
} // namespace webdisk

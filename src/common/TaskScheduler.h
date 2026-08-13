#pragma once

#include <functional>
#include <utility>

class SubTask;

namespace webdisk {
namespace common {

// 业务层只描述“追加异步任务”和“进入计算队列”，具体任务序列由 RPC 调用方提供。
class TaskScheduler {
public:
    using AddTask = std::function<void(SubTask*)>;
    using AddComputeTask = std::function<void(std::function<void()>)>;

    TaskScheduler(AddTask add_task, AddComputeTask add_compute_task)
        : add_task_{std::move(add_task)},
          add_compute_task_{std::move(add_compute_task)} {}

    void add_task(SubTask* task) const { add_task_(task); }
    void add_compute_task(std::function<void()> function) const { add_compute_task_(std::move(function)); }

private:
    AddTask add_task_;
    AddComputeTask add_compute_task_;
};

} // namespace common
} // namespace webdisk

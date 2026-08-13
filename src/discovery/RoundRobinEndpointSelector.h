#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

#include "common/Result.h"
#include "discovery/ServiceEndpoint.h"

namespace webdisk {
namespace discovery {

// 在线程间共享轮询序号，从每次发现得到的最新实例集合中选择一个端点。
class RoundRobinEndpointSelector {
public:
    common::Result<ServiceEndpoint> select(const std::vector<ServiceEndpoint>& endpoints);

private:
    // relaxed 足以保证序号原子递增；端点数据由调用方只读持有，不依赖该原子量同步。
    std::atomic<uint64_t> next_{0};
};

} // namespace discovery
} // namespace webdisk

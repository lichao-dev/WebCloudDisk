#include "discovery/RoundRobinEndpointSelector.h"

#include <cstddef>

namespace webdisk {
namespace discovery {

common::Result<ServiceEndpoint> RoundRobinEndpointSelector::select(const std::vector<ServiceEndpoint>& endpoints) {
    if (endpoints.empty()) {
        return common::Result<ServiceEndpoint>::failure(503, "No healthy service instance available");
    }

    // 序号持续递增而不绑定具体实例，实例集合变化后仍能自然落到新的有效下标。
    const uint64_t sequence = next_.fetch_add(1, std::memory_order_relaxed);
    const size_t index = static_cast<size_t>(sequence % endpoints.size());
    return common::Result<ServiceEndpoint>::success(endpoints[index]);
}

} // namespace discovery
} // namespace webdisk

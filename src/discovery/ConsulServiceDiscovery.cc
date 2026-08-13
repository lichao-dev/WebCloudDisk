#include "discovery/ConsulServiceDiscovery.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <workflow/WFGlobal.h>

namespace webdisk {
namespace discovery {

ConsulServiceDiscovery::ConsulServiceDiscovery(config::Config::Consul config)
    : config_{std::move(config)} {}

common::Result<std::unique_ptr<ConsulServiceDiscovery>> ConsulServiceDiscovery::create(config::Config::Consul config) {
    auto discovery = std::unique_ptr<ConsulServiceDiscovery>{new ConsulServiceDiscovery{std::move(config)}};

    protocol::ConsulConfig consul_config;
    consul_config.set_token(discovery->config_.token);
    consul_config.set_datacenter(discovery->config_.datacenter);
    // 由 Consul 在服务端过滤非 passing 实例，避免网关误选尚未就绪或已经失效的端点。
    consul_config.set_passing(true);
    if (discovery->client_.init(discovery->config_.url, std::move(consul_config)) != 0) {
        return common::Result<std::unique_ptr<ConsulServiceDiscovery>>::failure(
            500, "Failed to initialize Consul discovery client");
    }
    return common::Result<std::unique_ptr<ConsulServiceDiscovery>>::success(std::move(discovery));
}

WFConsulTask* ConsulServiceDiscovery::create_discover_task(const std::string& service_name, DiscoverCallback callback) {
    return client_.create_discover_task(
        "", service_name, config_.retry_max, [callback = std::move(callback)](WFConsulTask* task) {
            if (task->get_state() != WFT_STATE_SUCCESS) {
                std::string message = "Consul service discovery failed: ";
                message += WFGlobal::get_error_string(task->get_state(), task->get_error());
                callback(DiscoverResult::failure(503, std::move(message)));
                return;
            }

            // WFConsulTask 会在回调返回后销毁，必须在这里复制并转换发现结果。
            std::vector<protocol::ConsulServiceInstance> instances;
            if (!task->get_discover_result(instances)) {
                callback(DiscoverResult::failure(503, "Invalid Consul service discovery response"));
                return;
            }
            callback(make_endpoints(instances));
        });
}

ConsulServiceDiscovery::DiscoverResult
ConsulServiceDiscovery::make_endpoints(const std::vector<protocol::ConsulServiceInstance>& instances) {
    std::vector<ServiceEndpoint> endpoints;
    endpoints.reserve(instances.size());
    for (const auto& instance : instances) {
        const auto& address = instance.service.service_address;
        // 当前服务注册时始终公布显式地址；空地址或端口 0 不能用于创建 sRPC 客户端。
        if (address.first.empty() || address.second == 0) {
            continue;
        }
        endpoints.push_back({instance.service.service_id, address.first, address.second});
    }

    if (endpoints.empty()) {
        return DiscoverResult::failure(503, "No healthy service instance available");
    }

    // Consul 返回顺序没有负载均衡语义，稳定排序使轮询在相同实例集合上保持可预测。
    std::stable_sort(endpoints.begin(), endpoints.end(), [](const ServiceEndpoint& left, const ServiceEndpoint& right) {
        return left.instance_id < right.instance_id;
    });
    return DiscoverResult::success(std::move(endpoints));
}

} // namespace discovery
} // namespace webdisk

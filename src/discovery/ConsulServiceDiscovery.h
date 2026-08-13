#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <workflow/ConsulDataTypes.h>
#include <workflow/WFConsulClient.h>

#include "common/Result.h"
#include "config/Config.h"
#include "discovery/ServiceEndpoint.h"

namespace webdisk {
namespace discovery {

// 创建异步 Consul 查询任务，并把 passing 实例转换为项目内端点。
class ConsulServiceDiscovery {
public:
    using DiscoverResult = common::Result<std::vector<ServiceEndpoint>>;
    using DiscoverCallback = std::function<void(DiscoverResult)>;

    static common::Result<std::unique_ptr<ConsulServiceDiscovery>> create(config::Config::Consul config);

    ConsulServiceDiscovery(const ConsulServiceDiscovery&) = delete;
    ConsulServiceDiscovery& operator=(const ConsulServiceDiscovery&) = delete;

    // 只创建任务而不启动；调用方必须把返回任务加入当前 Workflow 序列。
    WFConsulTask* create_discover_task(const std::string& service_name, DiscoverCallback callback);

    // 过滤无效地址并稳定排序；公开此转换函数便于无网络单元测试。
    static DiscoverResult make_endpoints(const std::vector<protocol::ConsulServiceInstance>& instances);

private:
    explicit ConsulServiceDiscovery(config::Config::Consul config);

    config::Config::Consul config_;
    WFConsulClient client_;
};

} // namespace discovery
} // namespace webdisk

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <workflow/WFConsulClient.h>

#include "common/Result.h"
#include "config/Config.h"

namespace webdisk {
namespace discovery {

// 封装服务实例向 Consul 注册和注销的同步启动阶段操作。
class ConsulServiceRegistrar {
public:
    static common::Result<std::unique_ptr<ConsulServiceRegistrar>> create(config::Consul config);

    ConsulServiceRegistrar(const ConsulServiceRegistrar&) = delete;
    ConsulServiceRegistrar& operator=(const ConsulServiceRegistrar&) = delete;

    // 注册服务实例并同步等待 Consul 返回结果。
    common::Result<void> register_service(const std::string& service_name, const std::string& service_address,
                                          uint16_t service_port);
    // 注销当前成功注册的实例；尚未注册时直接成功。
    common::Result<void> deregister_service();

    // 返回当前已注册实例的 ID，未注册或已注销时为空。
    const std::string& instance_id() const { return instance_id_; }

    // 根据服务名、公布地址和端口生成稳定的实例 ID。
    static std::string make_instance_id(const std::string& service_name, const std::string& service_address,
                                        uint16_t service_port);
    // 生成 TCP 健康检查地址，并为裸 IPv6 地址补充方括号。
    static std::string make_tcp_address(const std::string& host, uint16_t port);

private:
    explicit ConsulServiceRegistrar(config::Consul config);

    config::Consul config_;
    WFConsulClient client_;
    std::string instance_id_;
};

} // namespace discovery
} // namespace webdisk

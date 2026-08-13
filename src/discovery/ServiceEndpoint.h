#pragma once

#include <cstdint>
#include <string>

namespace webdisk {
namespace discovery {

// 表示一次 RPC 调用可以选择的健康服务实例地址。
struct ServiceEndpoint {
    std::string instance_id;
    std::string host;
    uint16_t port{0};
};

} // namespace discovery
} // namespace webdisk

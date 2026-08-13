#include "discovery/ConsulServiceRegistrar.h"

#include <cctype>
#include <string>
#include <utility>

#include <workflow/ConsulDataTypes.h>
#include <workflow/WFFacilities.h>
#include <workflow/WFGlobal.h>

namespace webdisk {
namespace discovery {
namespace {

// 保存回调中的 Workflow 状态，供等待注册或注销的主线程读取。
struct ConsulTaskResult {
    int state{WFT_STATE_UNDEFINED}; // Workflow 任务状态
    int error{0}; // Workflow 系统错误或任务错误码
    std::string http_status; // Consul HTTP 响应状态码
};

// 在 WFConsulTask 销毁前复制诊断信息，供等待线程在回调完成后读取。
void capture_task_result(WFConsulTask* task, ConsulTaskResult& result) {
    result.state = task->get_state();
    result.error = task->get_error();
    const char* status_code = task->get_http_resp()->get_status_code();
    if (status_code != nullptr) {
        result.http_status = status_code;
    }
}

// 将 Workflow/HTTP 层的执行结果转换为项目统一的 Result<void>。
common::Result<void> task_result(const ConsulTaskResult& result, const std::string& operation) {
    if (result.state == WFT_STATE_SUCCESS) {
        return common::Result<void>::success();
    }

    std::string message = "Consul service " + operation + " failed: ";
    message += WFGlobal::get_error_string(result.state, result.error);
    // HTTP 状态可帮助区分 Consul 拒绝请求与底层网络错误。
    if (!result.http_status.empty()) {
        message += " (HTTP " + result.http_status + ")";
    }
    return common::Result<void>::failure(500, std::move(message));
}

} // namespace

ConsulServiceRegistrar::ConsulServiceRegistrar(config::Consul config)
    : config_{std::move(config)} {}

common::Result<std::unique_ptr<ConsulServiceRegistrar>> ConsulServiceRegistrar::create(config::Consul config) {
    auto registrar = std::unique_ptr<ConsulServiceRegistrar>{new ConsulServiceRegistrar{std::move(config)}};

    protocol::ConsulConfig consul_config;
    consul_config.set_token(registrar->config_.token);
    consul_config.set_datacenter(registrar->config_.datacenter);
    consul_config.set_replace_checks(true);
    consul_config.set_health_check(true);
    consul_config.set_check_interval(registrar->config_.health_check_interval_ms);
    consul_config.set_check_timeout(registrar->config_.health_check_timeout_ms);
    consul_config.set_auto_deregister_time(registrar->config_.deregister_critical_after_ms);
    consul_config.set_initial_status("critical");

    if (registrar->client_.init(registrar->config_.url, std::move(consul_config)) != 0) {
        return common::Result<std::unique_ptr<ConsulServiceRegistrar>>::failure(500,
                                                                                "Failed to initialize Consul client");
    }
    return common::Result<std::unique_ptr<ConsulServiceRegistrar>>::success(std::move(registrar));
}

common::Result<void> ConsulServiceRegistrar::register_service(const std::string& service_name,
                                                              const std::string& service_address,
                                                              uint16_t service_port) {
    const std::string instance_id = make_instance_id(service_name, service_address, service_port);
    // 注册任务创建时会复制客户端配置，因此先写入当前实例对应的 TCP 健康检查参数。
    protocol::ConsulConfig task_config;
    task_config.set_token(config_.token);
    task_config.set_datacenter(config_.datacenter);
    task_config.set_replace_checks(true);
    task_config.set_health_check(true);
    task_config.set_check_name(service_name + " TCP");
    task_config.set_check_tcp(make_tcp_address(config_.health_check_host, service_port));
    task_config.set_check_interval(config_.health_check_interval_ms);
    task_config.set_check_timeout(config_.health_check_timeout_ms);
    task_config.set_auto_deregister_time(config_.deregister_critical_after_ms);
    // 首次 TCP 检查通过前保持 critical，避免网关发现尚未就绪的实例。
    task_config.set_initial_status("critical");
    if (client_.init(config_.url, std::move(task_config)) != 0) {
        return common::Result<void>::failure(500, "Failed to initialize Consul registration client");
    }

    WFFacilities::WaitGroup wait_group{1};
    ConsulTaskResult result;
    auto* task = client_.create_register_task("", service_name, instance_id, config_.retry_max,
                                              [&result, &wait_group](WFConsulTask* completed_task) {
                                                  capture_task_result(completed_task, result);
                                                  wait_group.done();
                                              });

    protocol::ConsulService service{};
    service.service_address = {service_address, service_port};
    service.tag_override = false;
    task->set_service(&service);

    // 注册属于启动阶段，等待异步回调完成后再决定服务能否继续运行。
    task->start();
    wait_group.wait();

    auto registration = task_result(result, "registration");
    if (registration) {
        // 只有注册成功才保存实例 ID，防止退出时注销一个未注册的实例。
        instance_id_ = instance_id;
    }
    return registration;
}

common::Result<void> ConsulServiceRegistrar::deregister_service() {
    // 未成功注册时无需访问 Consul，便于启动失败路径安全清理。
    if (instance_id_.empty()) {
        return common::Result<void>::success();
    }

    WFFacilities::WaitGroup wait_group{1};
    ConsulTaskResult result;
    auto* task = client_.create_deregister_task("", instance_id_, config_.retry_max,
                                                [&result, &wait_group](WFConsulTask* completed_task) {
                                                    capture_task_result(completed_task, result);
                                                    wait_group.done();
                                                });
    // 等待 Consul 确认注销后，调用方再停止 RPC Server。
    task->start();
    wait_group.wait();

    auto deregistration = task_result(result, "deregistration");
    // 仅在确认注销成功后清空，失败时保留实例 ID 以便记录或重试。
    if (deregistration) {
        instance_id_.clear();
    }
    return deregistration;
}

std::string ConsulServiceRegistrar::make_instance_id(const std::string& service_name,
                                                     const std::string& service_address, uint16_t service_port) {
    std::string instance_id = service_name + "-";
    bool last_was_separator = false;
    // 将地址中的点、冒号等字符折叠为单个连字符，生成稳定且便于放入 URL 路径的实例 ID。
    for (const unsigned char ch : service_address) {
        if (std::isalnum(ch) != 0 || ch == '-') {
            instance_id.push_back(static_cast<char>(ch));
            last_was_separator = false;
        } else if (!last_was_separator) {
            instance_id.push_back('-');
            last_was_separator = true;
        }
    }
    if (!instance_id.empty() && instance_id.back() == '-') {
        instance_id.pop_back();
    }
    // 端口用于区分同一地址上运行的多个服务实例。
    instance_id += "-" + std::to_string(service_port);
    return instance_id;
}

std::string ConsulServiceRegistrar::make_tcp_address(const std::string& host, uint16_t port) {
    if (host.find(':') != std::string::npos && (host.front() != '[' || host.back() != ']')) {
        return "[" + host + "]:" + std::to_string(port);
    }
    return host + ":" + std::to_string(port);
}

} // namespace discovery
} // namespace webdisk

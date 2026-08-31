#include <algorithm>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>

#include <workflow/WFFacilities.h>

#include "common/ConfigCommandLine.h"
#include "config/Config.h"
#include "database/MySqlClient.h"
#include "discovery/ConsulServiceRegistrar.h"
#include "log/LogShutdownGuard.h"
#include "messaging/RabbitMqBackupTaskPublisher.h"
#include "repository/FileRepository.h"
#include "rpc/FileRpcServiceImpl.h"
#include "service/FileService.h"
#include "storage/FileStorage.h"

namespace {

WFFacilities::WaitGroup wait_group{1};

void signal_handler(int) {
    wait_group.done();
}

const char* usage() {
    return "Usage: cloud_disk_file_service -c <file>\n"
           "\n"
           "Options:\n"
           "  -c, --config <file>    Path to the file service INI configuration\n"
           "  -h, --help             Show this help message\n";
}

} // namespace

int main(int argc, char* argv[]) {
    auto options = webdisk::common::parse_config_command_line(argc, argv);
    if (!options) {
        std::cerr << "Error: " << options.error().message << "\n\n" << usage();
        return 2;
    }
    if (options.value().show_help) {
        std::cout << usage();
        return 0;
    }

    auto config_result = webdisk::config::FileServiceConfig::load(options.value().config_file.value());
    if (!config_result) {
        std::cerr << config_result.error().message << '\n';
        return 1;
    }
    webdisk::config::FileServiceConfig config = config_result.take_value();

    auto log_result = webdisk::log::Log::init(config.log);
    if (!log_result) {
        std::cerr << log_result.error().message << '\n';
        return 1;
    }
    webdisk::log::LogShutdownGuard log_shutdown_guard;
    LOG_INFO("Configuration: {}", config.to_string());

    // 初始化文件服务所依赖的数据库、元数据仓库和本地文件存储
    webdisk::db::MySqlClient database{config.database};
    webdisk::repository::FileRepository files{database};
    webdisk::storage::FileStorage storage{config.storage.root};
    auto storage_result = storage.init();
    if (!storage_result) {
        LOG_ERROR("File storage initialization failed: status={}", storage_result.error().status_code);
        return 1;
    }

    // [backup].enabled 开启时，通过 RabbitMQ 异步发布备份任务。
    // publisher 必须比 file_service 生命周期更长，因为 FileService 内部仅保存其非拥有指针。
    std::unique_ptr<webdisk::messaging::RabbitMqBackupTaskPublisher> backup_task_publisher;
    webdisk::service::FileService file_service{files, storage, config.storage.max_file_size};
    if (config.backup_enabled) {
        auto publisher_result = webdisk::messaging::RabbitMqBackupTaskPublisher::create(config.rabbitmq);
        if (!publisher_result) {
            LOG_ERROR("Backup publisher initialization failed: status={}", publisher_result.error().status_code);
            return 1;
        }
        backup_task_publisher = publisher_result.take_value();
        file_service.set_backup_task_publisher(backup_task_publisher.get());
    }

    webdisk::rpcserver::FileRpcServiceImpl rpc_service{file_service};
    srpc::RPCServerParams server_params = srpc::RPC_SERVER_PARAMS_DEFAULT;
    // 为文件上传预留额外 RPC 协议开销，避免合法的最大尺寸文件因封装开销被拒绝
    constexpr uint64_t overhead = 2ULL * 1024ULL * 1024ULL;
    const uint64_t request_limit = config.storage.max_file_size > std::numeric_limits<uint64_t>::max() - overhead
                                       ? config.storage.max_file_size
                                       : config.storage.max_file_size + overhead;
    server_params.request_size_limit =
        static_cast<size_t>(std::min<uint64_t>(request_limit, std::numeric_limits<size_t>::max()));
    srpc::SRPCServer server{&server_params};
    if (server.add_service(&rpc_service) != 0) {
        LOG_ERROR("Failed to register file RPC service");
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (server.start(config.rpc.file_service_port) != 0) {
        LOG_ERROR("File RPC service failed to start on port {}", config.rpc.file_service_port);
        return 1;
    }

    LOG_INFO("File RPC service listening on port {}", config.rpc.file_service_port);

    // RPC 服务启动成功后再注册到 Consul，避免服务尚不可用时就被其他实例发现
    auto registrar_result = webdisk::discovery::ConsulServiceRegistrar::create(config.consul);
    if (!registrar_result) {
        LOG_ERROR("File RPC service Consul client initialization failed: status={}",
                  registrar_result.error().status_code);
        server.stop();
        return 1;
    }
    auto registrar = registrar_result.take_value();
    auto registration = registrar->register_service(config.consul.file_service_name, config.rpc.file_service_host,
                                                    config.rpc.file_service_port);
    if (!registration) {
        LOG_ERROR("File RPC service Consul registration failed: status={}, error={}", registration.error().status_code,
                  registration.error().message);
        server.stop();
        return 1;
    }
    LOG_INFO("File RPC service registered with Consul: service={}, instance={}", config.consul.file_service_name,
             registrar->instance_id());

    wait_group.wait();

    // 退出时先从 Consul 注销，避免其他服务继续发现正在关闭的实例
    auto deregistration = registrar->deregister_service();
    if (!deregistration) {
        LOG_WARN("File RPC service Consul deregistration failed: status={}, error={}",
                 deregistration.error().status_code, deregistration.error().message);
    } else {
        LOG_INFO("File RPC service deregistered from Consul");
    }

    // Consul 注销完成后再停止 RPC 服务
    server.stop();
    LOG_INFO("File RPC service stopped");

    return 0;
}

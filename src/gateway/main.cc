#include <csignal>
#include <filesystem>
#include <iostream>
#include <string_view>

#include <workflow/WFFacilities.h>

#include "config/Config.h"
#include "gateway/GatewayApplication.h"
#include "log/Log.h"

namespace {

WFFacilities::WaitGroup wait_group{1};

void signal_handler(int) {
    wait_group.done();
}

webdisk::common::Result<std::filesystem::path> parse_config_path(int argc, char* argv[]) {
    if (argc != 3 || std::string_view(argv[1]) != "--config" || std::string_view(argv[2]).empty()) {
        return webdisk::common::Result<std::filesystem::path>::failure(
            500, "Usage: cloud_disk_api_gateway --config <server.ini>");
    }
    return webdisk::common::Result<std::filesystem::path>::success(argv[2]);
}

} // namespace

int main(int argc, char* argv[]) {
    auto config_path = parse_config_path(argc, argv);
    if (!config_path) {
        std::cerr << config_path.error().message << '\n';
        return 1;
    }

    auto config = webdisk::config::Config::load(config_path.value());
    if (!config) {
        std::cerr << config.error().message << '\n';
        return 1;
    }

    webdisk::config::Config::Log log_config = config.value().log;
    log_config.file = log_config.gateway_file;
    auto log_result = webdisk::log::Log::init(log_config);
    if (!log_result) {
        std::cerr << log_result.error().message << '\n';
        return 1;
    }
    LOG_INFO("Configuration: {}", config.value().to_string());

    const uint16_t listen_port = config.value().server.port;
    webdisk::gateway::GatewayApplication application{config.take_value()};
    auto initialized = application.init();
    if (!initialized) {
        LOG_ERROR("API gateway initialization failed: status={}", initialized.error().status_code);
        webdisk::log::Log::shutdown();
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (application.start() != 0) {
        LOG_ERROR("API gateway failed to start on port {}", listen_port);
        webdisk::log::Log::shutdown();
        return 1;
    }

    LOG_INFO("API gateway listening on port {}", listen_port);
    wait_group.wait();
    application.stop();
    LOG_INFO("API gateway stopped");
    webdisk::log::Log::shutdown();
    return 0;
}

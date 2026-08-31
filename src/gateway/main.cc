#include <csignal>
#include <iostream>

#include <workflow/WFFacilities.h>

#include "common/ConfigCommandLine.h"
#include "config/Config.h"
#include "gateway/GatewayApplication.h"
#include "log/LogShutdownGuard.h"

namespace {

WFFacilities::WaitGroup wait_group{1};

void signal_handler(int) {
    wait_group.done();
}

const char* usage() {
    return "Usage: cloud_disk_api_gateway -c <file>\n"
           "\n"
           "Options:\n"
           "  -c, --config <file>    Path to the gateway INI configuration\n"
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

    auto config = webdisk::config::GatewayConfig::load(options.value().config_file.value());
    if (!config) {
        std::cerr << config.error().message << '\n';
        return 1;
    }

    auto log_result = webdisk::log::Log::init(config.value().log);
    if (!log_result) {
        std::cerr << log_result.error().message << '\n';
        return 1;
    }
    webdisk::log::LogShutdownGuard log_shutdown_guard;
    LOG_INFO("Configuration: {}", config.value().to_string());

    const uint16_t listen_port = config.value().server.port;
    webdisk::gateway::GatewayApplication application{config.take_value()};
    auto initialized = application.init();
    if (!initialized) {
        LOG_ERROR("API gateway initialization failed: status={}", initialized.error().status_code);
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (application.start() != 0) {
        LOG_ERROR("API gateway failed to start on port {}", listen_port);
        return 1;
    }

    LOG_INFO("API gateway listening on port {}", listen_port);
    wait_group.wait();
    application.stop();
    LOG_INFO("API gateway stopped");
    return 0;
}

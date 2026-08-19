#include <csignal>
#include <filesystem>
#include <iostream>
#include <string_view>

#include "common/Result.h"
#include "config/Config.h"
#include "log/LogShutdownGuard.h"
#include "storage/FileStorage.h"
#include "storage/OssBackupStorage.h"
#include "worker/BackupWorker.h"

namespace {

volatile std::sig_atomic_t stop_requested = 0;

void signal_handler(int) {
    stop_requested = 1;
}

webdisk::common::Result<std::filesystem::path> parse_config_path(int argc, char* argv[]) {
    if (argc != 3 || std::string_view(argv[1]) != "--config" || std::string_view(argv[2]).empty()) {
        return webdisk::common::Result<std::filesystem::path>::failure(
            500, "Usage: cloud_disk_backup_worker --config <backup-worker.ini>");
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

    auto config = webdisk::config::BackupWorkerConfig::load(config_path.value());
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

    webdisk::storage::FileStorage file_storage{config.value().storage.root};
    auto storage_initialized = file_storage.init();
    if (!storage_initialized) {
        LOG_ERROR("Backup worker local storage initialization failed");
        return 1;
    }

    auto oss_backup_storage = webdisk::storage::OssBackupStorage::create(config.value().oss);
    if (!oss_backup_storage) {
        LOG_ERROR("Backup worker OSS initialization failed");
        return 1;
    }

    auto worker =
        webdisk::worker::BackupWorker::create(config.value().rabbitmq, file_storage, *oss_backup_storage.value());
    if (!worker) {
        LOG_ERROR("Backup worker initialization failed");
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    auto result = worker.value()->run([] { return stop_requested != 0; });
    if (!result) {
        LOG_ERROR("Backup worker exited with an error: {}", result.error().message);
        return 1;
    }

    return 0;
}

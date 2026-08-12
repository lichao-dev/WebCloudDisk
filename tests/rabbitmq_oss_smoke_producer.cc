#include <filesystem>
#include <iostream>
#include <string_view>

#include "common/Result.h"
#include "config/Config.h"
#include "messaging/BackupTask.h"
#include "messaging/RabbitMqBackupTaskPublisher.h"
#include "security/Sha256.h"
#include "storage/FileStorage.h"

namespace {

constexpr std::string_view smoke_content{"WebCloudDisk RabbitMQ stage-1 OSS smoke test"};

webdisk::common::Result<std::filesystem::path> parse_config_path(int argc, char* argv[]) {
    if (argc != 3 || std::string_view(argv[1]) != "--config" || std::string_view(argv[2]).empty()) {
        return webdisk::common::Result<std::filesystem::path>::failure(
            500, "Usage: cloud_disk_rabbitmq_oss_smoke_producer --config <server.ini>");
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
    if (!config || !config.value().oss.enabled || !config.value().rabbitmq.enabled) {
        std::cerr << "OSS and RabbitMQ smoke-test configuration is unavailable\n";
        return 1;
    }

    webdisk::storage::FileStorage file_storage{config.value().storage.root};
    auto initialized = file_storage.init();
    if (!initialized) {
        std::cerr << initialized.error().message << '\n';
        return 1;
    }

    auto hashcode = webdisk::security::Sha256::hex(smoke_content);
    if (!hashcode) {
        std::cerr << hashcode.error().message << '\n';
        return 1;
    }

    auto stored = file_storage.store_if_absent(hashcode.value(), smoke_content);
    if (!stored) {
        std::cerr << stored.error().message << '\n';
        return 1;
    }

    auto publisher = webdisk::messaging::RabbitMqBackupTaskPublisher::create(config.value().rabbitmq);
    if (!publisher) {
        std::cerr << publisher.error().message << '\n';
        return 1;
    }

    auto published = publisher.value()->publish(webdisk::messaging::BackupTask{
        webdisk::messaging::BackupTask::current_version, hashcode.value(), smoke_content.size()});
    if (!published) {
        std::cerr << published.error().message << '\n';
        return 1;
    }

    std::cout << "RabbitMQ OSS smoke task published: hashcode=" << hashcode.value()
              << ", new_content=" << std::boolalpha << stored.value() << '\n';
    return 0;
}

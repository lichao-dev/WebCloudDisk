#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include <SimpleAmqpClient/SimpleAmqpClient.h>

#include "common/Result.h"
#include "config/Config.h"
#include "messaging/BackupTask.h"
#include "messaging/RabbitMqBackupTaskPublisher.h"

namespace {

webdisk::common::Result<std::filesystem::path> parse_config_path(int argc, char* argv[]) {
    if (argc != 3 || std::string_view(argv[1]) != "--config" || std::string_view(argv[2]).empty()) {
        return webdisk::common::Result<std::filesystem::path>::failure(
            500, "Usage: cloud_disk_rabbitmq_smoke_test --config <server.ini>");
    }
    return webdisk::common::Result<std::filesystem::path>::success(argv[2]);
}

AmqpClient::Channel::ptr_t open_channel(const webdisk::config::Config::RabbitMq& config) {
    AmqpClient::Channel::OpenOpts options;
    options.host = config.host;
    options.port = config.port;
    options.vhost = config.vhost;
    options.auth = AmqpClient::Channel::OpenOpts::BasicAuth{config.username, config.password};
    return AmqpClient::Channel::Open(options);
}

} // namespace

int main(int argc, char* argv[]) {
    auto config_path = parse_config_path(argc, argv);
    if (!config_path) {
        std::cerr << config_path.error().message << '\n';
        return 1;
    }

    auto config = webdisk::config::Config::load(config_path.value());
    if (!config || !config.value().oss.enabled) {
        std::cerr << "RabbitMQ smoke-test configuration is unavailable\n";
        return 1;
    }

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    config.value().rabbitmq.queue = "webdisk.oss.backup.smoke." + std::to_string(stamp);
    const auto cleanup = [&config] {
        try {
            open_channel(config.value().rabbitmq)->DeleteQueue(config.value().rabbitmq.queue);
        } catch (...) {
            // 队列可能尚未创建或连接已经失效；它使用唯一测试名称，不会影响业务队列。
        }
    };

    try {
        const std::string hashcode(64, 'a');
        constexpr uint64_t file_size{19};
        auto publisher = webdisk::messaging::RabbitMqBackupTaskPublisher::create(config.value().rabbitmq);
        if (!publisher) {
            std::cerr << "failed to initialize RabbitMQ smoke-test publisher\n";
            cleanup();
            return 1;
        }

        auto channel = open_channel(config.value().rabbitmq);
        const std::string consumer_tag =
            channel->BasicConsume(config.value().rabbitmq.queue, "", true, false, false, 1);
        auto published = publisher.value()->publish(
            webdisk::messaging::BackupTask{webdisk::messaging::BackupTask::current_version, hashcode, file_size});
        if (!published) {
            std::cerr << "failed to publish RabbitMQ smoke-test task\n";
            cleanup();
            return 1;
        }

        AmqpClient::Envelope::ptr_t envelope;
        if (!channel->BasicConsumeMessage(consumer_tag, envelope, 5000)) {
            std::cerr << "failed to consume RabbitMQ smoke-test task\n";
            cleanup();
            return 1;
        }

        auto task = webdisk::messaging::parse_backup_task(envelope->Message()->Body());
        if (!task || task.value().hashcode != hashcode || task.value().size != file_size ||
            envelope->Message()->ContentType() != "application/json" ||
            envelope->Message()->DeliveryMode() != AmqpClient::BasicMessage::dm_persistent) {
            std::cerr << "RabbitMQ smoke-test message is invalid\n";
            channel->BasicReject(envelope, false);
            cleanup();
            return 1;
        }

        channel->BasicAck(envelope);
        channel->BasicCancel(consumer_tag);
        cleanup();
        std::cout << "RabbitMQ publisher/consumer smoke test passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "RabbitMQ smoke test failed: " << e.what() << '\n';
        cleanup();
        return 1;
    }
}

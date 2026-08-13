#include "messaging/RabbitMqBackupTaskPublisher.h"

#include <exception>
#include <mutex>
#include <string>
#include <utility>

#include <SimpleAmqpClient/SimpleAmqpClient.h>

#include "log/Log.h"

namespace webdisk {
namespace messaging {

class RabbitMqBackupTaskPublisher::Impl {
public:
    Impl(std::string queue, AmqpClient::Channel::ptr_t channel)
        : queue{std::move(queue)},
          channel{std::move(channel)} {}

    std::string queue;
    AmqpClient::Channel::ptr_t channel;
    std::mutex mutex;
};

RabbitMqBackupTaskPublisher::RabbitMqBackupTaskPublisher(std::unique_ptr<Impl> impl)
    : impl_{std::move(impl)} {}

RabbitMqBackupTaskPublisher::~RabbitMqBackupTaskPublisher() = default;

common::Result<std::unique_ptr<RabbitMqBackupTaskPublisher>>
RabbitMqBackupTaskPublisher::create(const config::RabbitMq& config) {
    try {
        AmqpClient::Channel::OpenOpts options;
        options.host = config.host;
        options.port = config.port;
        options.vhost = config.vhost;
        options.auth = AmqpClient::Channel::OpenOpts::BasicAuth{config.username, config.password};

        auto channel = AmqpClient::Channel::Open(options);
        channel->DeclareQueue(config.queue, false, true, false, false);

        auto impl = std::make_unique<Impl>(config.queue, std::move(channel));
        auto publisher = std::unique_ptr<RabbitMqBackupTaskPublisher>(new RabbitMqBackupTaskPublisher(std::move(impl)));
        LOG_INFO("RabbitMQ backup publisher initialized: host={}, port={}, vhost={}, queue={}", config.host,
                 config.port, config.vhost, config.queue);
        return common::Result<std::unique_ptr<RabbitMqBackupTaskPublisher>>::success(std::move(publisher));
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize RabbitMQ backup publisher: host={}, port={}, vhost={}, queue={}, error={}",
                  config.host, config.port, config.vhost, config.queue, e.what());
        return common::Result<std::unique_ptr<RabbitMqBackupTaskPublisher>>::failure(
            500, "Failed to initialize RabbitMQ backup publisher");
    }
}

common::Result<void> RabbitMqBackupTaskPublisher::publish(const BackupTask& task) {
    auto body = serialize_backup_task(task);
    if (!body) {
        return common::Result<void>::failure(body.error().status_code, body.error().message);
    }

    try {
        const auto message = AmqpClient::BasicMessage::Create(body.value());
        message->ContentType("application/json");
        message->DeliveryMode(AmqpClient::BasicMessage::dm_persistent);
        message->MessageId(task.hashcode);

        // SimpleAmqpClient 的 Channel 面向单线程使用；序列化发布调用，避免多个 HTTP 回调并发访问同一连接。
        std::lock_guard<std::mutex> lock{impl_->mutex};
        // 默认交换机使用队列名作为 routing key；mandatory 确保无法路由时发布失败。
        impl_->channel->BasicPublish("", impl_->queue, message, true);
        return common::Result<void>::success();
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to publish OSS backup task: hashcode={}, error={}", task.hashcode, e.what());
        return common::Result<void>::failure(500, "Failed to publish OSS backup task");
    }
}

} // namespace messaging
} // namespace webdisk

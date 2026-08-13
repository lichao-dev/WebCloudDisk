#include "worker/BackupWorker.h"

#include <exception>
#include <string>
#include <utility>

#include <SimpleAmqpClient/SimpleAmqpClient.h>

#include "log/Log.h"
#include "messaging/BackupTask.h"
#include "storage/FileStorage.h"
#include "storage/OssBackupStorage.h"

namespace webdisk {
namespace worker {

class BackupWorker::Impl {
public:
    Impl(std::string queue, std::string consumer_tag, AmqpClient::Channel::ptr_t channel)
        : queue{std::move(queue)},
          consumer_tag{std::move(consumer_tag)},
          channel{std::move(channel)} {}

    std::string queue;
    std::string consumer_tag;
    AmqpClient::Channel::ptr_t channel;
};

BackupWorker::BackupWorker(storage::FileStorage& file_storage, storage::OssBackupStorage& oss_backup_storage,
                           std::unique_ptr<Impl> impl)
    : file_storage_{file_storage},
      oss_backup_storage_{oss_backup_storage},
      impl_{std::move(impl)} {}

BackupWorker::~BackupWorker() = default;

common::Result<std::unique_ptr<BackupWorker>> BackupWorker::create(const config::RabbitMq& config,
                                                                   storage::FileStorage& file_storage,
                                                                   storage::OssBackupStorage& oss_backup_storage) {
    try {
        AmqpClient::Channel::OpenOpts options;
        options.host = config.host;
        options.port = config.port;
        options.vhost = config.vhost;
        options.auth = AmqpClient::Channel::OpenOpts::BasicAuth{config.username, config.password};

        auto channel = AmqpClient::Channel::Open(options);
        channel->DeclareQueue(config.queue, false, true, false, false);
        // 关闭自动确认和独占消费，每次只预取一条，避免单个阻塞式 OSS 上传占住多条未确认消息。
        auto consumer_tag = channel->BasicConsume(config.queue, "", true, false, false, 1);

        auto impl = std::make_unique<Impl>(config.queue, std::move(consumer_tag), std::move(channel));
        auto worker =
            std::unique_ptr<BackupWorker>(new BackupWorker(file_storage, oss_backup_storage, std::move(impl)));
        LOG_INFO("RabbitMQ backup worker initialized: host={}, port={}, vhost={}, queue={}", config.host, config.port,
                 config.vhost, config.queue);
        return common::Result<std::unique_ptr<BackupWorker>>::success(std::move(worker));
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize RabbitMQ backup worker: host={}, port={}, vhost={}, queue={}, error={}",
                  config.host, config.port, config.vhost, config.queue, e.what());
        return common::Result<std::unique_ptr<BackupWorker>>::failure(500,
                                                                      "Failed to initialize RabbitMQ backup worker");
    }
}

common::Result<void> BackupWorker::run(const std::function<bool()>& should_stop) {
    LOG_INFO("RabbitMQ backup worker started: queue={}", impl_->queue);
    try {
        while (!should_stop()) {
            AmqpClient::Envelope::ptr_t envelope;
            // 最多阻塞 1 秒等待消息；超时后回到循环顶部，及时检查进程是否收到停止信号。
            if (!impl_->channel->BasicConsumeMessage(impl_->consumer_tag, envelope, 1000)) {
                continue;
            }

            // 消息格式不符合备份任务契约时直接丢弃，避免无法处理的消息被反复投递。
            auto task = messaging::parse_backup_task(envelope->Message()->Body());
            if (!task) {
                LOG_ERROR("Discarding invalid OSS backup task: error={}", task.error().message);
                impl_->channel->BasicReject(envelope, false);
                continue;
            }

            // Worker 只负责备份本地已存在的内容；路径非法或文件丢失都属于不可重试的数据问题。
            auto local_path = file_storage_.path_for(task.value().hashcode);
            if (!local_path || !file_storage_.exists(task.value().hashcode)) {
                LOG_ERROR("Discarding OSS backup task because local content is unavailable: hashcode={}",
                          task.value().hashcode);
                impl_->channel->BasicReject(envelope, false);
                continue;
            }

            auto backed_up = oss_backup_storage_.backup_file(task.value().hashcode, local_path.value());
            if (!backed_up) {
                // 第一阶段尚未实现延迟重试和自动重连。保持消息未确认并退出，连接关闭后 Broker 会重新入队。
                LOG_ERROR("OSS backup task failed; leaving message unacknowledged: hashcode={}, error={}",
                          task.value().hashcode, backed_up.error().message);
                return common::Result<void>::failure(backed_up.error().status_code, backed_up.error().message);
            }

            // 只有 OSS 上传成功后才确认消息，保证已确认任务对应的备份已经完成。
            impl_->channel->BasicAck(envelope);
            LOG_INFO("OSS backup task completed: hashcode={}, size={}", task.value().hashcode, task.value().size);
        }

        // 正常停止时主动取消消费者，通知 Broker 结束当前消费会话。
        impl_->channel->BasicCancel(impl_->consumer_tag);
        LOG_INFO("RabbitMQ backup worker stopped");
        return common::Result<void>::success();
    } catch (const std::exception& e) {
        LOG_ERROR("RabbitMQ backup worker stopped because the broker connection failed: error={}", e.what());
        return common::Result<void>::failure(500, "RabbitMQ backup worker connection failed");
    }
}

} // namespace worker
} // namespace webdisk

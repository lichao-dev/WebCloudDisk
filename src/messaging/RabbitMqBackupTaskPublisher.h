#pragma once

#include <memory>

#include "common/Result.h"
#include "config/Config.h"
#include "messaging/BackupTask.h"

namespace webdisk {
namespace messaging {

class RabbitMqBackupTaskPublisher {
public:
    static common::Result<std::unique_ptr<RabbitMqBackupTaskPublisher>> create(const config::Config::RabbitMq& config);

    ~RabbitMqBackupTaskPublisher();

    common::Result<void> publish(const BackupTask& task);

private:
    class Impl;

    explicit RabbitMqBackupTaskPublisher(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

} // namespace messaging
} // namespace webdisk

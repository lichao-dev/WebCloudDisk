#pragma once

#include <functional>
#include <memory>

#include "common/Result.h"
#include "config/Config.h"

namespace webdisk {
namespace storage {
class FileStorage;
class OssBackupStorage;
} // namespace storage

namespace worker {

class BackupWorker {
public:
    static common::Result<std::unique_ptr<BackupWorker>> create(const config::Config& config,
                                                                storage::FileStorage& file_storage,
                                                                storage::OssBackupStorage& oss_backup_storage);

    ~BackupWorker();

    common::Result<void> run(const std::function<bool()>& should_stop);

private:
    class Impl;

    BackupWorker(storage::FileStorage& file_storage, storage::OssBackupStorage& oss_backup_storage,
                 std::unique_ptr<Impl> impl);

    storage::FileStorage& file_storage_;
    storage::OssBackupStorage& oss_backup_storage_;
    std::unique_ptr<Impl> impl_;
};

} // namespace worker
} // namespace webdisk

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

#include "common/Result.h"
#include "config/Config.h"
#include "log/Log.h"
#include "security/Sha256.h"
#include "storage/OssBackupStorage.h"

namespace {

struct Arguments {
    std::filesystem::path config_path;
    std::filesystem::path file_path;
};

webdisk::common::Result<Arguments> parse_arguments(int argc, char* argv[]) {
    if (argc != 5) {
        return webdisk::common::Result<Arguments>::failure(
            500, "Usage: cloud_disk_oss_smoke_test --config <server.ini> --file <local-file>");
    }

    Arguments arguments;
    for (int index = 1; index < argc; index += 2) {
        const std::string_view option = argv[index];
        const std::string_view value = argv[index + 1];
        if (value.empty()) {
            return webdisk::common::Result<Arguments>::failure(500, "OSS smoke test arguments must not be empty");
        }
        if (option == "--config") {
            arguments.config_path = value;
        } else if (option == "--file") {
            arguments.file_path = value;
        } else {
            return webdisk::common::Result<Arguments>::failure(
                500, "Usage: cloud_disk_oss_smoke_test --config <server.ini> --file <local-file>");
        }
    }

    if (arguments.config_path.empty() || arguments.file_path.empty()) {
        return webdisk::common::Result<Arguments>::failure(
            500, "Usage: cloud_disk_oss_smoke_test --config <server.ini> --file <local-file>");
    }
    return webdisk::common::Result<Arguments>::success(std::move(arguments));
}

int run_smoke_test(const webdisk::config::Config& config, const std::filesystem::path& file_path) {
    if (!config.oss.enabled) {
        std::cerr << "OSS must be enabled in the configuration for the smoke test\n";
        return 1;
    }

    std::error_code error;
    if (!std::filesystem::is_regular_file(file_path, error)) {
        std::cerr << "Smoke test input is not a regular file\n";
        return 1;
    }

    const uintmax_t file_size = std::filesystem::file_size(file_path, error);
    if (error || file_size > config.storage.max_file_size) {
        std::cerr << "Smoke test input exceeds the configured file size limit or cannot be inspected\n";
        return 1;
    }

    std::ifstream input(file_path, std::ios::binary);
    if (!input) {
        std::cerr << "Failed to open smoke test input file\n";
        return 1;
    }
    const std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (input.bad()) {
        std::cerr << "Failed to read smoke test input file\n";
        return 1;
    }

    auto hashcode = webdisk::security::Sha256::hex(content);
    if (!hashcode) {
        std::cerr << hashcode.error().message << '\n';
        return 1;
    }

    auto backup_storage = webdisk::storage::OssBackupStorage::create(config.oss);
    if (!backup_storage) {
        std::cerr << backup_storage.error().message << '\n';
        return 1;
    }
    auto backed_up = backup_storage.value()->backup_file(hashcode.value(), file_path);
    if (!backed_up) {
        std::cerr << backed_up.error().message << '\n';
        return 1;
    }

    std::cout << "OSS smoke test passed: object_key=" << config.oss.key_prefix << hashcode.value() << '\n';
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    auto arguments = parse_arguments(argc, argv);
    if (!arguments) {
        std::cerr << arguments.error().message << '\n';
        return 1;
    }

    auto config = webdisk::config::Config::load(arguments.value().config_path);
    if (!config) {
        std::cerr << config.error().message << '\n';
        return 1;
    }

    auto log_result = webdisk::log::Log::init(config.value().log);
    if (!log_result) {
        std::cerr << log_result.error().message << '\n';
        return 1;
    }

    const int result = run_smoke_test(config.value(), arguments.value().file_path);
    webdisk::log::Log::shutdown();
    return result;
}

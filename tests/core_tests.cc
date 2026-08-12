#include "config/Config.h"
#include "log/Log.h"
#include "messaging/BackupTask.h"
#include "security/JwtService.h"
#include "security/PasswordHasher.h"
#include "security/Sha256.h"
#include "storage/FileStorage.h"
#include "storage/OssBackupStorage.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

namespace {

class EnvironmentVariableGuard {
public:
    explicit EnvironmentVariableGuard(std::string name)
        : name_{std::move(name)} {
        if (const char* value = std::getenv(name_.c_str()); value != nullptr) {
            original_value_ = value;
        }
    }

    ~EnvironmentVariableGuard() {
        if (original_value_) {
            setenv(name_.c_str(), original_value_->c_str(), 1);
        } else {
            unsetenv(name_.c_str());
        }
    }

    void set(const char* value) const { assert(setenv(name_.c_str(), value, 1) == 0); }
    void unset() const { assert(unsetenv(name_.c_str()) == 0); }

private:
    std::string name_;
    std::optional<std::string> original_value_;
};

std::filesystem::path make_test_root() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("web-cloud-disk-tests-" + std::to_string(stamp));
    std::filesystem::create_directories(root);
    return root;
}

void test_config(const std::filesystem::path& root) {
    const auto config_dir = root / "config";
    std::filesystem::create_directories(config_dir);
    const auto path = config_dir / "server.ini";
    std::ofstream output(path);
    output << "[server]\nport=9527\nweb_root=./test-www\n"
              "[database]\nhost=127.0.0.1\nport=3306\nusername=test\npassword=test\n"
              "database=CloudDisk\nretry_max=3\n"
              "[auth]\njwt_secret=01234567890123456789012345678901\n"
              "jwt_issuer=test\ntoken_ttl_seconds=3600\npassword_iterations=600000\n"
              "[storage]\nroot=./test-upload\nmax_file_size_bytes=1024\n"
              "[oss]\nenabled=true\nregion=cn-hangzhou\nbucket=test-backup-bucket\n"
              "key_prefix=test/backup\n"
              "[rabbitmq]\nenabled=true\nhost=127.0.0.1\nport=5672\nusername=test-user\npassword=test-password\n"
              "vhost=/test\nqueue=test.oss.backup.v1\n"
              "[log]\nlevel=info\nconsole=false\nfile=./test-log/server.log\nworker_file=./test-log/worker.log\n"
              "roll_size=2048\nroll_files=3\n";
    output.close();

    auto config = webdisk::config::Config::load(path);
    assert(config);
    assert(config.value().server.port == 9527);
    const auto working_dir = std::filesystem::current_path();
    assert(config.value().server.web_root == (working_dir / "test-www").lexically_normal());
    assert(config.value().storage.root == (working_dir / "test-upload").lexically_normal());
    assert(config.value().oss.enabled);
    assert(config.value().oss.region == "cn-hangzhou");
    assert(config.value().oss.bucket == "test-backup-bucket");
    assert(config.value().oss.key_prefix == "test/backup/");
    assert(config.value().rabbitmq.enabled);
    assert(config.value().rabbitmq.host == "127.0.0.1");
    assert(config.value().rabbitmq.port == 5672);
    assert(config.value().rabbitmq.username == "test-user");
    assert(config.value().rabbitmq.password == "test-password");
    assert(config.value().rabbitmq.vhost == "/test");
    assert(config.value().rabbitmq.queue == "test.oss.backup.v1");
    assert(!config.value().log.console);
    assert(config.value().log.file == (working_dir / "test-log" / "server.log").lexically_normal());
    assert(config.value().log.worker_file == (working_dir / "test-log" / "worker.log").lexically_normal());
    assert(config.value().log.roll_size == 2048);
    assert(config.value().log.roll_files == 3);

    std::ofstream invalid_output(path);
    invalid_output << "[database]\nhost=127.0.0.1\nusername=test\ndatabase=CloudDisk\n"
                      "[auth]\njwt_secret=01234567890123456789012345678901\n"
                      "[oss]\nenabled=true\nregion=\nbucket=test-backup-bucket\n";
    invalid_output.close();

    auto invalid_config = webdisk::config::Config::load(path);
    assert(!invalid_config);
    assert(invalid_config.error().message.find("oss.region") != std::string::npos);

    std::ofstream local_only_output(path);
    local_only_output << "[database]\nhost=127.0.0.1\nusername=test\ndatabase=CloudDisk\n"
                         "[auth]\njwt_secret=01234567890123456789012345678901\n";
    local_only_output.close();

    auto local_only_config = webdisk::config::Config::load(path);
    assert(local_only_config);
    assert(!local_only_config.value().oss.enabled);
    assert(!local_only_config.value().rabbitmq.enabled);

    std::ofstream mismatched_output(path);
    mismatched_output << "[database]\nhost=127.0.0.1\nusername=test\ndatabase=CloudDisk\n"
                         "[auth]\njwt_secret=01234567890123456789012345678901\n"
                         "[oss]\nenabled=true\nregion=cn-hangzhou\nbucket=test-backup-bucket\n";
    mismatched_output.close();

    auto mismatched_config = webdisk::config::Config::load(path);
    assert(!mismatched_config);
    assert(mismatched_config.error().message.find("enabled") != std::string::npos);
}

void test_backup_task_message() {
    const std::string hashcode(64, 'a');
    const webdisk::messaging::BackupTask task{webdisk::messaging::BackupTask::current_version, hashcode, 42};
    auto serialized = webdisk::messaging::serialize_backup_task(task);
    assert(serialized);
    assert(!webdisk::messaging::serialize_backup_task(
        webdisk::messaging::BackupTask{webdisk::messaging::BackupTask::current_version, "invalid", 42}));

    auto parsed = webdisk::messaging::parse_backup_task(serialized.value());
    assert(parsed);
    assert(parsed.value().version == webdisk::messaging::BackupTask::current_version);
    assert(parsed.value().hashcode == hashcode);
    assert(parsed.value().size == 42);

    assert(!webdisk::messaging::parse_backup_task("not-json"));
    assert(!webdisk::messaging::parse_backup_task("{\"version\":2,\"hashcode\":\"" + hashcode + "\",\"size\":42}"));
    assert(!webdisk::messaging::parse_backup_task("{\"version\":1,\"hashcode\":\"invalid\",\"size\":42}"));
}

void test_password_hashing() {
    webdisk::security::PasswordHasher hasher(600000);
    auto encoded = hasher.hash("correct horse battery staple");
    assert(encoded);
    assert(encoded.value().find("pbkdf2-sha256$600000$") == 0);

    auto valid = hasher.verify("correct horse battery staple", encoded.value());
    auto invalid = hasher.verify("wrong", encoded.value());
    assert(valid.ok() && valid.value());
    assert(invalid.ok() && !invalid.value());
}

void test_jwt() {
    webdisk::security::JwtService jwt("01234567890123456789012345678901", "test", std::chrono::seconds(60));
    auto token = jwt.generate(42);
    assert(token);
    auto context = jwt.verify(token.value());
    assert(context);
    assert(context.value().user_id == 42);
    assert(!jwt.verify(token.value() + "broken"));
}

void test_storage(const std::filesystem::path& root) {
    webdisk::storage::FileStorage storage(root / "upload");
    assert(storage.init());
    constexpr std::string_view content{"binary\0content", 14};
    auto hashcode = webdisk::security::Sha256::hex(content);
    assert(hashcode);
    auto first = storage.store_if_absent(hashcode.value(), content);
    auto second = storage.store_if_absent(hashcode.value(), content);
    assert(first.ok() && first.value());
    assert(second.ok() && !second.value());
    assert(storage.exists(hashcode.value()));
}

void test_oss_backup_storage(const std::filesystem::path& root) {
    EnvironmentVariableGuard access_key_id{"OSS_ACCESS_KEY_ID"};
    EnvironmentVariableGuard access_key_secret{"OSS_ACCESS_KEY_SECRET"};
    EnvironmentVariableGuard session_token{"OSS_SESSION_TOKEN"};
    access_key_id.unset();
    access_key_secret.unset();
    session_token.unset();

    webdisk::config::Config::Oss config;
    config.enabled = true;
    config.region = "cn-hangzhou";
    config.bucket = "test-backup-bucket";

    auto missing_credentials = webdisk::storage::OssBackupStorage::create(config);
    assert(!missing_credentials);
    assert(missing_credentials.error().message.find("credentials") != std::string::npos);

    access_key_id.set("test-access-key-id");
    access_key_secret.set("test-access-key-secret");
    auto storage = webdisk::storage::OssBackupStorage::create(config);
    assert(storage);

    const std::string valid_hashcode(64, 'a');
    assert(!storage.value()->backup_file("invalid-hash", root));
    assert(!storage.value()->backup_file(valid_hashcode, root / "missing-file"));
    assert(!storage.value()->backup_file(valid_hashcode, root));
}

void test_logging(const std::filesystem::path& root) {
    webdisk::config::Config::Log invalid_config;
    invalid_config.level = "invalid";
    assert(!webdisk::log::Log::init(invalid_config));

    webdisk::config::Config::Log log_config;
    log_config.level = "debug";
    log_config.console = false;
    log_config.file = root / "log" / "test.log";
    log_config.roll_size = 1024 * 1024;
    log_config.roll_files = 2;
    assert(webdisk::log::Log::init(log_config));

    LOG_DEBUG("debug log test: {}", 42);
    LOG_ERROR("error log test");

    webdisk::config::Config app_config;
    app_config.log = log_config;
    app_config.database.username = "private-db-user";
    app_config.database.password = "private-db-password";
    app_config.auth.jwt_secret = "private-jwt-secret";
    app_config.oss.enabled = true;
    app_config.oss.region = "cn-hangzhou";
    app_config.oss.bucket = "test-backup-bucket";
    app_config.rabbitmq.enabled = true;
    app_config.rabbitmq.username = "private-rabbitmq-user";
    app_config.rabbitmq.password = "private-rabbitmq-password";
    const std::string config_text = app_config.to_string();
    LOG_INFO("Configuration: {}", config_text);
    webdisk::log::Log::shutdown();

    assert(webdisk::log::Log::init(log_config));
    LOG_INFO("second run log test");
    webdisk::log::Log::shutdown();

    const auto archived_log = log_config.file.parent_path() / "test.1.log";
    std::ifstream input(archived_log);
    const std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    assert(content.find("debug log test: 42") != std::string::npos);
    assert(content.find("error log test") != std::string::npos);
    assert(content.find("Configuration: server{") != std::string::npos);
    assert(content.find("username_configured=true") != std::string::npos);
    assert(content.find("password_configured=true") != std::string::npos);
    assert(content.find("jwt_secret_configured=true") != std::string::npos);
    assert(content.find("oss{enabled=true") != std::string::npos);
    assert(content.find("credentials_provider=environment") != std::string::npos);
    assert(content.find("rabbitmq{enabled=true") != std::string::npos);
    assert(content.find("username_configured=true") != std::string::npos);
    assert(content.find("password_configured=true") != std::string::npos);
    assert(content.find("private-db-user") == std::string::npos);
    assert(content.find("private-db-password") == std::string::npos);
    assert(content.find("private-jwt-secret") == std::string::npos);
    assert(content.find("private-rabbitmq-user") == std::string::npos);
    assert(content.find("private-rabbitmq-password") == std::string::npos);

    std::ifstream current_input(log_config.file);
    const std::string current_content((std::istreambuf_iterator<char>(current_input)),
                                      std::istreambuf_iterator<char>());
    assert(current_content.find("second run log test") != std::string::npos);
    assert(current_content.find("debug log test: 42") == std::string::npos);
}

} // namespace

int main() {
    const auto root = make_test_root();
    test_config(root);
    test_backup_task_message();
    test_password_hashing();
    test_jwt();
    test_storage(root);
    test_oss_backup_storage(root);
    test_logging(root);
    std::filesystem::remove_all(root);
    std::cout << "all core tests passed\n";
    return 0;
}

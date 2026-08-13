#include "config/Config.h"
#include "discovery/ConsulServiceDiscovery.h"
#include "discovery/ConsulServiceRegistrar.h"
#include "discovery/RoundRobinEndpointSelector.h"
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
#include <vector>

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
    const auto root = std::filesystem::temp_directory_path() / ("web-cloud-disk-tests-" + std::to_string(stamp));
    std::filesystem::create_directories(root);
    return root;
}

void test_config(const std::filesystem::path& root) {
    const auto config_dir = root / "config";
    std::filesystem::create_directories(config_dir);
    const auto working_dir = std::filesystem::current_path();
    const auto write_config = [&config_dir](const std::string& name, const std::string& content) {
        const auto path = config_dir / name;
        std::ofstream output(path);
        output << content;
        output.close();
        return path;
    };

    const auto gateway_path = write_config(
        "gateway.ini", "[server]\nport=9527\nweb_root=./test-www\n"
                       "[auth]\njwt_secret=01234567890123456789012345678901\njwt_issuer=test\n"
                       "token_ttl_seconds=invalid-for-gateway\n"
                       "[storage]\nmax_file_size_bytes=1024\n[rpc]\nrequest_timeout_ms=120000\n"
                       "[consul]\nurl=http://127.0.0.1:8500/\ndatacenter=dc1\ntoken=test-consul-token\n"
                       "user_service_name=webdisk-user-service\nfile_service_name=webdisk-file-service\nretry_max=3\n"
                       "health_check_interval_ms=invalid-for-gateway\n"
                       "[log]\nlevel=info\nconsole=false\nfile=./test-log/gateway.log\nroll_size=2048\nroll_files=3\n"
                       "[database]\nport=invalid-unrelated-value\n");
    auto gateway = webdisk::config::GatewayConfig::load(gateway_path);
    assert(gateway);
    assert(gateway.value().server.port == 9527);
    assert(gateway.value().server.web_root == (working_dir / "test-www").lexically_normal());
    assert(gateway.value().storage.max_file_size == 1024);
    assert(gateway.value().rpc.request_timeout_ms == 120000);
    assert(gateway.value().consul.url == "http://127.0.0.1:8500");
    assert(gateway.value().log.file == (working_dir / "test-log" / "gateway.log").lexically_normal());

    const auto user_path = write_config(
        "user-service.ini",
        "[database]\nhost=127.0.0.1\nport=3306\nusername=test\npassword=test\ndatabase=CloudDisk\n"
        "retry_max=3\n[auth]\njwt_secret=01234567890123456789012345678901\njwt_issuer=test\n"
        "token_ttl_seconds=3600\npassword_iterations=600000\n"
        "[rpc]\nuser_service_host=127.0.0.1\nuser_service_port=9601\n"
        "[consul]\nurl=http://127.0.0.1:8500\ndatacenter=dc1\nuser_service_name=webdisk-user-service\n"
        "health_check_host=host.docker.internal\nretry_max=3\nhealth_check_interval_ms=5000\n"
        "health_check_timeout_ms=2000\nderegister_critical_after_ms=600000\n"
        "[log]\nconsole=true\nfile=./test-log/user.log\n[storage]\nmax_file_size_bytes=invalid-unrelated-value\n");
    auto user = webdisk::config::UserServiceConfig::load(user_path);
    assert(user);
    assert(user.value().database.database == "CloudDisk");
    assert(user.value().auth.password_iterations == 600000);
    assert(user.value().rpc.user_service_port == 9601);
    assert(user.value().consul.health_check_timeout_ms == 2000);

    const auto file_path = write_config(
        "file-service.ini",
        "[database]\nhost=127.0.0.1\nport=3306\nusername=test\npassword=test\ndatabase=CloudDisk\n"
        "[storage]\nroot=./test-upload\nmax_file_size_bytes=1024\n[oss]\nregion=invalid-unrelated-value\n"
        "[rabbitmq]\nhost=127.0.0.1\nport=5672\nusername=test-user\npassword=test-password\n"
        "vhost=/test\nqueue=test.oss.backup.v1\n[rpc]\nfile_service_host=127.0.0.1\nfile_service_port=9602\n"
        "[consul]\nurl=http://127.0.0.1:8500\ndatacenter=dc1\nfile_service_name=webdisk-file-service\n"
        "health_check_host=host.docker.internal\nhealth_check_interval_ms=5000\nhealth_check_timeout_ms=2000\n"
        "deregister_critical_after_ms=600000\n[log]\nconsole=true\nfile=./test-log/file.log\n");
    auto file = webdisk::config::FileServiceConfig::load(file_path);
    assert(file);
    assert(file.value().storage.root == (working_dir / "test-upload").lexically_normal());
    assert(file.value().backup_enabled);
    assert(file.value().rabbitmq.queue == "test.oss.backup.v1");
    assert(file.value().rpc.file_service_port == 9602);

    const auto local_file_path =
        write_config("local-file-service.ini", "[database]\nhost=127.0.0.1\nusername=test\ndatabase=CloudDisk\n"
                                               "[backup]\nenabled=false\n[rabbitmq]\nport=invalid-unrelated-value\n");
    auto local_file = webdisk::config::FileServiceConfig::load(local_file_path);
    assert(local_file);
    assert(!local_file.value().backup_enabled);

    const auto worker_path = write_config(
        "backup-worker.ini", "[storage]\nroot=./test-upload\n[oss]\nregion=cn-hangzhou\n"
                             "bucket=test-backup-bucket\nkey_prefix=test/backup\n"
                             "[rabbitmq]\nhost=127.0.0.1\nport=5672\nusername=test-user\npassword=test-password\n"
                             "vhost=/test\nqueue=test.oss.backup.v1\n[log]\nconsole=true\nfile=./test-log/worker.log\n"
                             "[backup]\nenabled=false\n"
                             "[consul]\nurl=invalid-unrelated-value\n");
    auto worker = webdisk::config::BackupWorkerConfig::load(worker_path);
    assert(worker);
    assert(worker.value().storage.root == (working_dir / "test-upload").lexically_normal());
    assert(worker.value().oss.key_prefix == "test/backup/");
    assert(worker.value().rabbitmq.username == "test-user");

    const auto invalid_file_path =
        write_config("invalid-file-service.ini",
                     "[database]\nhost=127.0.0.1\nusername=test\ndatabase=CloudDisk\n[storage]\nroot=./upload\n"
                     "[backup]\nenabled=true\n[rpc]\nfile_service_host=127.0.0.1\n"
                     "[consul]\nhealth_check_interval_ms=5000\nhealth_check_timeout_ms=2000\n");
    auto invalid_file = webdisk::config::FileServiceConfig::load(invalid_file_path);
    assert(!invalid_file);
    assert(invalid_file.error().message.find("rabbitmq") != std::string::npos);

    const auto invalid_user_path = write_config(
        "invalid-user-service.ini", "[database]\nhost=127.0.0.1\nusername=test\ndatabase=CloudDisk\n"
                                    "[auth]\njwt_secret=01234567890123456789012345678901\n"
                                    "[consul]\nhealth_check_interval_ms=1000\nhealth_check_timeout_ms=2000\n");
    auto invalid_user = webdisk::config::UserServiceConfig::load(invalid_user_path);
    assert(!invalid_user);
    assert(invalid_user.error().message.find("health_check_timeout_ms") != std::string::npos);

    const auto invalid_worker_path = write_config("invalid-worker.ini", "[oss]\nregion=cn-hangzhou\n");
    auto invalid_worker = webdisk::config::BackupWorkerConfig::load(invalid_worker_path);
    assert(!invalid_worker);
    assert(invalid_worker.error().message.find("oss.region and oss.bucket") != std::string::npos);
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

void test_consul_service_identity() {
    using webdisk::discovery::ConsulServiceRegistrar;

    auto registrar = ConsulServiceRegistrar::create(webdisk::config::Consul{});
    assert(registrar);
    assert(registrar.value()->instance_id().empty());
    assert(registrar.value()->deregister_service());

    assert(ConsulServiceRegistrar::make_instance_id("webdisk-user-service", "127.0.0.1", 9601) ==
           "webdisk-user-service-127-0-0-1-9601");
    assert(ConsulServiceRegistrar::make_instance_id("webdisk-user-service", "host.example.com", 9601) ==
           "webdisk-user-service-host-example-com-9601");
    assert(ConsulServiceRegistrar::make_tcp_address("host.docker.internal", 9601) == "host.docker.internal:9601");
    assert(ConsulServiceRegistrar::make_tcp_address("::1", 9601) == "[::1]:9601");
}

void test_consul_service_discovery() {
    std::vector<protocol::ConsulServiceInstance> instances(4);
    instances[0].service.service_id = "user-b";
    instances[0].service.service_address = {"127.0.0.1", 9612};
    instances[1].service.service_id = "invalid-empty-host";
    instances[1].service.service_address = {"", 9613};
    instances[2].service.service_id = "user-a";
    instances[2].service.service_address = {"127.0.0.1", 9611};
    instances[3].service.service_id = "invalid-zero-port";
    instances[3].service.service_address = {"127.0.0.1", 0};

    auto endpoints = webdisk::discovery::ConsulServiceDiscovery::make_endpoints(instances);
    assert(endpoints);
    assert(endpoints.value().size() == 2);
    assert(endpoints.value()[0].instance_id == "user-a");
    assert(endpoints.value()[0].port == 9611);
    assert(endpoints.value()[1].instance_id == "user-b");
    assert(endpoints.value()[1].port == 9612);

    webdisk::discovery::RoundRobinEndpointSelector selector;
    auto first = selector.select(endpoints.value());
    auto second = selector.select(endpoints.value());
    auto third = selector.select(endpoints.value());
    assert(first && first.value().instance_id == "user-a");
    assert(second && second.value().instance_id == "user-b");
    assert(third && third.value().instance_id == "user-a");

    // 实例集合缩减后，持续递增的轮询序号仍必须映射到剩余的有效端点。
    const std::vector<webdisk::discovery::ServiceEndpoint> remaining{endpoints.value()[1]};
    auto after_removal = selector.select(remaining);
    assert(after_removal && after_removal.value().instance_id == "user-b");

    assert(!selector.select({}));
    assert(!webdisk::discovery::ConsulServiceDiscovery::make_endpoints({}));
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

    webdisk::config::Oss config;
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
    webdisk::config::Log invalid_config;
    invalid_config.level = "invalid";
    assert(!webdisk::log::Log::init(invalid_config));

    webdisk::config::Log log_config;
    log_config.level = "debug";
    log_config.console = false;
    log_config.file = root / "log" / "test.log";
    log_config.roll_size = 1024 * 1024;
    log_config.roll_files = 2;
    assert(webdisk::log::Log::init(log_config));

    LOG_DEBUG("debug log test: {}", 42);
    LOG_ERROR("error log test");

    webdisk::config::UserServiceConfig user_config;
    user_config.log = log_config;
    user_config.database.username = "private-db-user";
    user_config.database.password = "private-db-password";
    user_config.auth.jwt_secret = "private-jwt-secret";
    user_config.consul.token = "private-consul-token";
    webdisk::config::BackupWorkerConfig worker_config;
    worker_config.oss.region = "cn-hangzhou";
    worker_config.oss.bucket = "test-backup-bucket";
    worker_config.rabbitmq.username = "private-rabbitmq-user";
    worker_config.rabbitmq.password = "private-rabbitmq-password";
    const std::string config_text = user_config.to_string() + " " + worker_config.to_string();
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
    assert(content.find("Configuration: database{") != std::string::npos);
    assert(content.find("username_configured=true") != std::string::npos);
    assert(content.find("password_configured=true") != std::string::npos);
    assert(content.find("jwt_secret_configured=true") != std::string::npos);
    assert(content.find("oss{region=cn-hangzhou") != std::string::npos);
    assert(content.find("credentials_provider=environment") != std::string::npos);
    assert(content.find("rabbitmq{host=") != std::string::npos);
    assert(content.find("consul{url=") != std::string::npos);
    assert(content.find("token_configured=true") != std::string::npos);
    assert(content.find("username_configured=true") != std::string::npos);
    assert(content.find("password_configured=true") != std::string::npos);
    assert(content.find("private-db-user") == std::string::npos);
    assert(content.find("private-db-password") == std::string::npos);
    assert(content.find("private-jwt-secret") == std::string::npos);
    assert(content.find("private-rabbitmq-user") == std::string::npos);
    assert(content.find("private-rabbitmq-password") == std::string::npos);
    assert(content.find("private-consul-token") == std::string::npos);

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
    test_consul_service_identity();
    test_consul_service_discovery();
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

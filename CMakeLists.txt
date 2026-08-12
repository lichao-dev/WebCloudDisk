cmake_minimum_required(VERSION 3.22)

project(WebCloudDisk LANGUAGES C CXX)

# 使用 C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 可执行文件统一输出到 build/bin
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")


# =========================
# 查找系统依赖
# =========================
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)


# =========================
# 第三方库目录
# =========================
set(THIRD_PARTY_DIR "${CMAKE_SOURCE_DIR}/third_party")

# OSS SDK 使用系统 curl 和 OpenSSL，避免配置阶段下载依赖。
set(BUILD_TESTS OFF)
set(BUILD_SAMPLES OFF)
set(BUILD_PERF_TESTS OFF)
set(USE_SYSTEM_CURL ON)
set(USE_SYSTEM_OPENSSL ON)

add_subdirectory(
    "${THIRD_PARTY_DIR}/alibabacloud-oss-cpp-sdk-v2"
    "${CMAKE_BINARY_DIR}/third_party/alibabacloud-oss-cpp-sdk-v2"
    EXCLUDE_FROM_ALL
)

# spdlog 随主项目静态编译，关闭示例、测试、基准和安装规则。
set(SPDLOG_BUILD_SHARED OFF)
set(SPDLOG_BUILD_EXAMPLE OFF)
set(SPDLOG_BUILD_TESTS OFF)
set(SPDLOG_BUILD_BENCH OFF)
set(SPDLOG_INSTALL OFF)

add_subdirectory(
    "${THIRD_PARTY_DIR}/spdlog"
    "${CMAKE_BINARY_DIR}/third_party/spdlog"
    EXCLUDE_FROM_ALL
)

# RabbitMQ 客户端依赖随主项目按需静态编译，不安装到系统目录。
set(BUILD_SHARED_LIBS OFF)
set(BUILD_STATIC_LIBS ON)
# rabbitmq-c 需要保留静态 export 定义；EXCLUDE_FROM_ALL 会阻止它进入主项目安装结果。
set(INSTALL_STATIC_LIBS ON)
set(BUILD_EXAMPLES OFF)
set(BUILD_TOOLS OFF)
set(BUILD_TOOLS_DOCS OFF)
set(BUILD_API_DOCS OFF)
set(RUN_SYSTEM_TESTS OFF)
set(BUILD_OSSFUZZ OFF)
set(ENABLE_TESTING OFF)
set(ENABLE_SSL_SUPPORT ON)

add_subdirectory(
    "${THIRD_PARTY_DIR}/rabbitmq-c"
    "${CMAKE_BINARY_DIR}/third_party/rabbitmq-c"
    EXCLUDE_FROM_ALL
)

add_subdirectory(
    "${THIRD_PARTY_DIR}/SimpleAmqpClient"
    "${CMAKE_BINARY_DIR}/third_party/SimpleAmqpClient"
    EXCLUDE_FROM_ALL
)

target_include_directories(SimpleAmqpClient SYSTEM PUBLIC
    "${THIRD_PARTY_DIR}/SimpleAmqpClient/src"
)

add_library(SimpleAmqpClient::SimpleAmqpClient ALIAS SimpleAmqpClient)


# =========================
# 核心静态库
#
# 将业务代码编译为 libcloud_disk_core.a，
# server 和 test 程序都链接该库。
# =========================
add_library(cloud_disk_core STATIC
    src/app/Application.cc

    # 通用组件
    # 配置管理
    src/config/Config.cc

    # 数据库访问
    src/database/MySqlClient.cc

    # HTTP 请求处理
    src/http/ApiResponse.cc
    src/http/AuthHandler.cc
    src/http/AuthMiddleware.cc
    src/http/FileHandler.cc
    src/http/UserHandler.cc

    # 日志
    src/log/Log.cc

    # 数据访问层
    src/repository/FileRepository.cc
    src/repository/UserRepository.cc

    # 安全模块
    src/security/JwtService.cc
    src/security/PasswordHasher.cc
    src/security/Sha256.cc

    # 业务服务层
    src/service/AuthService.cc
    src/service/FileService.cc
    src/service/UserService.cc

    # 文件存储
    src/storage/FileStorage.cc
    src/storage/OssBackupStorage.cc
)


# 项目源码头文件路径
target_include_directories(cloud_disk_core PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
)


# 第三方库头文件
# SYSTEM 表示这些头文件来自外部库，
# 编译时降低第三方代码产生 warning 的影响。
target_include_directories(cloud_disk_core SYSTEM PUBLIC
    "${THIRD_PARTY_DIR}/inih"
    "${THIRD_PARTY_DIR}/nlohmann_json/include"
    "${THIRD_PARTY_DIR}/jwt-cpp/include"
    "${THIRD_PARTY_DIR}/workflow/_include"
    "${THIRD_PARTY_DIR}/wfrest/_include"
)


# =========================
# 编译宏
# =========================

# 使用 spdlog 编译版库
# 开启 trace 级别日志
target_compile_definitions(cloud_disk_core PUBLIC
    SPDLOG_COMPILED_LIB
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE
)


# 编译警告选项
target_compile_options(cloud_disk_core PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)


# =========================
# 链接第三方库
# =========================
target_link_libraries(cloud_disk_core PUBLIC
    # 阿里云 OSS C++ SDK V2
    alibabacloud_oss_v2::oss

    # wfrest HTTP 框架
    "${THIRD_PARTY_DIR}/wfrest/_lib/libwfrest.a"

    # Workflow 异步网络框架
    "${THIRD_PARTY_DIR}/workflow/_lib/libworkflow.a"

    # spdlog 静态库
    spdlog::spdlog

    # 系统库
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
    Threads::Threads

    # 动态加载相关库，例如 dlopen
    "${CMAKE_DL_LIBS}"
)


# =========================
# 主程序
# =========================
add_executable(cloud_disk_server
    src/main.cc
)

target_link_libraries(cloud_disk_server PRIVATE
    cloud_disk_core
)

install(TARGETS cloud_disk_server
    RUNTIME DESTINATION bin
)


# =========================
# 单元测试
# =========================
include(CTest)

if(BUILD_TESTING)

    add_executable(cloud_disk_core_tests
        tests/core_tests.cc
    )

    target_link_libraries(cloud_disk_core_tests PRIVATE
        cloud_disk_core
    )

    # 手动访问真实 OSS 的冒烟程序，不注册到 CTest，避免普通测试依赖网络和云端凭据。
    add_executable(cloud_disk_oss_smoke_test
        tests/oss_smoke_test.cc
    )

    target_link_libraries(cloud_disk_oss_smoke_test PRIVATE
        cloud_disk_core
    )

    add_executable(cloud_disk_rabbitmq_client_tests
        tests/rabbitmq_client_tests.cc
    )

    target_link_libraries(cloud_disk_rabbitmq_client_tests PRIVATE
        SimpleAmqpClient::SimpleAmqpClient
    )

    add_test(
        NAME cloud_disk_core_tests
        COMMAND cloud_disk_core_tests
    )

    add_test(
        NAME cloud_disk_rabbitmq_client_tests
        COMMAND cloud_disk_rabbitmq_client_tests
    )

endif()

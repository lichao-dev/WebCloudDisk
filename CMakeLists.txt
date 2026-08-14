cmake_minimum_required(VERSION 3.22)

project(WebCloudDisk LANGUAGES C CXX)

# 使用 C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 测试程序等默认输出到 build/bin
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(WEB_DISK_RUNTIME_CONFIGS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
foreach(CONFIG_TYPE IN LISTS WEB_DISK_RUNTIME_CONFIGS)
    set("CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_TYPE}" "${CMAKE_BINARY_DIR}/bin")
endforeach()


# =========================
# 查找系统依赖
# =========================
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

# 优先从当前 protoc 所在前缀查找 Protobuf Config 包；Ubuntu 发行版
# 未提供 ProtobufConfig.cmake 时，回退到 CMake 内置的 FindProtobuf 模块。
find_program(WEB_DISK_PROTOC_EXECUTABLE protoc REQUIRED)
get_filename_component(WEB_DISK_PROTOBUF_BIN_DIR "${WEB_DISK_PROTOC_EXECUTABLE}" DIRECTORY)
get_filename_component(WEB_DISK_PROTOBUF_PREFIX "${WEB_DISK_PROTOBUF_BIN_DIR}" DIRECTORY)
list(PREPEND CMAKE_PREFIX_PATH "${WEB_DISK_PROTOBUF_PREFIX}")
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)
find_package(Protobuf REQUIRED)
unset(CMAKE_FIND_PACKAGE_PREFER_CONFIG)


# =========================
# 第三方库目录
# =========================
set(THIRD_PARTY_DIR "${CMAKE_SOURCE_DIR}/third_party")

set(SRPC_GENERATOR "${THIRD_PARTY_DIR}/srpc/_bin/srpc_generator")
set(SRPC_LIBRARY "${THIRD_PARTY_DIR}/srpc/_lib/libsrpc.a")
if(NOT EXISTS "${SRPC_GENERATOR}" OR NOT EXISTS "${SRPC_LIBRARY}")
    message(FATAL_ERROR
        "sRPC is not built. Follow docs/第三方库编译与迁移指南.md to build "
        "srpc-static and srpc_generator under third_party/srpc first."
    )
endif()

find_library(SNAPPY_LIBRARY NAMES snappy
    HINTS "${WEB_DISK_PROTOBUF_PREFIX}/opt/snappy/lib"
    REQUIRED
)
find_library(LZ4_LIBRARY NAMES lz4
    HINTS "${WEB_DISK_PROTOBUF_PREFIX}/opt/lz4/lib"
    REQUIRED
)


# =========================
# Protobuf / sRPC 协议代码
# =========================
set(RPC_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(RPC_GENERATED_PROTO_DIR "${RPC_GENERATED_DIR}/proto")
set(RPC_PROTO_FILES
    "${CMAKE_SOURCE_DIR}/proto/common.proto"
    "${CMAKE_SOURCE_DIR}/proto/user_service.proto"
    "${CMAKE_SOURCE_DIR}/proto/file_service.proto"
)
set(RPC_GENERATED_SOURCES
    "${RPC_GENERATED_PROTO_DIR}/common.pb.cc"
    "${RPC_GENERATED_PROTO_DIR}/user_service.pb.cc"
    "${RPC_GENERATED_PROTO_DIR}/file_service.pb.cc"
)
set(RPC_GENERATED_HEADERS
    "${RPC_GENERATED_PROTO_DIR}/common.pb.h"
    "${RPC_GENERATED_PROTO_DIR}/user_service.pb.h"
    "${RPC_GENERATED_PROTO_DIR}/file_service.pb.h"
    "${RPC_GENERATED_PROTO_DIR}/common.srpc.h"
    "${RPC_GENERATED_PROTO_DIR}/user_service.srpc.h"
    "${RPC_GENERATED_PROTO_DIR}/file_service.srpc.h"
)

add_custom_command(
    OUTPUT ${RPC_GENERATED_SOURCES} ${RPC_GENERATED_HEADERS}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${RPC_GENERATED_PROTO_DIR}"
    COMMAND "${WEB_DISK_PROTOC_EXECUTABLE}"
        --proto_path=${CMAKE_SOURCE_DIR}
        --cpp_out=${RPC_GENERATED_DIR}
        ${RPC_PROTO_FILES}
    COMMAND "${SRPC_GENERATOR}" protobuf
        "proto/user_service.proto"
        "${RPC_GENERATED_PROTO_DIR}"
    COMMAND "${SRPC_GENERATOR}" protobuf
        "proto/file_service.proto"
        "${RPC_GENERATED_PROTO_DIR}"
    DEPENDS ${RPC_PROTO_FILES} "${SRPC_GENERATOR}"
    BYPRODUCTS
        "${RPC_GENERATED_PROTO_DIR}/client.pb_skeleton.cc"
        "${RPC_GENERATED_PROTO_DIR}/server.pb_skeleton.cc"
    COMMENT "Generating WebCloudDisk Protobuf and sRPC sources"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    VERBATIM
)

add_library(webdisk_rpc_protocol STATIC
    ${RPC_GENERATED_SOURCES}
    ${RPC_GENERATED_HEADERS}
)

target_include_directories(webdisk_rpc_protocol PUBLIC
    "${RPC_GENERATED_DIR}"
    "${RPC_GENERATED_PROTO_DIR}"
    "${THIRD_PARTY_DIR}/srpc/_include"
)

target_link_libraries(webdisk_rpc_protocol PUBLIC
    protobuf::libprotobuf
)

add_library(webdisk_srpc_runtime INTERFACE)
target_link_libraries(webdisk_srpc_runtime INTERFACE
    webdisk_rpc_protocol
    "${SRPC_LIBRARY}"
    "${THIRD_PARTY_DIR}/workflow/_lib/libworkflow.a"
    "${SNAPPY_LIBRARY}"
    "${LZ4_LIBRARY}"
    protobuf::libprotobuf
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
    Threads::Threads
    "${CMAKE_DL_LIBS}"
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Ubuntu 默认启用 --as-needed，可能在后续静态库产生引用前丢弃 OpenSSL 和 zlib。
    target_link_options(webdisk_srpc_runtime INTERFACE
        "LINKER:--no-as-needed"
    )
endif()

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
# 网关、RPC 服务、Worker 和测试程序都链接该库。
# =========================
add_library(cloud_disk_core STATIC
    # 通用组件
    # 配置管理
    src/config/Config.cc

    # 数据库访问
    src/database/MySqlClient.cc

    # 服务注册与发现
    src/discovery/ConsulServiceDiscovery.cc
    src/discovery/ConsulServiceRegistrar.cc
    src/discovery/RoundRobinEndpointSelector.cc

    # HTTP 请求处理
    src/http/ApiResponse.cc
    src/http/AuthMiddleware.cc

    # 日志
    src/log/Log.cc

    # 消息队列
    src/messaging/BackupTask.cc
    src/messaging/RabbitMqBackupTaskPublisher.cc

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

    # 后台任务
    src/worker/BackupWorker.cc
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

    # RabbitMQ C++ 客户端
    SimpleAmqpClient::SimpleAmqpClient

    # 系统库
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
    Threads::Threads

    # 动态加载相关库，例如 dlopen
    "${CMAKE_DL_LIBS}"
)


# =========================
# 正式进程
# =========================
add_executable(cloud_disk_backup_worker
    src/worker/main.cc
)

add_executable(cloud_disk_api_gateway
    src/gateway/GatewayApplication.cc
    src/gateway/main.cc
)

add_executable(cloud_disk_user_service
    src/rpc/UserRpcServiceImpl.cc
    src/user_service/main.cc
)

add_executable(cloud_disk_file_service
    src/file_service/main.cc
    src/rpc/FileRpcServiceImpl.cc
)

set(WEB_DISK_SERVICE_TARGETS
    cloud_disk_backup_worker
    cloud_disk_api_gateway
    cloud_disk_user_service
    cloud_disk_file_service
)

# 正式程序直接输出到项目根目录 bin，便于部署时与测试程序区分。
set_target_properties(${WEB_DISK_SERVICE_TARGETS} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

foreach(CONFIG_TYPE IN LISTS WEB_DISK_RUNTIME_CONFIGS)
    set_target_properties(${WEB_DISK_SERVICE_TARGETS} PROPERTIES
        "RUNTIME_OUTPUT_DIRECTORY_${CONFIG_TYPE}" "${CMAKE_SOURCE_DIR}/bin"
    )
endforeach()

target_link_libraries(cloud_disk_backup_worker PRIVATE
    cloud_disk_core
)

target_link_libraries(cloud_disk_api_gateway PRIVATE
    cloud_disk_core
    webdisk_srpc_runtime
)

target_link_libraries(cloud_disk_user_service PRIVATE
    cloud_disk_core
    webdisk_srpc_runtime
)

target_link_libraries(cloud_disk_file_service PRIVATE
    cloud_disk_core
    webdisk_srpc_runtime
)

install(TARGETS
    cloud_disk_backup_worker
    cloud_disk_api_gateway
    cloud_disk_user_service
    cloud_disk_file_service
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

    add_executable(cloud_disk_rpc_protocol_tests
        tests/rpc_protocol_tests.cc
    )

    target_link_libraries(cloud_disk_rpc_protocol_tests PRIVATE
        webdisk_rpc_protocol
    )

    # 手动验证两个真实 RPC 进程可达，不注册到 CTest，避免普通测试依赖运行中的服务。
    add_executable(cloud_disk_rpc_smoke_test
        tests/rpc_smoke_test.cc
    )

    target_link_libraries(cloud_disk_rpc_smoke_test PRIVATE
        cloud_disk_core
        webdisk_srpc_runtime
    )

    # 手动连接真实 Broker 的冒烟程序；验证正式发布器、消息属性、消费和 ACK，不访问 OSS。
    add_executable(cloud_disk_rabbitmq_smoke_test
        tests/rabbitmq_smoke_test.cc
    )

    target_link_libraries(cloud_disk_rabbitmq_smoke_test PRIVATE
        cloud_disk_core
    )

    # 手动端到端 OSS 验收的生产端；写入固定测试内容并向业务队列发布任务，不注册到 CTest。
    add_executable(cloud_disk_rabbitmq_oss_smoke_producer
        tests/rabbitmq_oss_smoke_producer.cc
    )

    target_link_libraries(cloud_disk_rabbitmq_oss_smoke_producer PRIVATE
        cloud_disk_core
    )

    add_test(
        NAME cloud_disk_core_tests
        COMMAND cloud_disk_core_tests
    )

    add_test(
        NAME cloud_disk_rabbitmq_client_tests
        COMMAND cloud_disk_rabbitmq_client_tests
    )

    add_test(
        NAME cloud_disk_rpc_protocol_tests
        COMMAND cloud_disk_rpc_protocol_tests
    )

endif()

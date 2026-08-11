cmake_minimum_required(VERSION 3.15)

project(WebCloudDisk LANGUAGES CXX)

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
    "${THIRD_PARTY_DIR}/spdlog/include"
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
    "${THIRD_PARTY_DIR}/spdlog/build/libspdlog.a"

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

    add_test(
        NAME cloud_disk_core_tests
        COMMAND cloud_disk_core_tests
    )

endif()

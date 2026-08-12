# WebCloudDisk

WebCloudDisk 是一个使用 C++17 开发的 Web 网盘后端。项目采用单体架构，基于 wfrest、Workflow 和 MySQL，实现用户认证、本地文件上传、查询和下载，并使用阿里云 OSS 提供可选的容灾备份。

> 本地磁盘始终是主存储；启用 OSS 与 RabbitMQ 后，Web 服务发布备份任务，独立 Worker 异步上传 OSS。文件删除和文件分享等功能留到后续阶段扩展。

## 第一期功能

- 用户注册、登录和当前用户信息查询
- JWT 身份认证
- 文件列表查询
- 本地文件上传和下载
- 基于 SHA-256 的内容寻址存储
- 可选的 RabbitMQ 异步 OSS 容灾备份
- INI 配置加载、参数校验和脱敏配置日志
- 控制台与滚动文件日志
- MySQL 用户和文件元数据持久化

## 技术栈

| 类别 | 组件 |
| --- | --- |
| 语言与构建 | C++17、CMake |
| HTTP 与异步任务 | wfrest、Workflow |
| 数据库 | MySQL 8.0 |
| 配置与 JSON | inih、nlohmann/json |
| 认证与安全 | jwt-cpp、OpenSSL、PBKDF2-HMAC-SHA256、SHA-256 |
| 日志 | spdlog |
| 文件存储 | 本地文件系统、阿里云 OSS C++ SDK V2 |
| 消息队列客户端 | rabbitmq-c、SimpleAmqpClient |

## 项目结构

```text
WebCloudDisk/
├── conf/           # 服务配置文件示例
├── docs/           # 需求说明和第三方库文档
├── sql/            # 数据库迁移脚本
├── src/
│   ├── app/        # 应用组装、路由注册和生命周期管理
│   ├── common/     # 通用返回值类型
│   ├── config/     # INI 配置加载和校验
│   ├── database/   # Workflow MySQL 客户端封装
│   ├── http/       # HTTP Handler、中间件和响应构造
│   ├── log/        # spdlog 封装
│   ├── messaging/  # 备份任务消息和 RabbitMQ 发布器
│   ├── model/      # 业务数据模型
│   ├── repository/ # MySQL 数据访问层
│   ├── security/   # 密码、JWT 和文件哈希
│   ├── server/     # Web 服务进程入口
│   ├── service/    # 业务逻辑层
│   ├── storage/    # 本地主存储和 OSS 备份实现
│   └── worker/     # Worker 进程入口和 RabbitMQ 备份任务消费者
├── tests/          # 单元测试
├── third_party/    # 第三方库源码及本地构建产物
└── www/            # 静态 Web 资源
```

主要调用方向是：`HTTP Handler -> Service -> Repository / Storage`。`Application` 负责集中组装这些组件，避免把应用生命周期和依赖关系分散到 `main()` 中。

## 准备环境

以下命令以 Ubuntu/Debian 为例安装基础构建依赖：

```bash
sudo apt update
sudo apt install -y build-essential cmake libssl-dev zlib1g-dev libboost-all-dev
```

项目依赖的第三方源码位于 `third_party/`。首次构建或把项目迁移到另一台机器后，需要按照 [第三方库编译与迁移指南](docs/第三方库编译与迁移指南.md) 先编译 Workflow 和 wfrest；不要直接复用其他机器或旧路径下生成的 CMake 缓存。
spdlog、OSS SDK、rabbitmq-c 和 SimpleAmqpClient 由主项目 CMake 按依赖顺序编译，不需要提前安装到系统目录。

## 初始化数据库

先创建数据库，再执行第一份迁移脚本：

```bash
mysql -u root -p -e "CREATE DATABASE cloud_disk CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;"
mysql -u root -p cloud_disk < sql/001_init.sql
```

如果 `cloud_disk` 已存在，可以跳过第一条命令。初始化脚本面向 MySQL 8.0；MySQL 5.7 不支持 `utf8mb4_0900_ai_ci`，需要改用该版本支持的排序规则。

迁移脚本使用编号前缀，是为了后续按 `001`、`002`、`003` 的顺序持续演进数据库结构。

## 配置

所有命令均应在项目根目录执行。先复制配置示例：

```bash
cp conf/server.ini.example conf/server.ini
```

至少需要修改以下配置：

- `[database]` 中的用户名、密码和数据库名称
- `[auth]` 中的 JWT 密钥；生产环境应使用长度不少于 32 字符的随机密钥
- 需要 OSS 异步备份时，同时启用 `[oss]` 和 `[rabbitmq]`，并配置 RabbitMQ 连接信息
- 按实际环境调整存储目录、日志目录、端口和上传大小限制

`conf/server.ini` 包含敏感信息，已被 Git 忽略，不应提交。配置中的 `./www`、`./upload` 和 `./log/...` 等相对路径，都以程序启动时的工作目录为基准，因此项目约定从项目根目录启动服务。

## 构建和测试

```bash
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_TESTING=ON \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

`cloud_disk_rabbitmq_client_tests` 只验证客户端静态库能够创建和读取 AMQP 消息，不连接 RabbitMQ Broker。

需要验证正式发布器、真实 Broker、消息属性、消费和手动 ACK，但不访问 OSS 时，执行：

```bash
cmake --build build --target cloud_disk_rabbitmq_smoke_test
./build/bin/cloud_disk_rabbitmq_smoke_test --config conf/server.ini
```

冒烟程序使用独立临时队列，直接消费并校验正式发布器发送的消息；成功后会手动 ACK 并删除临时队列。正式
Worker 的处理流程由下面的真实 OSS 端到端验收覆盖。

在明确授权向当前 OSS Bucket 写入固定测试对象后，可以执行完整的生产者、业务队列和真实 Worker 验收：

```bash
cmake --build build --target cloud_disk_rabbitmq_oss_smoke_producer cloud_disk_backup_worker
./build/bin/cloud_disk_rabbitmq_oss_smoke_producer --config conf/server.ini
./build/bin/cloud_disk_backup_worker --config conf/server.ini
```

生产端会把固定内容 `WebCloudDisk RabbitMQ stage-1 OSS smoke test` 写入当前 `storage.root`，并发布到业务队列；
Worker 上传成功后继续等待后续任务，需要按 `Ctrl+C` 停止。本地内容寻址文件和 OSS 对象会作为测试备份保留。

### OSS 手动冒烟验证

普通 CTest 不访问 OSS。需要验证 Region、Bucket、RAM 权限和 OSS SDK V2 时，先在 `conf/server.ini` 中同时启用
`[oss]`、`[rabbitmq]` 并填写相应连接配置，然后通过环境变量提供 OSS 凭据：

```bash
export OSS_ACCESS_KEY_ID="your_access_key_id"
export OSS_ACCESS_KEY_SECRET="your_access_key_secret"
# 使用 STS 临时凭据时再设置：
export OSS_SESSION_TOKEN="your_session_token"
```

选择一个不超过 `storage.max_file_size_bytes` 的本地文件执行：

```bash
cmake --build build --target cloud_disk_oss_smoke_test
./build/bin/cloud_disk_oss_smoke_test --config conf/server.ini --file README.md
```

程序会以文件内容的 SHA-256 作为对象名，将文件上传到配置的 `key_prefix` 下。该对象会作为正常备份保留，冒烟程序
不会自动删除它。凭据只从环境变量读取，不要写入或提交到配置文件。

服务正常运行时，文件会先写入本地主存储并写入数据库元数据，再向 RabbitMQ 的持久化队列发布任务。独立 Worker
根据内容哈希读取同一台机器上的本地文件，上传 OSS 成功后手动确认消息。第一阶段尚未实现 Transactional Outbox、
延迟重试、死信队列和断线重连，因此 RabbitMQ 发布失败只记录日志，不会撤销已经完成的本地上传。

服务程序生成在 `build/bin/cloud_disk_server`。如需将编译好的服务程序复制到项目根目录的 `bin/`，执行：

```bash
cmake --install build --prefix "$PWD"
```

安装后的程序为 `bin/cloud_disk_server` 和 `bin/cloud_disk_backup_worker`；测试程序仍只保留在 `build/bin/`。项目根目录的 `bin/` 已被 Git 忽略。如果项目目录发生移动，建议删除旧构建目录后重新执行 CMake 配置，避免缓存仍然引用旧路径。

## 启动和停止

两个进程必须从同一个项目根目录启动，以共享 `storage.root`。先确保 RabbitMQ Broker 已运行，然后分别在两个终端执行：

```bash
./bin/cloud_disk_backup_worker --config conf/server.ini
./bin/cloud_disk_server --config conf/server.ini
```

Worker 需要继承 `OSS_ACCESS_KEY_ID`、`OSS_ACCESS_KEY_SECRET` 和可选的 `OSS_SESSION_TOKEN` 环境变量。默认 HTTP
监听端口为 `9527`；两个进程都可通过 `Ctrl+C` 正常停止。

## HTTP API

| 方法 | 路径 | 认证 | 说明 |
| --- | --- | --- | --- |
| `POST` | `/api/v1/auth/register` | 否 | 注册用户 |
| `POST` | `/api/v1/auth/login` | 否 | 登录并获取 JWT |
| `GET` | `/api/v1/user/me` | Bearer Token | 查询当前用户 |
| `GET` | `/api/v1/files` | Bearer Token | 查询当前用户的文件列表 |
| `POST` | `/api/v1/files` | Bearer Token | 上传文件，表单字段名为 `file` |
| `GET` | `/api/v1/file/{id}` | Bearer Token | 下载指定文件 |

受保护接口使用以下请求头：

```http
Authorization: Bearer <token>
```

普通成功响应使用统一结构：

```json
{
  "status": "success",
  "message": "Operation successful",
  "data": {}
}
```

错误响应使用统一结构：

```json
{
  "status": "error",
  "message": "Error description"
}
```

详细字段和业务规则见 [Web 网盘项目说明](docs/Web网盘项目-Markdown/Web网盘项目.md)。

## 文件存储规则

上传完成后，文件内容保存为：

```text
<storage.root>/<sha256>
```

数据库保存文件元数据和内容哈希，不保存部署机器上的绝对路径。这样修改工作目录或存储根目录时不需要批量更新数据库。相同内容只保存一份物理文件；后续还可以基于这个哈希机制扩展上传前检查，实现秒传。

## 继续开发

- [项目上下文](PROJECT_CONTEXT.md)：当前架构、约定、已知问题和后续开发入口
- [Web 网盘项目说明](docs/Web网盘项目-Markdown/Web网盘项目.md)：完整需求和阶段规划
- [第三方库编译与迁移指南](docs/第三方库编译与迁移指南.md)：本地依赖的构建与迁移方式

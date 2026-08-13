# WebCloudDisk

WebCloudDisk 是一个使用 C++17 开发的 Web 网盘后端。第四期采用一个 wfrest HTTP API 网关和两个基于
sRPC/Protobuf 的后端服务，实现用户认证、本地文件上传、查询和下载，并使用阿里云 OSS 提供可选的容灾备份。
第五期已经接入 Consul：两个 RPC 服务支持注册、TCP 健康检查和停止时注销；网关会查询 `passing` 实例，简单
轮询选择端点，并按所选地址创建本次 sRPC 调用。

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
| RPC 与接口定义 | sRPC 0.10.4、Protobuf |
| 服务注册与发现 | Consul 2.0.3、Workflow `WFConsulClient` |
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
├── proto/          # 公共消息、用户服务和文件服务 RPC 协议
├── sql/            # 数据库迁移脚本
├── src/
│   ├── common/     # 通用返回值类型
│   ├── config/     # INI 配置加载和校验
│   ├── database/   # Workflow MySQL 客户端封装
│   ├── discovery/  # Consul 服务注册、健康实例发现和轮询选择
│   ├── file_service/ # 文件 RPC 服务进程入口
│   ├── gateway/    # HTTP API 网关
│   ├── http/       # 网关使用的认证中间件和统一响应构造
│   ├── log/        # spdlog 封装
│   ├── messaging/  # 备份任务消息和 RabbitMQ 发布器
│   ├── model/      # 业务数据模型
│   ├── repository/ # MySQL 数据访问层
│   ├── rpc/        # sRPC 服务端实现
│   ├── security/   # 密码、JWT 和文件哈希
│   ├── service/    # 业务逻辑层
│   ├── storage/    # 本地主存储和 OSS 备份实现
│   ├── user_service/ # 用户 RPC 服务进程入口
│   └── worker/     # Worker 进程入口和 RabbitMQ 备份任务消费者
├── tests/          # 单元测试
├── third_party/    # 第三方库源码及本地构建产物
└── www/            # 静态 Web 资源
```

第四期主要调用方向是：`HTTP -> API Gateway -> sRPC -> User/File Service -> Repository / Storage`。JWT 在网关
校验，用户服务负责注册、登录和用户查询，文件服务负责列表、上传、下载以及第一阶段 RabbitMQ 备份任务发布。

第五期当前完整流程：

```text
User/File RPC Service 启动
  ↓
注册到 Consul，并配置 TCP 健康检查
  ↓
Consul 标记 passing 实例
  ↓
ConsulServiceDiscovery 查询并转换健康端点
  ↓
RoundRobinEndpointSelector 轮询选择实例
  ↓
网关按所选 host:port 创建本次 sRPC 调用
```

用户服务和文件服务分别维护轮询序号，彼此的请求数量不会影响对方的实例选择顺序。

## 准备环境

以下命令以 Ubuntu/Debian 为例安装基础构建依赖：

```bash
sudo apt update
sudo apt install -y build-essential cmake libssl-dev zlib1g-dev libboost-all-dev
```

项目依赖的第三方源码位于 `third_party/`。首次构建或把项目迁移到另一台机器后，需要按照 [第三方库编译与迁移指南](docs/第三方库编译与迁移指南.md) 先编译 Workflow 和 wfrest；不要直接复用其他机器或旧路径下生成的 CMake 缓存。
spdlog、OSS SDK、rabbitmq-c 和 SimpleAmqpClient 由主项目 CMake 按依赖顺序编译，不需要提前安装到系统目录。
macOS 还需要先用 Homebrew 安装 Protobuf、Snappy 和 LZ4，并按指南将 sRPC 编译到 `third_party/srpc`，不安装到系统：

```bash
brew install protobuf snappy lz4 openssl@3
```

## 初始化数据库

先创建数据库，再执行第一份迁移脚本：

```bash
mysql -u root -p -e "CREATE DATABASE cloud_disk CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;"
mysql -u root -p cloud_disk < sql/001_init.sql
```

如果 `cloud_disk` 已存在，可以跳过第一条命令。初始化脚本面向 MySQL 8.0；MySQL 5.7 不支持 `utf8mb4_0900_ai_ci`，需要改用该版本支持的排序规则。

迁移脚本使用编号前缀，是为了后续按 `001`、`002`、`003` 的顺序持续演进数据库结构。

## 配置

所有命令均应在项目根目录执行。四个正式进程分别复制自己的配置示例：

```bash
cp conf/gateway.ini.example conf/gateway.ini
cp conf/user-service.ini.example conf/user-service.ini
cp conf/file-service.ini.example conf/file-service.ini
cp conf/backup-worker.ini.example conf/backup-worker.ini
```

至少需要修改以下配置：

- `user-service.ini` 和 `file-service.ini` 中各自的数据库账号、密码和数据库名称
- `gateway.ini` 与 `user-service.ini` 使用相同的 JWT 密钥和签发者；Token 有效期只由用户服务配置
- `gateway.ini` 与 `file-service.ini` 使用相同的上传大小限制
- `file-service.ini` 与 `backup-worker.ini` 使用相同的本地存储根目录和 RabbitMQ 业务队列
- `file-service.ini` 的 `[backup].enabled` 默认开启 RabbitMQ 任务发布；本地不运行 RabbitMQ 时可设为 `false`
- `backup-worker.ini` 不设启用开关；启动 Worker 本身即表示启用 OSS 备份
- 两个 RPC 服务的 `[consul]` 配置 TCP 健康检查地址；Docker Desktop 环境默认使用
  `health_check_host=host.docker.internal`

`conf/*.ini` 包含敏感信息，已被 Git 忽略，不应提交；只有 `*.ini.example` 会进入版本库。每个进程只解析并校验
自己的配置段，无关段不会造成启动失败。配置中的相对路径以程序启动时的工作目录为基准，因此项目约定从项目根目录
启动服务。

## 启动 macOS 单节点 Consul

本地开发使用 Docker Desktop 运行单节点 Consul 2.0.3。首次创建容器时执行：

```bash
docker pull hashicorp/consul:2.0.3
docker volume create consul_data
docker run \
  --name consul \
  -d \
  --restart unless-stopped \
  -p 8500:8500 \
  -p 8600:8600/udp \
  -v consul_data:/consul/data \
  hashicorp/consul:2.0.3 \
  consul agent \
  -server \
  -ui \
  -node=consul-server \
  -bootstrap-expect=1 \
  -client=0.0.0.0 \
  -data-dir=/consul/data
```

后续只需执行 `docker start consul`。通过下面的命令检查成员，并访问
[http://localhost:8500](http://localhost:8500) 查看 UI：

```bash
docker exec consul consul members
```

Consul Agent 运行在容器内，所以它不能通过 `127.0.0.1` 检查宿主机 RPC 端口。示例配置使用
`host.docker.internal`；如果改为 macOS 原生 Consul，需要相应调整 `[consul].health_check_host`。

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
`cloud_disk_rpc_protocol_tests` 验证用户和文件 Protobuf 消息，其中包括含 `\0` 字节的文件内容。
`cloud_disk_core_tests` 中的 Consul 用例不访问网络，只验证实例地址转换、无效端点过滤、稳定排序和轮询行为；
真实注册与健康检查仍需启动本地 Consul 和 RPC 服务观察。

启动两个 RPC 服务后，可以执行不访问 MySQL 业务数据的连通性检查：

```bash
./build/bin/cloud_disk_rpc_smoke_test \
  --user-config conf/user-service.ini \
  --file-config conf/file-service.ini
```

需要验证正式发布器、真实 Broker、消息属性、消费和手动 ACK，但不访问 OSS 时，执行：

```bash
cmake --build build --target cloud_disk_rabbitmq_smoke_test
./build/bin/cloud_disk_rabbitmq_smoke_test --config conf/file-service.ini
```

冒烟程序使用独立临时队列，直接消费并校验正式发布器发送的消息；成功后会手动 ACK 并删除临时队列。正式
Worker 的处理流程由下面的真实 OSS 端到端验收覆盖。

在明确授权向当前 OSS Bucket 写入固定测试对象后，可以执行完整的生产者、业务队列和真实 Worker 验收：

```bash
cmake --build build --target cloud_disk_rabbitmq_oss_smoke_producer cloud_disk_backup_worker
./build/bin/cloud_disk_rabbitmq_oss_smoke_producer --config conf/file-service.ini
./build/bin/cloud_disk_backup_worker --config conf/backup-worker.ini
```

生产端会把固定内容 `WebCloudDisk RabbitMQ stage-1 OSS smoke test` 写入当前 `storage.root`，并发布到业务队列；
Worker 上传成功后继续等待后续任务，需要按 `Ctrl+C` 停止。本地内容寻址文件和 OSS 对象会作为测试备份保留。

### OSS 手动冒烟验证

普通 CTest 不访问 OSS。需要验证 Region、Bucket、RAM 权限和 OSS SDK V2 时，先填写
`conf/backup-worker.ini` 中的 `[oss]` 和 `[rabbitmq]`，然后通过环境变量提供 OSS 凭据：

```bash
export OSS_ACCESS_KEY_ID="your_access_key_id"
export OSS_ACCESS_KEY_SECRET="your_access_key_secret"
# 使用 STS 临时凭据时再设置：
export OSS_SESSION_TOKEN="your_session_token"
```

选择一个本地文件执行：

```bash
cmake --build build --target cloud_disk_oss_smoke_test
./build/bin/cloud_disk_oss_smoke_test --config conf/backup-worker.ini --file README.md
```

程序会以文件内容的 SHA-256 作为对象名，将文件上传到配置的 `key_prefix` 下。该对象会作为正常备份保留，冒烟程序
不会自动删除它。凭据只从环境变量读取，不要写入或提交到配置文件。

服务正常运行时，文件会先写入本地主存储并写入数据库元数据，再向 RabbitMQ 的持久化队列发布任务。独立 Worker
根据内容哈希读取同一台机器上的本地文件，上传 OSS 成功后手动确认消息。第一阶段尚未实现 Transactional Outbox、
延迟重试、死信队列和断线重连，因此 RabbitMQ 发布失败只记录日志，不会撤销已经完成的本地上传。

服务程序生成在 `build/bin/`。如需将编译好的服务程序复制到项目根目录的 `bin/`，执行：

```bash
cmake --install build --prefix "$PWD"
```

安装结果包含 API 网关、用户 RPC 服务、文件 RPC 服务和备份 Worker；测试程序仍只保留在
`build/bin/`。项目根目录的 `bin/` 已被 Git 忽略。如果项目目录发生移动，建议删除旧构建目录后重新执行 CMake
配置，避免缓存仍然引用旧路径。

## 启动和停止

第五期开发模式应先确保 Consul 正常运行，再从项目根目录启动两个 RPC 服务和 API 网关。文件服务默认发布备份任务，
因此应确保 RabbitMQ Broker 已运行；启动备份 Worker 本身即表示启用 OSS 备份：

```bash
./bin/cloud_disk_user_service --config conf/user-service.ini
./bin/cloud_disk_file_service --config conf/file-service.ini
./bin/cloud_disk_api_gateway --config conf/gateway.ini
./bin/cloud_disk_backup_worker --config conf/backup-worker.ini
```

默认端口分别为 HTTP `9527`、用户 RPC `9601`、文件 RPC `9602`。所有进程都可通过 `Ctrl+C` 正常停止。Worker
需要继承 `OSS_ACCESS_KEY_ID`、`OSS_ACCESS_KEY_SECRET` 和可选的 `OSS_SESSION_TOKEN` 环境变量。

用户和文件 RPC 服务会在监听成功后注册到 Consul；注册失败时服务退出。正常停止时先注销 Consul 实例，再停止
RPC Server。网关对每次请求查询 `passing` 实例并简单轮询；没有健康实例或 Consul 查询失败时返回 HTTP 503。

本地联调已使用同一逻辑服务名启动两个用户服务实例：连续请求依次选择两个端口；注销其中一个实例后，后续请求
继续调用剩余健康实例；全部实例注销后，网关返回 `503 No healthy service instance available`。

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

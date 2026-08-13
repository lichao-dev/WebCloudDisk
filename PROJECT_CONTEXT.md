# WebCloudDisk 项目上下文

> 更新时间：2026-08-13
> 当前项目路径：`/Users/lichao/workspace/projects/WebCloudDisk`
> 目标：让新的 Codex 对话先阅读本文件，再基于当前 checkout 和实时 Git 状态继续工作。

## 1. 当前阶段与范围

WebCloudDisk 是一个 C++17 Web 网盘项目。第三期 RabbitMQ 异步 OSS 备份第一阶段已经实现，当前已经覆盖：

- 用户注册与登录。
- JWT 访问令牌。
- 获取当前用户信息。
- 查询当前用户文件列表。
- 本地文件上传和下载。
- 可选的 RabbitMQ 异步 OSS 备份；本地磁盘仍是主存储。
- INI 配置加载与校验。
- 统一 JSON API 响应、错误传播和日志。
- 第四期第一版微服务拆分：HTTP API 网关、用户 sRPC 服务和文件 sRPC 服务。
- Protobuf 协议自动生成、项目内 sRPC 静态链接，以及 macOS 本机三进程联调。
- 第五期 Consul：两个 RPC 服务启动后注册、TCP 健康检查、停止前注销。
- 网关查询 passing 实例、转换端点、线程安全轮询，并按所选地址创建本次 sRPC 调用。

当前尚未实现 Docker/Compose 项目部署、Transactional Outbox、延迟重试、死信队列、RabbitMQ 自动重连、
文件删除和分享。本地文件丢失后的 OSS 恢复也留待后续完成。

需求主文档：`docs/Web网盘项目-Markdown/Web网盘项目.md`：

- 第 1 章对应当前第一期本地存储实现。
- 第 2 章 Docker 当前是学习和后续容器化准备，仓库里还没有项目 Dockerfile、Compose 或镜像构建流程。
- 第 3 章 OSS 保留“本地为主、OSS 容灾备份”的结构，实际上传已经迁移到独立 Worker。
- 第 4 章 RabbitMQ 已完成第一阶段生产者、持久化队列、消费者和手动确认链路。
- 第 5 章已完成第一版固定地址微服务：网关调用用户和文件两个 sRPC 服务。
- 第 6 章 Consul 已形成完整开发闭环：注册、TCP 健康检查、注销、passing 实例查询、轮询和网关动态调用。

## 2. 当前 Git 与工作区状态

- 当前分支：`main`。
- 第四期基线提交：`9463a5a Implement sRPC microservice architecture`。
- RabbitMQ 异步 OSS 备份第一阶段、项目内 sRPC 0.10.4、第四期微服务和第五期 Consul 完整链路均已纳入本地
  `main`。截至本次更新，第五期提交尚未推送到 `origin/main`；后续仍以实时 `git status`、`git log` 和远端状态
  为准。
- OSS 配置、RabbitMQ 发布与消费、真实 Broker 冒烟、真实 Bucket 备份和 HTTP 上传链路均已完成验证。
- 未提交修改同样视为用户当前工作成果；不要覆盖、还原或重置。
- `conf/*.ini`、`log/`、`upload/`、`build/` 和 `compile_commands.json` 已在 `.gitignore` 中忽略；
  `conf/*.ini.example` 是可提交的脱敏示例。
- 四份真实 INI 可能包含数据库密码、JWT 密钥、RabbitMQ 密码或 Consul Token，不要打印、提交或写入文档。

## 3. 架构与调用方向

第四期默认运行结构：

```text
HTTP Client
  ↓
cloud_disk_api_gateway (wfrest :9527)
  ├─ UserRpcService → cloud_disk_user_service (:9601)
  │                    ↓
  │                  AuthService / UserService → UserRepository → MySQL
  └─ FileRpcService → cloud_disk_file_service (:9602)
                       ↓
                     FileService → FileRepository / FileStorage / RabbitMQ

cloud_disk_backup_worker
  ↓
RabbitMQ / FileStorage / OssBackupStorage
  ↓
本地文件系统 / OSS

cloud_disk_user_service / cloud_disk_file_service
  ↓ 启动后注册，停止前注销
Consul (:8500)
  ↓ TCP 检查 host.docker.internal:9601 / :9602
passing 健康实例

cloud_disk_api_gateway
  ↓ 每次请求查询 passing 实例
ConsulServiceDiscovery
  ↓
RoundRobinEndpointSelector
  ↓
按所选 host:port 创建 sRPC 任务
```

`GatewayApplication` 持有一个 Consul 查询器以及用户、文件服务各自的轮询选择器。发现任务先加入当前 HTTP
`SeriesWork`；发现回调选择端点并把对应 sRPC 任务追加到同一序列，保证执行顺序和请求对象生命周期一致。

旧的 `cloud_disk_server`、`Application` 和单体 HTTP Handler 已删除；HTTP 请求只通过 API 网关进入系统。

各目录职责：

- `src/config`：复用公共配置段类型，并由四种进程级配置分别加载、严格校验自己的 INI 字段。
- `src/common`：`Result<T>`、`Result<void>` 和 `AppError`。
- `src/database`：封装 Workflow MySQL 任务创建、连接 URL 和 SQL 字符串转义。
- `src/discovery`：Consul 服务注册与注销、passing 实例查询、端点转换和简单轮询选择。
- `src/http`：网关使用的认证中间件和统一 API 响应。
- `src/gateway`：HTTP API 网关、REST 到 sRPC 的协议转换、Consul 动态端点选择和进程入口。
- `src/log`：同步 spdlog 初始化、关闭和 `LOG_*` 宏。
- `src/messaging`：备份任务 JSON 契约、发布接口和 RabbitMQ 发布器。
- `src/model`：`User`、`FileInfo`、`AuthContext` 等跨层数据模型。
- `src/repository`：生成 SQL、解析 MySQL 结果、把底层错误转换为 `Result`。
- `src/rpc`：用户和文件 sRPC 服务端实现。
- `src/security`：PBKDF2 密码哈希、JWT、SHA-256。
- `src/service`：注册、登录、用户查询和文件业务规则。
- `src/storage`：具体的本地内容寻址主存储、备份接口和 OSS 备份实现。
- `src/user_service`、`src/file_service`：两个 RPC 服务的进程入口和依赖组装。
- `src/worker`：`cloud_disk_backup_worker` 进程入口、RabbitMQ 消费循环、消息校验、OSS 上传和手动确认。
- `tests`：核心组件测试，不依赖真实 MySQL。

四个正式进程分别在各自的 `main()` 中组装依赖：

```text
Config
  ├─ API Gateway
  │   ├─ JwtService / AuthMiddleware
  │   ├─ ConsulServiceDiscovery
  │   ├─ User RoundRobinEndpointSelector
  │   └─ File RoundRobinEndpointSelector
  ├─ User RPC Service
  │   ├─ MySqlClient / UserRepository
  │   ├─ PasswordHasher / JwtService
  │   ├─ AuthService / UserService
  │   └─ ConsulServiceRegistrar
  └─ File RPC Service
      ├─ MySqlClient / FileRepository
      ├─ FileStorage / FileService
      ├─ RabbitMqBackupTaskPublisher（可选）
      └─ ConsulServiceRegistrar
```

独立的 `cloud_disk_backup_worker` 持有 `FileStorage`、`OssBackupStorage` 和 RabbitMQ 消费连接；Web 服务不再创建 OSS 客户端或持有 OSS 凭据。

## 4. 启动与停止

各正式进程的启动命令必须写全，并传入自己的配置文件：

```bash
./build/bin/cloud_disk_user_service --config conf/user-service.ini
./build/bin/cloud_disk_file_service --config conf/file-service.ini
./build/bin/cloud_disk_api_gateway --config conf/gateway.ini
```

只执行程序名或参数数量不等于 3 会报用法错误并退出。默认 HTTP、用户 RPC、文件 RPC 端口分别是
`9527`、`9601`、`9602`。

各进程 `main()` 的共同顺序：

1. 解析 `--config <path>`。
2. 对应进程配置类型的 `load()`。
3. `Log::init()`。
4. 记录去敏后的 `Config::to_string()`。
5. 组装网关或 RPC 服务依赖。
6. 注册 `SIGINT`、`SIGTERM`。
7. 启动 HTTP 或 sRPC 服务。
8. 用户和文件 RPC 服务在监听成功后同步等待 Consul 注册完成；注册失败则停止 RPC Server 并退出。
9. 主线程在 `WFFacilities::WaitGroup{1}.wait()` 上等待。
10. 信号处理函数调用 `done()`；RPC 服务先从 Consul 注销，再停止 RPC Server 并关闭日志。

第 8、10 步只适用于两个 RPC 服务。API 网关不注册到 Consul，但处理请求时通过发现模块查询上游 RPC 服务。

`WaitGroup` 不创建线程；它只是阻塞主线程。`{1}` 表示一次 `done()` 即可解除等待。

备份 Worker 使用自己的配置文件；其 `storage.root` 和 RabbitMQ 队列必须与文件服务保持一致：

```bash
./build/bin/cloud_disk_backup_worker --config conf/backup-worker.ini
```

Worker 通过带 1 秒超时的 `BasicConsumeMessage()` 等待消息，因此能定期检查 `SIGINT`/`SIGTERM` 停止标志。第一阶段不自动重连：RabbitMQ 连接或 OSS 上传失败时进程返回失败；OSS 失败的消息保持未确认，连接关闭后由 Broker 重新入队。

## 5. HTTP API

所有 JSON 成功响应统一为：

```json
{
  "status": "success",
  "message": "...",
  "data": {}
}
```

错误响应统一为：

```json
{
  "status": "error",
  "message": "..."
}
```

当前接口：

| 方法 | 路径 | 认证 | 输入 | 成功 |
| --- | --- | --- | --- | --- |
| POST | `/api/v1/auth/register` | 否 | JSON：`username/password/confirm` | 201，用户 ID 和用户名 |
| POST | `/api/v1/auth/login` | 否 | JSON：`username/password` | 200，Bearer JWT 和用户信息 |
| GET | `/api/v1/user/me` | Bearer | 无 | 200，当前用户信息 |
| GET | `/api/v1/files` | Bearer | 无 | 200，当前用户文件数组 |
| POST | `/api/v1/files` | Bearer | `multipart/form-data`，字段名 `file` | 201，文件 ID 和文件名 |
| GET | `/api/v1/file/{id}` | Bearer | 正整数文件 ID | 200，文件下载 |

此外还提供 `/` 和 `/static` 静态资源。

前端上传格式已经确认：

- `www/index.html` 的文件输入框带 `multiple`，用户可以一次选择多个文件。
- `www/static/api.js` 为每个文件创建 `FormData`，以字段名 `file` 发送一个 `multipart/form-data` HTTP 请求；浏览器负责生成 boundary。
- 当前前端用 `for...of` 配合 `await` 逐个上传，因此下一请求会等上一请求收到响应后再发出。后端对一般并发请求仍不保证“先收到就先入库”。
- wfrest 解析后将表单字段表示为键值映射；文件字段的值包含原始文件名和文件内容。

`GatewayApplication` 使用 nlohmann/json 的无异常解析：

```cpp
nlohmann::json::parse(request->body(), nullptr, false)
```

非法 JSON 返回 discarded 状态并映射为 400，不使用异常作为预期输入校验流程。

认证要求 `Authorization: Bearer <token>`，Token 内不允许空白字符。

## 6. Result 与状态码约定

`common::Result<T>` 使用 `std::variant<T, AppError>` 表示成功值或错误；`Result<void>` 使用 `std::optional<AppError>`。二者都提供：

- `ok()`。
- `explicit operator bool()`。
- `error()`。
- 成功值访问接口（仅 `Result<T>`）。

第一期 `AppError::status_code` 直接使用 HTTP 状态码，底层模块也沿用这一约定：

- 400：请求参数或格式错误。
- 401：身份认证失败。
- 404：用户或文件不存在。
- 409：用户名或同一用户下的文件名冲突。
- 413：上传内容超过限制。
- 500：数据库、文件系统、配置、密码算法等服务器内部错误。

这是第一期的简化设计。以后若分层要求提高，再考虑业务错误枚举并在 HTTP 层映射，不要现在过度设计。

几个常用组合类型的含义：

- `Result<std::optional<User>>`：先区分数据库查询是否成功，再区分成功查询后是否找到用户。登录时“用户名不存在”和“密码错误”都返回 401，避免泄露用户名是否存在。
- `Result<std::optional<FileInfo>>`：区分数据库失败、未找到当前用户可下载的文件、找到文件；未找到或文件不属于当前用户都映射为 404。
- `Result<bool>`（文件存储）：错误表示存储失败；成功值 `true` 表示新写入，`false` 表示内容已存在并完成去重，并不是操作失败。

## 7. 异步任务与线程模型

项目没有调用 `WORKFLOW_library_init()`，因此使用 Workflow 默认线程设置：

- Poller：4 个，负责 HTTP/MySQL 非阻塞网络 I/O、accept/read/write 和事件通知。
- Handler：20 个，负责路由、轻量 Handler、Workflow 完成回调和 MySQL 完成回调。
- Compute：默认等于在线 CPU 数，负责 CPU 密集或同步阻塞工作。
- DNS：默认 4 个，按需创建。
- 主线程：1 个，负责启动和等待退出。

Workflow 根据 `fd % poller_threads` 把 socket 注册到某个 Poller。监听 socket 通常只有一个，因此 accept 由监听 fd 所属的 Poller 执行；已连接 socket 再根据自身 fd 分散到多个 Poller。

路由线程安排：

- 注册和上传通过 `server_.POST(path, 0, handler)` 进入 Compute 队列，因为密码哈希、文件哈希和同步写盘较重。
- 其他路由先在 Handler 线程执行。
- 登录先异步查 MySQL；查询回调在 Handler 线程中执行，再用 `response->Compute(0, ...)` 把 PBKDF2 验证转移到 Compute。
- 两处参数 `0` 都是 wfrest 的计算队列 ID，通常对应同一个名为 `wfrest0` 的队列；`POST(..., 0, handler)` 调度整个路由 Handler，`Compute(0, lambda)` 只调度指定 Lambda，并不表示“第 0 个线程”。

MySQL 调用链：

```text
当前业务线程创建 WFMySQLTask
  ↓
TaskScheduler::add_task(task) 加入当前 RPC 请求序列
  ↓
Poller 执行 MySQL 网络收发（SQL 真正在 MySQL 服务端执行）
  ↓
当前 RPC 序列执行 WFMySQLTask 完成回调
  ↓
Repository 将原始任务转换为 Result
  ↓
Repository/Service 回调在同一调用栈继续生成响应
```

`create_mysql_task()` 只创建任务并保存回调，不会立即调用回调。任务必须被加入 Workflow 序列或显式
`task->start()` 后才会调度。`common::TaskScheduler` 把 Service 与具体请求上下文解耦：RPC 实现通过
`RPCContext::get_series()` 将数据库或计算任务追加到当前 RPC 序列，业务规则和 Repository 无需了解 sRPC。

登录流程可能经历多次线程角色切换：RPC 服务创建 MySQL 任务 → Poller 负责网络事件 → 查询回调继续业务流程 →
Compute 验证 PBKDF2；若需要升级密码哈希，还会再追加更新数据库任务。SQL 本身由 MySQL 服务端执行。

`success()`/`error()` 只设置 `HttpResp`；Workflow 会在当前请求任务序列完成后发送最终响应。

文件元数据插入成功后，MySQL 完成回调通过 `response->Compute(0, ...)` 把阻塞式 RabbitMQ 发布移到 Compute 队列。发布器内部用互斥锁串行访问单个 SimpleAmqpClient Channel，并等待 Broker 的 publisher confirm。

## 8. 数据库

初始化脚本：`sql/001_init.sql`，面向 MySQL 8.0。

### tbl_user

- `id`：无符号自增主键。
- `username`：最多 64 字符，唯一键 `uk_user_username`。
- `password_hash`：保存编码后的密码哈希，不保存明文密码。
- 毫秒精度创建和更新时间。

### tbl_file

- `id`：无符号自增主键。
- `uid`：所属用户，外键引用 `tbl_user.id`。
- `filename`：用户看到的原始文件名。
- `hashcode`：64 字符 SHA-256 十六进制摘要。
- `size`：文件大小。
- 唯一键 `(uid, filename)`：同一用户不能有两个同名文件。
- `hashcode` 有普通索引，为后续秒传/去重扩展保留。

MySQL 的主键和唯一键都会建立索引：`id` 是主键索引，`username` 是唯一二级索引；文件表的 `(uid, filename)` 也是联合唯一索引。

数据库不保存本地文件路径。实际路径统一由：

```text
storage.root / hashcode
```

计算。这样修改上传目录或迁移工作目录时，不需要批量更新数据库路径。

Repository 通过 `MySqlClient::escape()` 包装 Workflow 的 `protocol::MySQLUtil::escape_string()`。它只返回转义内容，不添加 SQL 外层单引号。MySQL 连接 URL 格式为：

```text
mysql://encoded-user:encoded-password@host:port/encoded-database
```

该 URL 含凭据，禁止写入日志。

MySQL 错误处理先区分 Workflow 传输失败与 MySQL ERR Packet。用户插入时只有错误码 1062 映射为 409；其他详细数据库错误写日志，对客户端统一返回 500，避免泄露内部结构。

## 9. 文件存储与一致性

`FileStorage` 是本地主存储的具体实现，负责基于内容哈希的文件去重、临时文件写入和原子发布。项目确定主存储只使用本地磁盘，容灾备份也明确只使用 OSS，因此两者都采用具体类，不为其他存储类型保留抽象接口。

上传流程：

1. 校验文件大小和文件名。
2. 计算内容 SHA-256。
3. 如果 `{root}/{hashcode}` 已存在则跳过重复写入。
4. 否则先写 `{root}/.tmp/<hash>.<random>`。
5. 完整写入后通过 rename 原子发布到 `{root}/{hashcode}`。
6. 插入 `tbl_file` 元数据。
7. `[backup].enabled` 开启时，向 RabbitMQ 持久化队列发布只包含版本、哈希和大小的备份任务。
8. Worker 从同一台机器的 `storage.root / hashcode` 读取内容，上传 `key_prefix + hashcode`，成功后手动 ACK。

本地存储先于数据库：数据库失败可能留下无引用文件，但不会产生指向缺失内容的数据库记录；并发上传相同内容时，rename 冲突会按去重成功处理。RabbitMQ 发布失败不会撤销本地上传。第一阶段尚无 Outbox，因此“数据库成功、消息发布失败”时任务可能丢失，这是后续可靠性阶段需要补齐的边界。

当前项目没有实现无引用文件清理器。以后可由定时维护任务扫描存储目录，与数据库中的去重哈希集合比较，并在等待宽限期和二次确认后清理；`.tmp` 中断上传残留应按另一套过期规则处理。

当前已经具备内容级去重基础，后续“秒传”可以复用 `hashcode`，但第一期没有单独的秒传 API。

## 10. 安全设计

- 密码算法：PBKDF2-HMAC-SHA256。
- 默认迭代次数：600000，配置下限也是 600000。
- Salt：16 字节随机数。
- Digest：32 字节。
- 数据库存储格式：`pbkdf2-sha256$iterations$base64(salt)$base64(hash)`。
- 密码验证使用 `CRYPTO_memcmp()` 恒定时间比较。
- 登录成功后，如果数据库中的迭代次数低于当前配置，会自动重新哈希并更新。
- JWT 使用 HS256，校验 issuer、subject、过期时间；用户 ID 以字符串 claim `uid` 保存。
- 数据库主键使用 `uint64_t` 是为了匹配 MySQL `BIGINT UNSIGNED`；JWT claim 改存字符串，是为了绕开 jwt-cpp 默认 JSON 只有有符号 64 位整数的边界，同时保留完整取值范围。
- JWT 密钥至少 32 字符。
- `AuthMiddleware` 是项目自有的认证辅助类，由受保护 Handler 手动调用，不是 wfrest 自动注册的全局中间件。它解析 Bearer Token、调用 `JwtService::verify()` 并返回用户 ID。
- 下载查询把 `user_id` 和 `file_id` 同时写进 SQL 条件，避免跨用户访问。
- 下载文件名使用 `filename*=UTF-8''...` 编码，避免响应头注入。
- 配置日志不输出数据库用户名、密码和 JWT 密钥，只输出是否已配置。

密码哈希中的 `iterations` 是 PBKDF2 对同一密码执行伪随机函数的轮数，不是登录次数。校验旧密码必须使用数据库编码串中的迭代次数；`needs_rehash()` 再将其与当前配置比较。编码串中的摘要部分解码后是校验时的 `expected` 值。

## 11. 配置与日志

四份示例分别是 `conf/gateway.ini.example`、`conf/user-service.ini.example`、
`conf/file-service.ini.example` 和 `conf/backup-worker.ini.example`。对应的真实 `.ini` 文件敏感且被忽略。

进程配置边界：

- `GatewayConfig`：HTTP server、JWT 验证、上传请求大小、RPC 超时、Consul 服务发现和日志。
- `UserServiceConfig`：MySQL、密码哈希与 JWT 签发、用户 RPC 监听、Consul 注册和日志。
- `FileServiceConfig`：MySQL、本地存储、由 `[backup].enabled` 控制的 RabbitMQ 发布、文件 RPC 监听、Consul 注册和日志；
  开关默认开启，但不加载 OSS Region、Bucket 或凭据。
- `BackupWorkerConfig`：本地存储、RabbitMQ 消费、OSS 和日志；启动 Worker 本身即表示启用备份，不再读取额外开关，
  也不加载 MySQL、JWT、RPC 或 Consul。

每份 `[log]` 都只含当前进程独占的 `file`，不再在一个配置中维护四个日志路径。网关与用户服务的 JWT 密钥及签发者、
网关与文件服务的上传大小、文件服务与 Worker 的 `storage.root` 和 RabbitMQ 队列必须成对保持一致。Docker Desktop
内的 Consul 默认通过 `host.docker.internal` 检查 macOS 宿主机 RPC 端口。

项目约定从项目根目录启动；所有相对路径相对于进程当前工作目录，而不是配置文件目录。

日志是同步 spdlog，不使用项目级单例。`Log::init()` 创建 logger 后通过 `spdlog::set_default_logger()` 设置 spdlog 默认 logger。业务代码统一使用：

```cpp
LOG_TRACE(...)
LOG_DEBUG(...)
LOG_INFO(...)
LOG_WARN(...)
LOG_ERROR(...)
LOG_CRITICAL(...)
```

日志格式包含时间、级别、线程 ID、源码文件和行号。支持控制台和滚动文件 sink，错误级别立即 flush。

## 12. 依赖与构建

第三方源码位于 `third_party/<library>`：

- inih：头文件使用。
- nlohmann/json 3.12.0：头文件使用，当前实际目录是 `third_party/nlohmann_json/include`。
- jwt-cpp 0.7.2：头文件使用。
- spdlog 1.17.0：由主项目 CMake 通过 `add_subdirectory()` 编译为静态库。
- Workflow 1.0.1：静态库 `third_party/workflow/_lib/libworkflow.a`。
- Consul 客户端复用 Workflow 的 `WFConsulClient`，没有新增 ppconsul 依赖。
- wfrest 0.9.9：静态库 `third_party/wfrest/_lib/libwfrest.a`。
- 阿里云 OSS C++ SDK V2：由主项目 CMake 通过 `add_subdirectory()` 编译。
- rabbitmq-c 0.18.0：由主项目 CMake 编译为静态库。
- SimpleAmqpClient 2.6.0：由主项目 CMake 编译为静态库，并链接同一构建树中的 rabbitmq-c target。
- sRPC 0.10.4：源码位于 `third_party/srpc`，使用项目内 `libsrpc.a` 和 `srpc_generator`，不安装到系统。
- Protobuf 35.1、Snappy 1.2.2、LZ4 1.10.0：由 macOS Homebrew 提供；CMake 从当前 `protoc` 前缀查找。
- 系统依赖：OpenSSL、zlib、Threads、Boost chrono。

标准构建命令：

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

如果移动了项目路径，必须删除或换用新的 build 目录重新配置；不要复用记录旧绝对路径的 CMakeCache。

需要提前编译的第三方库顺序为：Workflow → wfrest → sRPC。wfrest 配置时需要：

```bash
-DWorkflow_DIR="${PROJECT_ROOT}/third_party/workflow"
```

macOS 上 sRPC 只构建 `srpc-static` 和 `srpc_generator`，并使用 C++17 兼容 Homebrew Protobuf；详细说明见
`docs/第三方库编译与迁移指南.md`。

## 13. 代码风格与既定偏好

- 当前命名空间以实际代码为准：`webdisk::<module>`；`main()` 和文件内辅助符号可留在全局/匿名命名空间。
- 项目自有构造函数的成员初始化列表已经统一为花括号直接列表初始化；不要机械修改 `third_party/`。花括号可防止窄化，但仍需留意 `initializer_list` 重载。
- 当前重构倾向在实现文件中使用完整命名空间限定，避免局部 `using namespace` 或类型导入；遇到 nlohmann JSON 时要按真实构造重载判断，空数组优先写 `auto files = nlohmann::json::array();`，不要写可能生成 `[[]]` 的嵌套花括号形式。
- 代码标识符、日志和对外消息使用英文；注释可以使用中文。
- 对复杂控制流、异步顺序、安全边界、存储一致性和重要假设写简洁注释；不要给显而易见代码添加逐行解释。
- 对话中已经为多个关键函数和条件补充就地注释；“为项目自身 `src/` 下每个 `class` 顶部添加一句简短职责注释”尚未全量完成，如继续做必须先扫描现状且不要改第三方库。
- 预期失败使用 `Result`/普通分支，不依赖异常控制流程。第三方库真正抛出的异常可以在边界捕获并转换。
- `GatewayApplication`、RPC 服务实现和 `Log` 都不是项目自建单例；依赖和生命周期由各进程对象组合管理。
- 保留 `std::move(callback)` 以转移异步回调所有权。捕获列表中的 `callback = std::move(callback)` 本身不要求 `mutable`；只有 Lambda 函数体内需要修改或再次移动按值捕获对象时才需要。当前仅 `AuthService.cc` 保留 3 处必要的 `mutable`。
- 不要把敏感配置、MySQL URL、密码哈希原始材料或 JWT 密钥写入日志。

## 14. 当前已验证状态与待处理事项

2026-08-13 对当前工作区验证：

- Debug 配置和完整构建成功，rabbitmq-c 与 SimpleAmqpClient 均生成静态库。
- spdlog 由主项目编译到 `build/third_party/spdlog/`，Debug 产物为 `libspdlogd.a`，Release 产物为 `libspdlog.a`，不再依赖源码目录中的预编译产物。
- `ctest --test-dir build --output-on-failure` 通过，共 `3/3` 个测试：核心组件、RabbitMQ 客户端和 RPC 协议测试。
- 安装目标只包含 `cloud_disk_api_gateway`、`cloud_disk_user_service`、`cloud_disk_file_service` 和备份 Worker。
- 子项目没有覆盖主项目的 Debug 构建类型或全局 `CMAKE_CXX_FLAGS`。
- 新 Web 服务已连接本机 RabbitMQ 4.x，成功声明 `webdisk.oss.backup.v1`：durable=true、auto_delete=false、exclusive=false，并完成启动和 `Ctrl+C` 停止验证。
- `cloud_disk_rabbitmq_smoke_test` 已通过真实 Broker 验证正式发布器、持久化消息、消费和手动 ACK；它直接消费并校验消息，不访问 OSS，成功后删除独立临时队列。正式 Worker 由真实 OSS 端到端验收覆盖。
- 经用户明确授权，`cloud_disk_rabbitmq_oss_smoke_producer` 已把固定测试内容写入本地主存储并向业务队列发布任务；正式 Worker 成功上传当前配置的真实 OSS Bucket，随后手动 ACK，队列恢复为零待处理、零未确认消息，并完成 `Ctrl+C` 正常停止。授权约定要求本地文件和 OSS 对象作为测试备份保留。
- 用户 RPC 服务、文件 RPC 服务和 API 网关已在 macOS 本机分别监听 `9601`、`9602`、`9527`；手动 RPC 冒烟同时连通两个服务，HTTP 空注册请求经网关和用户 RPC 返回统一 400 JSON，三个进程均通过 `Ctrl+C` 正常停止。
- 用户已手动启动 API 网关、用户 RPC 服务、文件 RPC 服务和备份 Worker 四个正式进程，完成注册、登录、上传和下载，并确认 OSS 中的备份内容正确。
- macOS Docker 单节点 Consul 2.0.3 已启动，`docker exec consul consul members` 和
  `http://localhost:8500` UI 可正常访问。
- 用户和文件 RPC 服务已实现“监听成功后注册、停止前注销”，注册时配置初始 `critical` 状态和 TCP 健康检查；
  Consul 发现查询只请求 `passing` 实例。
- `cloud_disk_core_tests` 已覆盖发现结果中的无效地址过滤、按实例 ID 稳定排序、`A → B → A` 轮询及空实例错误；
  完整构建、`3/3` CTest、`git diff --check` 和根目录 `bin/` 安装验证均已通过。
- 独立测试服务名下的两个用户 RPC 实例分别监听 `19601`、`19611`，Consul TCP 检查均进入 passing；网关连续
  请求依次选择两个实例。注销 `19601` 后 passing 列表只剩 `19611`，后续请求仍成功到达剩余实例；全部注销后
  网关返回 HTTP 503。临时服务、Consul 注册和测试配置均已清理。
- 当前文件 RPC 使用 Protobuf `bytes` 传输文件，HTTP 和 RPC 请求大小均受配置约束；同机开发可用，但会产生额外序列化与内存复制，跨机器扩容时应改为流式或对象存储直传。

第二阶段：消费者可靠性

1. 增加延迟重试、最大重试次数和 DLQ，替换当前 OSS 失败后保留消息未确认并退出 Worker 的第一阶段行为。
2. 增加 RabbitMQ 断线重连和退避，避免短暂断线直接终止 Worker。
3. 增加运行指标，并增加覆盖重连、重试和 DLQ 的自动化真实 Broker 集成测试。

第三阶段：生产者可靠性

1. 增加 Transactional Outbox，消除数据库成功但消息发布失败的任务丢失窗口。
2. 增加 Outbox 投递与投递结果记录，支持 Broker 恢复后继续投递。
3. 增加补偿清理和集成测试，覆盖数据库已提交、Broker 不可用以及恢复后补发的完整边界。

不纳入上述两个 RabbitMQ 可靠性阶段的后续事项：实现本地文件缺失时从 OSS 恢复的容灾闭环。

macOS 默认大小写不敏感文件系统上，Workflow 源码中的 `BUILD` 文件会与 `build` 目录冲突；重建第三方 Workflow 时使用 `third_party/workflow/build-macos` 作为构建目录。

## 15. 建议下次 Codex 的开始顺序

1. 先阅读本文件、`README.md`、需求主文档，再查看真实的 `git status`、最近提交和当前 `git diff`；不要只相信本文件的时间点快照。
2. RabbitMQ 第二、第三阶段按用户要求暂停；当前继续使用第一阶段，不要主动开始重试、DLQ、重连或 Outbox。
3. 第五期 Consul 开发闭环和多实例故障切换验证已经完成；继续修改前先检查当前未提交差异，避免破坏注册、发现与
   HTTP SeriesWork 中的异步执行顺序。
4. 第五期已纳入本地 `main` 但尚未推送；继续工作时保留任何新增的未提交成果，只修改任务明确涉及的文件。
5. 提交前运行 `git diff --check`、`cmake --build build --parallel` 和 `ctest --test-dir build --output-on-failure`。

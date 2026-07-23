# WebCloudDisk 项目上下文

> 更新时间：2026-07-23  
> 当前项目路径：`/home/lichao/projects/WebCloudDisk`  
> 目标：让新的 Codex 对话先阅读本文件，再基于当前 checkout 继续第一期开发。

## 1. 当前阶段与范围

WebCloudDisk 是一个 C++17 Web 网盘项目。当前正在实现第一期单体后端，使用 wfrest、Workflow、MySQL 和本地文件系统，已经覆盖：

- 用户注册与登录。
- JWT 访问令牌。
- 获取当前用户信息。
- 查询当前用户文件列表。
- 本地文件上传和下载。
- INI 配置加载与校验。
- 统一 JSON API 响应、错误传播和日志。

第一期明确不包含 Docker、RabbitMQ、OSS、微服务和删除文件接口。除非用户主动扩大范围，否则不要提前实现这些内容。

需求主文档：`docs/Web网盘项目-Markdown/Web网盘项目.md`。第一期对应第 1 章；后面的 Docker 部分当前不处理。

## 2. 当前 Git 与工作区状态

- 当前分支：`main`。
- 当前提交：`283034a Implement phase-one WebCloudDisk service`。
- 当前存在用户未提交修改，必须保留：
  - `src/config/Config.h`：给 `Config` 增加了类说明注释。
  - `src/main.cc`：把 `WaitGroup` 和 `Application` 改成花括号直接初始化。
- 不要覆盖、还原或重置用户修改。
- `conf/server.ini`、`log/`、`upload/`、`build/` 和 `compile_commands.json` 已在 `.gitignore` 中忽略。
- `conf/server.ini` 含数据库密码和 JWT 密钥，不要打印、提交或写入文档。

## 3. 架构与调用方向

项目采用分层单体结构：

```text
main
  ↓
Application
  ↓
HTTP Handler / Middleware
  ↓
Service
  ↓
Repository / FileStorage
  ↓
MySqlClient / 本地文件系统
```

各目录职责：

- `src/app`：`Application` 组装组件、保证依赖生命周期、注册路由并启动/停止服务器；不是单例。
- `src/config`：通过 inih 加载、严格校验 INI，并解析相对于进程工作目录的路径。
- `src/common`：`Result<T>`、`Result<void>` 和 `AppError`。
- `src/database`：封装 Workflow MySQL 任务创建、连接 URL 和 SQL 字符串转义。
- `src/http`：认证中间件、请求解析、Handler 和统一 API 响应。
- `src/log`：同步 spdlog 初始化、关闭和 `LOG_*` 宏。
- `src/model`：`User`、`FileInfo`、`AuthContext` 等跨层数据模型。
- `src/repository`：生成 SQL、解析 MySQL 结果、把底层错误转换为 `Result`。
- `src/security`：PBKDF2 密码哈希、JWT、SHA-256。
- `src/service`：注册、登录、用户查询和文件业务规则。
- `src/storage`：文件存储接口和本地内容寻址实现。
- `tests`：核心组件测试，不依赖真实 MySQL。

关键依赖关系由 `Application` 成员声明顺序和构造函数初始化顺序保证：

```text
Config
  ├─ MySqlClient
  │   ├─ UserRepository
  │   └─ FileRepository
  ├─ PasswordHasher
  ├─ JwtService
  └─ LocalFileStorage
       ↓
Service
  ↓
Middleware / Handler
  ↓
wfrest::HttpServer
```

## 4. 启动与停止

启动命令必须写全：

```bash
./build/bin/cloud_disk_server --config conf/server.ini
```

只执行 `./cloud_disk_server` 或参数数量不等于 3 会报用法错误并退出。

`main()` 的顺序：

1. 解析 `--config <path>`。
2. `Config::load()`。
3. `Log::init()`。
4. 记录去敏后的 `Config::to_string()`。
5. 创建 `Application` 并执行 `init()`。
6. 注册 `SIGINT`、`SIGTERM`。
7. 启动 HTTP 服务。
8. 主线程在 `WFFacilities::WaitGroup{1}.wait()` 上等待。
9. 信号处理函数调用 `done()`，主线程继续执行 `Application::stop()` 和日志关闭。

`WaitGroup` 不创建线程；它只是阻塞主线程。`{1}` 表示一次 `done()` 即可解除等待。

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

`AuthHandler` 使用 nlohmann/json 的无异常解析：

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

MySQL 调用链：

```text
当前业务线程创建 WFMySQLTask
  ↓
response->add_task(task) 加入当前 HTTP 请求序列
  ↓
Poller 执行 MySQL 网络收发（SQL 真正在 MySQL 服务端执行）
  ↓
Handler 执行 WFMySQLTask 完成回调
  ↓
Repository 将原始任务转换为 Result
  ↓
Service/Handler 回调在同一调用栈继续生成响应
```

`create_mysql_task()` 只创建任务并保存回调，不会立即调用回调。任务必须被 `response->add_task(task)` 或 `task->start()` 调度。

`success()`/`error()` 只设置 `HttpResp`；Workflow 会在当前请求任务序列完成后发送最终响应。

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

`FileStorage` 是抽象接口；第一期实现为 `LocalFileStorage`。

上传流程：

1. 校验文件大小和文件名。
2. 计算内容 SHA-256。
3. 如果 `{root}/{hashcode}` 已存在则跳过重复写入。
4. 否则先写 `{root}/.tmp/<hash>.<random>`。
5. 完整写入后通过 rename 原子发布到 `{root}/{hashcode}`。
6. 再插入 `tbl_file` 元数据。

存储先于数据库：数据库失败可能留下无引用文件，但不会产生指向缺失内容的数据库记录；无引用文件以后可以安全清理。并发上传相同内容时，rename 冲突会按去重成功处理。

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
- JWT 密钥至少 32 字符。
- 下载查询把 `user_id` 和 `file_id` 同时写进 SQL 条件，避免跨用户访问。
- 下载文件名使用 `filename*=UTF-8''...` 编码，避免响应头注入。
- 配置日志不输出数据库用户名、密码和 JWT 密钥，只输出是否已配置。

## 11. 配置与日志

示例配置：`conf/server.ini.example`。真实配置：`conf/server.ini`（敏感且被忽略）。

配置分组：

- `[server]`：`port`、`web_root`。
- `[database]`：host、port、username、password、database、retry_max。
- `[auth]`：jwt_secret、jwt_issuer、token_ttl_seconds、password_iterations。
- `[storage]`：root、max_file_size_bytes。
- `[log]`：level、console、file、roll_size、roll_files。

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
- spdlog 1.17.0：静态库 `third_party/spdlog/build/libspdlog.a`。
- Workflow 1.0.1：静态库 `third_party/workflow/_lib/libworkflow.a`。
- wfrest 0.9.9：静态库 `third_party/wfrest/_lib/libwfrest.a`。
- 系统依赖：OpenSSL、zlib、Threads。

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

第三方库重建顺序：spdlog → Workflow → wfrest。wfrest 配置时需要：

```bash
-DWorkflow_DIR="${PROJECT_ROOT}/third_party/workflow"
```

详细说明见 `docs/第三方库编译与迁移指南.md`。

## 13. 代码风格与既定偏好

- 当前命名空间以实际代码为准：`webdisk::<module>`；`main()` 和文件内辅助符号可留在全局/匿名命名空间。
- 代码标识符、日志和对外消息使用英文；注释可以使用中文。
- 对复杂控制流、异步顺序、安全边界、存储一致性和重要假设写简洁注释；不要给显而易见代码添加逐行解释。
- 用户当前明确要求：为项目自身 `src/` 下每个 `class` 顶部添加一句简短职责注释，类似 `Config`；不要改第三方库。该修改尚未完成，见下一节。
- 预期失败使用 `Result`/普通分支，不依赖异常控制流程。第三方库真正抛出的异常可以在边界捕获并转换。
- `Application`、`Log` 等都不是项目自建单例；依赖和生命周期由对象组合管理。
- 保留 `std::move(callback)` 以转移异步回调所有权。只有 Lambda 内需要修改或再次移动捕获值时才保留 `mutable`。
- 不要把敏感配置、MySQL URL、密码哈希原始材料或 JWT 密钥写入日志。

## 14. 当前已验证状态与待处理事项

2026-07-23 使用当前源码在新的 `/tmp` 构建目录实际执行 CMake。配置成功，但构建失败，首先需要处理：

1. `src/service/AuthService.cc` 缺少 `model::User` 限定：
   - 第 63 行使用了 `std::optional<User>`。
   - 第 85 行 `finish_login(const User& ...)`。
   - 当前文件没有 `using model::User;`。
   - 可添加 `using model::User;`，或把两处改为 `model::User`，然后重新构建。
2. 修复上项后，`tests/core_tests.cc` 预计还会失败：测试调用 `storage.initialize()`，当前类接口是 `storage.init()`。应把测试改成 `init()`。
3. 用户最新未完成请求：为所有项目类顶部添加一句简短职责注释。之前因 Codex 工作区写权限仍绑定已删除的旧路径 `/home/lichao/lirui/WebCloudDisk`，补丁没有写入。新会话应在当前路径重新执行。
4. `docs/第三方库编译与迁移指南.md` 仍有旧名称：多处写 `third_party/json` 和 `src/config/AppConfig.cc`；当前实际是 `third_party/nlohmann_json` 和 `src/config/Config.cc`，后续应同步文档。
5. 若继续清理 Lambda，可删除只调用 `std::function`、没有修改/移动捕获值的多余 `mutable`；需要 `std::move` 捕获成员的 Lambda 必须保留。

当前没有完成一次全量构建或测试通过验证。下一次修改后必须执行标准构建和 `ctest`，不要只做静态判断。

## 15. 建议下次 Codex 的开始顺序

1. 先阅读本文件、`README.md`、第一期需求和当前 `git diff`。
2. 保留 `Config.h`、`main.cc` 的用户修改。
3. 先修复 `AuthService.cc` 的 `User` 命名空间编译错误。
4. 修复测试中的 `initialize()` → `init()`。
5. 完成所有项目类顶部的一句职责注释。
6. 审核并移除真正多余的 `mutable`，不要机械删除需要移动捕获值的情况。
7. 在新 build 目录重新配置、完整编译并运行 `ctest`。
8. 最后同步第三方迁移文档里的旧目录/类名。


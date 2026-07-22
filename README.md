# WebCloudDisk

第一期后端是基于 wfrest、Workflow 和 MySQL 的 C++17 单体服务，支持注册、登录、当前用户、文件列表、本地上传和下载。
Docker、RabbitMQ 和 OSS 不参与第一期构建。

## 准备数据库

在目标数据库中执行：

```bash
mysql -u root -p CloudDisk < sql/001_init.sql
```

初始化脚本面向 MySQL 8.0；如果使用 MySQL 5.7，需要调整 `utf8mb4_0900_ai_ci` 排序规则。

## 配置

```bash
cp conf/server.ini.example conf/server.ini
```

修改 `conf/server.ini` 中的数据库账号、数据库密码和 JWT 密钥。项目约定从项目根目录启动程序，配置中的相对路径以启动时的工作目录为基准。

## 构建和测试

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## 启动

```bash
./build/bin/cloud_disk_server --config conf/server.ini
```

默认监听端口为 `9527`。接口定义见 `docs/Web网盘项目-Markdown/Web网盘项目.md`。

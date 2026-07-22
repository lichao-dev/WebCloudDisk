# Web 网盘项目

---

# 1 第一期：实现基本功能

这个项目总共分为5 期，每一期我们都会逐步加入一些新的功能。

## 1.1 数据库表的设计

这个项目只会涉及到两张表，分别为tbl_user 和tbl_file。tbl_user 表的结构如下：

**tbl_user 表的建表语句：**

```sql
CREATE TABLE tbl_user (
    id            BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    username      VARCHAR(64) NOT NULL,
    password_hash VARCHAR(255) NOT NULL,
    created_at    DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    updated_at    DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
                               ON UPDATE CURRENT_TIMESTAMP(3),

    PRIMARY KEY (id),
    UNIQUE KEY uk_user_username (username)
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_0900_ai_ci;
```



**tbl_file 表的建表语句：**

```sql
CREATE TABLE tbl_file (
    id          BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    uid         BIGINT UNSIGNED NOT NULL,
    filename    VARCHAR(255) NOT NULL,
    hashcode    CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    size        BIGINT UNSIGNED NOT NULL,
    created_at  DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    updated_at  DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
                             ON UPDATE CURRENT_TIMESTAMP(3),

    PRIMARY KEY (id),
    UNIQUE KEY uk_file_user_filename (uid, filename),
    KEY idx_file_user_created (uid, created_at),
    KEY idx_file_hashcode (hashcode),

    CONSTRAINT fk_file_user
        FOREIGN KEY (uid) REFERENCES tbl_user(id)
        ON UPDATE RESTRICT
        ON DELETE RESTRICT
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_0900_ai_ci;
```

## 1.2 注册

1. API 端点：POST /api/v1/auth/register。

2. 请求:

```bash
POST /api/v1/auth/register HTTP/1.1
Content-Type: application/json
// 省略其它头部字段
{
"username": "peanutixx",
"password": "1234",
"confirm": "1234"
}
```

3. 响应：

成功返回响应状态码201，并返回下面格式的JSON 数据。

```bash
HTTP/1.1 201 Created
Content-Type: application/json
// 省略其它头部字段
{
"status": "success",
"message": "注册成功",
"data": {
"userId": 1,
"username": "peanutixx"
}
}
```

失败，返回下面格式的JSON 数据，并设置对应的响应状态码：

```json
{
"status": "error",
"message": <具体的出错原因>
}
```



|             原因             | 响应状态码 |        message         |
| :--------------------------: | :--------: | :--------------------: |
| 请求类型不是application/json |    400     |     “请求格式有误”     |
|       用户名或密码为空       |    400     | “用户名和密码不能为空” |
|     密码和确认密码不一致     |    400     | “两次输入的密码不一致” |
|         插入记录失败         |    409     |     “用户名已存在”     |

## 1.3 登录

1. API 端点：POST /api/v1/auth/login。

2. 请求:

```bash
POST /api/v1/auth/login HTTP/1.1
Content-Type: application/json
// 省略其它头部字段
{
"username": "peanutixx",
"password": "1234"
}
```

3. 响应：

成功返回响应状态码200，并返回下面格式的JSON 数据。

```bash
HTTP/1.1 200 OK
Content-Type: application/json
// 省略其它头部字段
{
"status": "success",
"message": "登录成功",
"data": {
"accessToken": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...",
"tokenType": "Bearer",
"user": {
"userId": 1,
"username": "peanutixx"
}
}
}
```

失败，返回下面格式的JSON 数据，并设置对应的响应状态码：

```json
{
"status": "error",
"message": <具体的出错原因>
}
```

```text
原因
响应状态码
message
```

|             原因             | 响应状态码 |        message         |
| :--------------------------: | :--------: | :--------------------: |
| 请求类型不是application/json |    400     |     “请求格式有误”     |
|       用户名或密码为空       |    400     | “用户名和密码不能为空” |
|           密码不对           |    401     |   “用户名或密码错误”   |
|       执行SQL 语句失败       |    500     |    “内部服务器错误”    |
|       SQL 返回空结果集       |    401     |   “用户名或密码错误”   |

## 1.4 获取当前用户信息

1. API 端点：GET /api/v1/user/me。

2. 请求:

```bash
GET /api/v1/user/me HTTP/1.1
Authorization: Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...
// 省略其它头部字段
```

3. 响应：

成功返回响应状态码200，并返回下面格式的JSON 数据。

```bash
HTTP/1.1 200 OK
Content-Type: application/json
// 省略其它头部字段
{
"status": "success",
"message": "获取个人信息成功",
"data": {
"userId": 1,
"username": "peanutixx",
"createdAt": "2026-04-02 12:04:01"
}
}
```

失败，返回下面格式的JSON 数据，并设置对应的响应状态码：

```json
{
"status": "error",
"message": <具体的出错原因>
}
```

|                原因                | 响应状态码 |     message      |
| :--------------------------------: | :--------: | :--------------: |
| 没有令牌，或者令牌的类型不是Bearer |    401     | “无效的访问令牌” |
|            令牌验证失败            |    401     | “无效的访问令牌” |



## 1.5 文件列表查询

1. API 端点：GET /api/v1/files。

```bash
2承载令牌：最常见的令牌使用方式，“谁持有谁就是令牌的主人”。客户端不需要证明自己真实的身份，令牌本身就是凭证。
```

2. 请求:

```bash
GET /api/v1/files HTTP/1.1
Authorization: Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...
// 省略其它头部字段
```

3. 响应：

成功返回响应状态码200，并返回下面格式的JSON 数据。

```bash
HTTP/1.1 200 OK
Content-Type: application/json
// 省略其它头部字段
{
"status": "success",
"message": "获取文件列表成功",
"data": {
"files": [
{
"fileId": 6,
"filename": "AlgorithmicPuzzles.pdf",
"size": 1647220,
"createdAt": "2026-04-02 21:06:29",
"updatedAt": "2026-04-02 21:06:29"
},
{
"fileId": 7,
"filename": "images.jpg",
"size": 5579,
"createdAt": "2026-04-02 21:07:33",
"updatedAt": "2026-04-02 21:07:33"
}
]
}
}
```

失败，返回下面格式的JSON 数据，并设置对应的响应状态码：

```json
{
"status": "error",
"message": <具体的出错原因>
}
```

|                    原因                    | 响应状态码 |     message      |
| :----------------------------------------: | :--------: | :--------------: |
| 没有令牌，令牌类型不是Bearer，令牌校验失败 |    401     | “无效的访问令牌” |
|              SQL 语句执行失败              |    500     | “内部服务器错误” |



## 1.6 上传文件

1. API 端点：POST /api/v1/files。

2. 请求:

```bash
POST /api/v1/files HTTP/1.1
Content-Type: multipart/form-data; boundary=...
Authorization: Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...
// 省略其它头部字段
// 请求体
```

3. 响应：

成功返回响应状态码201，并返回下面格式的JSON 数据。

```bash
HTTP/1.1 201 Created
Content-Type: application/json
// 省略其它头部字段
{
"status": "success",
"message": "上传成功",
"data": {
"fileId": 22,
"filename": "a.txt"
}
}
```

失败，返回下面格式的JSON 数据，并设置对应的响应状态码：

```json
{
"status": "error",
"message": <具体的出错原因>
}
```

|                    原因                    | 响应状态码 |     message      |
| :----------------------------------------: | :--------: | :--------------: |
| 没有令牌，令牌类型不是Bearer，令牌校验失败 |    401     | “无效的访问令牌” |
|     请求体类型不为multipart/form-data      |    400     |  “请求格式有误”  |
|              SQL 语句执行失败              |    500     | “内部服务器错误” |

## 1.7 下载文件

1. API 端点：GET /api/v1/file/{id}。

2. 请求:

```bash
GET /api/v1/file/22 HTTP/1.1
Authorization: Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...
// 省略其它头部字段
```

3. 响应：

成功返回响应状态码200，并返回下面格式的JSON 数据。

```bash
HTTP/1.1 200 OK
Content-Disposition: attachment; filename=a.txt
// a.txt 要替换成具体的文件名，省略其它头部字段
// 文件内容...
```

失败，返回下面格式的JSON 数据，并设置对应的响应状态码：

```json
{
"status": "error",
"message": <具体的出错原因>
}
```

|                    原因                    | 响应状态码 |     message      |
| :----------------------------------------: | :--------: | :--------------: |
| 没有令牌，令牌类型不是Bearer，令牌校验失败 |    401     | “无效的访问令牌” |
|              请求的文件不存在              |    404     |   “文件不存在”   |
|              SQL 语句执行失败              |    500     | “内部服务器错误” |

## 1.8 服务配置

第一期后端使用 INI 文件保存服务端口、数据库、JWT、本地文件存储和日志配置。项目通过
`third_party/inih/INIReader.h` 读取配置。

开发时先复制示例配置：

```bash
cp conf/server.ini.example conf/server.ini
```

然后修改 `conf/server.ini` 中的数据库账号、数据库密码和 JWT 密钥。真实配置文件包含敏感信息，不能提交到版本库。

```ini
[server]
port = 9527
web_root = ./www

[database]
host = 127.0.0.1
port = 3306
username = cloud_disk
password = change_me
database = CloudDisk
retry_max = 3

[auth]
jwt_secret = change_me_to_a_random_secret_at_least_32_characters
jwt_issuer = web-cloud-disk
token_ttl_seconds = 3600
password_iterations = 600000

[storage]
root = ./upload
max_file_size_bytes = 104857600

[log]
level = info
console = true
file = ./log/server.log
roll_size = 100000000
roll_files = 5
```

项目约定从项目根目录启动程序，配置中的相对路径以进程启动时的工作目录为基准。因此 `./upload`、`./www` 和 `./log/server.log` 都指向项目根目录下的对应路径。

# 2 Docker

## 2.1 简介

Docker 是一种轻量级的虚拟化技术，它让应用程序及其依赖环境可以被打包成一个标准化的单元（镜像），在任何地方

都能一致地运行。

如果用一个生活中的例子类比：Docker 之于软件，就像集装箱之于货物。在集装箱发明之前，货物的运输是一件麻烦的

事情：不同的货物需要不同的包装、不同的装卸方式，换一种运输工具就要重新装卸。集装箱的出现改变了这一切：无论

里面装的是什么，集装箱的外形是标准的，可以用同样的方式装卸、堆放和运输。Docker 做的事情类似：无论你的应用

是用Python、C++、Java 还是其他语言写的，无论它需要什么样的依赖库和环境，一旦被打包成Docker 镜像，就可以

用同样的方式在任何支持Docker 的机器上运行。

Docker 的口号是：“build once, run everywhere”，一次构建，处处运行。Docker 是一项革命性的技术，它彻底改变了

开发和交付的方式。

## 2.2 核心概念

Docker 有三个最核心的概念：

- 镜像（Image）

Docker 镜像是一个特殊的文件系统，除了提供容器运行时所需的程序、库、资源、配置等文件外，还包含了一些为

运行时准备的一些配置参数。镜像不包含任何动态数据，其内容在构建之后也不会被改变。

- 容器（Container）

镜像(Image) 和容器(Container) 的关系，就像是程序和进程一样，镜像是静态的定义，容器是镜像运行时的实体。

容器可以被创建、启动、停止、删除、暂停等。

- 仓库（Repository）

镜像构建完成后，可以很容易的在当前宿主机上运行，但是，如果需要在其它服务器上使用这个镜像，我们就需要

一个集中的存储、分发镜像的服务，Docker Registry 就是这样的服务。Docker Hub 是Docker 官方的Registry，

里面存放了很多镜像的集合，镜像的集合我们称之为Repository，比如：library/ubuntu。

```bash
3健壮性：通俗地讲，就是系统的“抗打击能力”或“皮实程度”。一个健壮的程序，不是“不会出错”，而是在出错时不会崩溃或产生灾难性后果。
```

理解了这三个概念，对理解Docker 至关重要。

## 2.3 安装

本节将介绍如何在Ubuntu 系统上安装Docker，并配置国内镜像加速。在开始安装之前，我们需要确认系统版本是否满

足要求，并清理可能存在的旧版本。

1. 系统要求

目前最新的Docker 引擎，支持下面64 位版本的Ubuntu：

- Ubuntu Resolute 26.04 (LTS)

- Ubuntu Questing 25.10

- Ubuntu Noble 24.04 (LTS)

- Ubuntu Jammy 22.04 (LTS)

执行命令cat /etc/*release 查看你的Linux 系统版本信息：

```bash
peanut@wd:~$ cat /etc/*release
DISTRIB_ID=Ubuntu
DISTRIB_RELEASE=22.04
DISTRIB_CODENAME=jammy
DISTRIB_DESCRIPTION="Ubuntu 22.04.5 LTS"
PRETTY_NAME="Ubuntu 22.04.5 LTS"
NAME="Ubuntu"
VERSION_ID="22.04"
VERSION="22.04.5 LTS (Jammy Jellyfish)"
VERSION_CODENAME=jammy
ID=ubuntu
ID_LIKE=debian
HOME_URL="https://www.ubuntu.com/"
SUPPORT_URL="https://help.ubuntu.com/"
BUG_REPORT_URL="https://bugs.launchpad.net/ubuntu/"
PRIVACY_POLICY_URL="https://www.ubuntu.com/legal/terms-and-policies/privacy-policy"
UBUNTU_CODENAME=jammy
```

2. 卸载旧版本，如果有的话

```bash
sudo apt remove $(dpkg --get-selections docker.io docker-compose docker-compose-v2 docker-doc
```

![Docker 架构示意图](Web网盘项目.assets/docker-architecture.png)

```bash
podman-docker containerd runc | cut -f1)
```

3. 设置Docker 的apt 仓库

```bash
# Add Docker's official GPG key:
sudo apt update
sudo apt install ca-certificates curl
sudo install -m 0755 -d /etc/apt/keyrings
# 由于众所周知的原因，下面这条命令可能需要多执行几次才能成功
sudo curl -fsSL https://download.docker.com/linux/ubuntu/gpg -o /etc/apt/keyrings/docker.asc
sudo chmod a+r /etc/apt/keyrings/docker.asc
# Add the repository to Apt sources:
sudo tee /etc/apt/sources.list.d/docker.sources <<EOF
Types: deb
URIs: https://download.docker.com/linux/ubuntu
Suites: $(. /etc/os-release && echo "${UBUNTU_CODENAME:-$VERSION_CODENAME}")
Components: stable
Architectures: $(dpkg --print-architecture)
Signed-By: /etc/apt/keyrings/docker.asc
EOF
sudo apt update
```

4. 安装Docker

```bash
sudo apt install docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
```

5. 修改镜像源

将下面内容添加到/etc/docker/daemon.json 中，如果文件不存在，则自己创建。

```json
{
"registry-mirrors": [
"https://docker.m.daocloud.io/",
"https://dockerpull.com",
"https://docker-0.unsee.tech",
"https://docker-cf.registry.cyou",
"https://docker.1panel.live"
]
}
```

6. 重启Docker 服务，并设置开机自启动

```bash
sudo systemctl daemon-reload
sudo systemctl restart docker
sudo systemctl enable docker
```

7. 将当前用户加入到docker 组

Docker客户端得使用/var/run/docker.sock 这个本地套接字文件与Docker引擎通信，而这个文件默认情况下只有root

和docker 组的用户才能访问。

```bash
# 如果系统没有docker 组，使用下面命令创建
# sudo groupadd docker
sudo usermod -aG docker $USER
```

退出当前终端并重新登录。

8. 验证安装是否成功

执行docker run hello-world，出现下面界面，说明安装成功。

```bash
peanut@wd:~$ docker run hello-world
Hello from Docker!
This message shows that your installation appears to be working correctly.
To generate this message, Docker took the following steps:
1. The Docker client contacted the Docker daemon.
2. The Docker daemon pulled the "hello-world" image from the Docker Hub.
(amd64)
3. The Docker daemon created a new container from that image which runs the
executable that produces the output you are currently reading.
4. The Docker daemon streamed that output to the Docker client, which sent it
to your terminal.
To try something more ambitious, you can run an Ubuntu container with:
$ docker run -it ubuntu bash
Share images, automate workflows, and more with a free Docker ID:
https://hub.docker.com/
For more examples and ideas, visit:
https://docs.docker.com/get-started/
```

详细信息见：Install Docker Engine

## 2.4 Docker 命令

### 帮助手册

Docker CLI 有一个非常好用的帮助手册：docker <command> --help。

### 镜像相关命令

命令 解释

```bash
docker pull
从仓库拉取（下载）镜像
docker push
推送（上传）一个镜像到仓库
docker images
查看镜像
docker rmi
删除镜像
docker save
将镜像导出（保存）为tar 包
docker load
从tar 包加载一个镜像
```

### 容器相关命令

命令 解释

```bash
docker run
```

-d: 后台启动

```bash
--name: 指定容器名
```

依据镜像创建并启动一个新容器

-p: 端口映射

-v：目录挂载

```bash
--network：加入指定的网络
docker ps
```

-a: 查看所有容器（默认只查看正在运行的）

查看容器

-q: 只显示容器的ID

```bash
docker stop
停止一个或多个容器
docker start
启动一个或多个已停止的容器
docker restart
重启一个或多个容器
docker rm
```

删除一个或多个容器

-f: 强制删除正在运行的容器

```bash
docker exec
```

-i: 交互式（即使在后台运行，也能接收终端输入的字符）

在一个正在运行的容器中执行一条命令

-t: 分配一个伪终端

```bash
docker logs
查看容器运行的日志
docker inspect
查看容器的详细信息
docker commit
将容器的当前状态保存为镜像
```

接下来，我们会通过一些具体的实验，让大家练习上面罗列的命令。

## 2.5 Docker 实验

实验1：流程

1. 启动一个Nginx 容器

2. 修改Nginx 的首页（/usr/share/nginx/html/index.html）

3. 将修改后的容器保存为镜像

4. 将镜像保存为tar 包

5. 停止并删除Nginx 容器

6. 从tar 包导入镜像

7. 从新导入的镜像，启动Nginx 容器

8. 查看Nginx 的首页，看是否有被修改

实验2：挂载

```bash
docker run -v 选项用于在容器和宿主机之间实现绑定挂载（Bind Mount）和卷挂载（Volume Mount），实现数据持久
```

化和文件共享。

1. 绑定挂载

将宿主机目录映射到容器内：

```bash
# 绝对路径
docker run -v /app/nginx/www:/usr/share/nginx/html nginx
# 相对路径
docker run -v ./www:/usr/share/nginx/html nginx
```

2. 卷挂载

卷是Docker 管理的特殊目录，用于在容器之间共享和持久化数据。与容器内的普通文件系统不同，卷的生命周期独立于

容器。

```bash
# 创建卷
docker volume create myvolume
# 卷挂载
docker run -v myvolume:/usr/share/nginx/html nginx
```

实验3：自定义网络

Docker 为每个容器都分配了唯一的ip 地址，容器使用ip 地址和端口，可以相互访问。

Docker 启动时会自动创建docker0 虚拟网桥（默认的bridge 网络），所有未指定网络的容器都会连接到这个网桥上。但

在生产环境下，我们不推荐使用默认的bridge 网络（docker0），而是推荐使用自定义网络。因为默认的bridge 网络只

能用IP 地址通信，不支持容器名DNS 解析。

```bash
# 创建网络
docker network create mynetwork
# 查看网络详情
docker network inspect mynetwork
# 启动容器并连接到自定义网络
docker run -d --name app1 --network mynetwork nginx
```

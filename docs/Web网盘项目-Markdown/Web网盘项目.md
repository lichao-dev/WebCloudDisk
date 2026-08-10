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

理解了这三个概念，对理解Docker 至关重要。

## 2.3 安装

### 2.3.1 Linux

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
sudo apt remove $(dpkg --get-selections docker.io docker-compose docker-compose-v2 docker-doc podman-docker containerd runc | cut -f1)
```

![Docker 架构示意图](Web网盘项目.assets/docker-architecture.png)



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

### 2.3.2 macOS

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

---

# 3 第二期：阿里云对象存储OSS

在企业开发中，容灾备份是至关重要的。因此，我们需要一套成熟的方案来解决这个问题。遗憾的是，企业自建解决方案，基本上不可能的。原因有两个：1) 成本太高；2) 无论是数据同步还是错误恢复，实现难度都非常大。

这种情况下，企业往往会使用现有的成熟的云存储方案。使用云存储方案之后，数据丢失的问题基本上就不会发生了，而且也减少了开发和运维的工作量。在我们这个项目中，我们使用的云产品是阿里云对象存储OSS。

## 3.1 快速了解OSS

请查看阿里云官方网站：什么是对象存储OSS。

核心概念：

想要顺利使用OSS，我们必须先了解下面几个基本概念：

- 存储空间（Bucket）

存储空间是用户用于存储对象（Object）的容器，所有的对象都必须隶属于某个存储空间。存储空间具有各种配置属性，包括地域、访问权限、存储类型等。用户可以根据实际需求，创建不同类型的存储空间来存储不同的数据。

- 对象（Object）

对象是OSS 存储数据的基本单元，也被称为OSS 的文件。和传统的文件系统不同，对象没有文件目录层级结构的关系。对象由元数据（Object Meta）、用户数据（Data）和文件名（Key）组成，并且由存储空间内部唯一的Key来标识。对象元数据是一组键值对，表示了对象的一些属性，例如文件类型、编码方式等信息，同时用户也可以在元数据中存储一些自定义的信息。

- 对象名（ObjectKey）

有时也叫ObjectName，是对象所在存储空间（Bucket）的完整名称，包含完整路径和后缀名，如：abc/efg/123.jpg。

- Object 类型— Object 有三种类型：

– Normal：通过简单上传生成的Object。上传结束之后内容是固定的，只能读取，不能修改。如果Object 内容

发生了改变，只能重新上传同名的Object 来覆盖之前的内容。简单上传适用于上传小于5 GB 的单个文件、一

次HTTP 请求交互即可完成上传的场景。

– Multipart：通过分片上传生成的Object。上传结束之后内容是固定的，只能读取，不能修改。如果Object 内

容发生了改变，只能重新上传同名的Object 来覆盖之前的内容。分片上传适用于大文件加速上传、网络环境

较差、文件大小不确定的场景。

– Appendable：通过追加上传生成的Object。顾名思义，Appendable 对象是可以追加数据的。追加上传适用于

视频监控、视频直播等领域生成的实时视频流场景。

重要：OSS 不支持不同类型的Object 相互转换。

- Region（地域）

Region 表示OSS 的数据中心所在物理位置。创建Bucket 时需指定Region。Bucket 创建成功后，不能更改Region。

该Bucket 下所有的Object 都存储在Region 对应的数据中心。

- Endpoint（访问域名）

Endpoint 表示OSS 对外服务的访问域名。OSS 以HTTP RESTful API 的形式对外提供服务，访问不同的Region，

需要不同的域名。内网和外网访问同一个Region 所需要的Endpoint 也是不同的。

详细内容请参见：各个Region 对应的Endpoint。

- AccessKey（访问密钥）

AccessKey 简称AK，指的是访问身份验证中用到的AccessKey ID 和AccessKey Secret。OSS 通过使用AccessKey

ID 和AccessKey Secret 对称加密的方法来验证某个请求的发送者身份。AccessKey ID 用于标识用户；AccessKey

Secret 是用户用于加密签名字符串和OSS 用来验证签名字符串的密钥，必须保密。

详细内容请参见：创建AccessKey。

## 3.2 控制台操作

详细内容请参见：控制台快速入门

## 3.3 安装OSS C++ SDK

```bash
# 安装依赖库
sudo apt install libssl-dev
sudo apt install libcurl4-openssl-dev
# 解压缩C++ SDK 安装包
tar xzvf aliyun-oss-cpp-sdk-1.10.0.tar.gz
cd aliyun-oss-cpp-sdk-1.10.0
# 安装C++ SDK
```

```bash
mkdir build && cd build
cmake ..
make
sudo make install
sudo ldconfig
```

安装成功后，/usr/local/lib/目录下会多一个静态库libalibabacloud-oss-cpp-sdk.a；/usr/local/include/目录

下会多一个文件夹alibabacloud，里面放的是头文件。

### 编译链接选项

重要：OSS C++ SDK 默认关闭了rtti 属性。因此使用g++编译链接时，请添加-fno-rtti -lalibabacloud-oss-

```bash
cpp-sdk -lcurl -lcrypto -lpthread。
```

详细内容请参见：安装C++ SDK。

## 3.4 示例：上传文件

在OSS 中，操作的基本数据单元是文件（Object）。OSS C++ SDK 提供了丰富的文件上传方式，在我们的项目中，使用

简单上传即可。简单上传：包括从内存上传和从本地上传，最大不能超过5GB。

1. 从内存上传

**oss_upload1.cc**

```cpp
#include <alibabacloud/oss/OssClient.h>
using namespace std;
using namespace AlibabaCloud::OSS;
int main(void)
{
8
// 1. 初始化网络等资源
9
InitializeSdk();
10
// 2. 设置OSS 账号信息，创建OssClient
11
string endpoint = "oss-cn-wuhan-lr.aliyuncs.com";
12
string accessKeyId = "LTAI5tHQTUfRRD7DTnRcC9DZ";
13
string accessKeySecret = "BCm5w87LsnCHBL0w6MXwZAnJYY0XoN";
14
string region = "cn-wuhan";
15
ClientConfiguration conf;
16
OssClient client(endpoint, accessKeyId, accessKeySecret, conf);
17
client.SetRegion(region);
18
// 3. 上传文件
19
string bucketName = "peanutixx-oss-demo";
20
string objectName = "dir/demo1.txt";
21
string content = "Hello AlibbaCloud OSS";
22
shared_ptr<iostream> stream = make_shared<stringstream>(move(content));
23
PutObjectRequest request(bucketName, objectName, stream);
24
auto outcome = client.PutObject(request);
25
// 4. 错误处理
26
if (!outcome.isSuccess()) {
27
cout << "PutObject FAILED"
28
<< ", code:" << outcome.error().Code()
```

```bash
29
<< ", message:" << outcome.error().Message()
30
<< ", requestId:" << outcome.error().RequestId() << endl;
31
exit(1);
32
}
33
// 5. 释放网络等资源。
34
ShutdownSdk();
35
return 0;
}
```

2. 从本地上传

**oss_upload2.cc**

```cpp
#include <alibabacloud/oss/OssClient.h>
using namespace std;
using namespace AlibabaCloud::OSS;
int main(void)
{
8
// 1. 初始化网络等资源
9
InitializeSdk();
10
// 2. 设置OSS 账号信息，创建OssClient 对象
11
string endpoint = "oss-cn-wuhan-lr.aliyuncs.com";
12
string accessKeyId = "LTAI5tHQTUfRRD7DTnRcC9DZ";
13
string accessKeySercret = "BCm5w87LsnCHBL0w6MXwZAnJYY0XoN";
14
string region = "cn-wuhan";
15
ClientConfiguration conf;
16
OssClient client(endpoint, accessKeyId, accessKeySercret, conf);
17
client.SetRegion(region);
18
// 3. 上传文件
19
string bucketName = "peanutixx-oss-demo";
20
string objectName = "dir/demo2.txt";
21
auto outcome = client.PutObject(bucketName, objectName, "a.txt");
22
// 4. 处理错误
23
if (!outcome.isSuccess()) {
24
cout << "PutObject FAILED"
25
<< ", code:" << outcome.error().Code()
26
<< ", message:" << outcome.error().Message()
27
<< ", requestId:" << outcome.error().RequestId() << endl;
28
exit(1);
29
}
30
// 5. 释放网络等资源
31
ShutdownSdk();
32
return 0;
}
```

### 注意事项

在整个程序的生命周期中，InitializeSdk() 和ShutdownSdk() 应该各只执行一次！使用SDK 之前，执行

```bash
InitializeSdk()；SDK 使用完毕之后（后续不再使用），执行ShutdownSdk()。
```

---

# 4 第三期：消息队列RabbitMQ

同步备份文件会导致响应时间变长，影响用户体验。我们可以改用消息队列实现异步备份，从而提升系统响应速度。

## 4.1 引入消息队列带来的好处

1. 异步解耦，提升响应速度

这是我们引入消息队列的核心原因。生产者（我们的应用程序）只需把备份任务作为“消息”丢进队列，立刻就能

返回，无需等待耗时的备份操作完成。用户体验得到直接改善。

2. 削峰填谷，增强系统稳定性

在业务高峰期（如大促期间），瞬间的请求量可能压垮后端。消息队列能像大坝一样，把突发的流量先蓄起来（削

峰），然后让后端服务按照自己能处理的速度去消费（填谷），避免后端被冲垮。

3. 故障隔离，提高系统可用性

当备份服务（消费者）临时宕机或网络抖动时，消息会安全地存储在队列中。待服务恢复后，它可以继续从断点处

处理消息。生产者的核心业务不受影响，两者故障互相隔离。

4. 支持最终一致性

对于备份这类对实时一致性要求不高的场景（不需要备份立即完成），消息队列能很好地保证“最终一致性”——即

只要没有意外，消息最终一定会被处理，备份最终一定会完成。

5. 弹性扩展

如果备份任务增多，可以方便地增加“消费者”（即更多的备份服务器）来处理队列中的消息，实现水平扩展。

## 4.2 RabbitMQ 简介

RabbitMQ 是一个广泛使用的开源消息队列中间件，它实现了AMQP（高级消息队列协议），基于Erlang 语言6 开发，具

备高可靠性、灵活的路由能力以及易于使用的管理界面。

它的核心设计理念是：生产者从不直接发送消息到队列，而是发送给“交换机”，由交换机根据提前配置好的路由规则，

将消息分发到不同的队列中。

```bash
6Erlang：是一门专为高并发、高容错、分布式系统而生的函数式编程语言，由爱立信（Ericsson）公司于1986 年设计。
```

![学习新技术的流程示意图 1](/Users/lichao/Downloads/Web网盘项目.assets/learning-process-1.png)

![学习新技术的流程示意图 2](/Users/lichao/Downloads/Web网盘项目.assets/learning-process-2.png)

## 4.3 使用Docker 安装RabbitMQ

```bash
# 拉取带管理插件的RabbitMQ 镜像，包含Web 可视化管理界面
docker pull rabbitmq:management
docker run -d \
# 后台运行容器（守护模式）
--hostname rabbitsrv \
# 设置容器内部主机名
--name rabbit \
# 容器名称
-p 5672:5672 \
# AMQP 协议端口（应用程序连接RabbitMQ 使用）
-p 15672:15672 \
# 管理界面端口（浏览器访问http://localhost:15672）
-p 25672:25672 \
# 集群通信端口
-v /data/rabbitmq:/var/lib/rabbitmq \
# 挂载数据目录（持久化消息队列数据）
rabbitmq:management
# 使用带管理插件的RabbitMQ 镜像
```

## 4.4 RabbitMQ 的架构和核心概念

这一小节，我们讲解RabbitMQ 的架构和它的一些核心概念，这些是了解和使用RabbitMQ 的前提。

RabbitMQ 的架构图如下所示：

RabbitMQ 的核心概念：

1. 生产者（Producer）— 发送消息的应用程序。

生产者创建消息并将消息发送到交换机，生产者可以指定消息的路由键（Routing Key），交换机会根据路由键判断

消息应路由到哪个队列。

2. 消费者（Consumer）— 接收和处理消息的应用程序。

消费者从队列中接收消息，处理业务逻辑。消费者有两种消费模式：

- 拉取模式（Pull）：消费者主动从队列拉取消息。

- 推送模式（Push）：RabbitMQ 主动推送消息给消费者。

3. 队列（Queue）— 存储消息的容器，本质上是一个命名缓冲区。

4. 交换机（Exchange）— 接收生产者发送的消息，并根据路由规则将消息路由到一个或多个队列。

RabbitMQ 有四种类型的交换机：

![RabbitMQ 架构图](/Users/lichao/Downloads/Web网盘项目.assets/rabbitmq-architecture.png)

类型 路由规则 作用

直连（Direct） 消息的routingKey 必须与

单播

队列绑定的bindingKey 完全匹配。

扇形（Fanout） 忽略routingKey，广播给所有绑定的队列。 广播

```bash
routingKey 支持通配符匹配：
*匹配一个字段，
```

主题（Topic）

根据多个条件路由

```bash
#: 匹配任意多个字段（包括0 个），
```

字段之间以. 分隔。

标头（Headers） 根据消息的头属性匹配，忽略routingKey。 用于路由规则非常复杂的情况

5. 绑定（Binding）— 连接交换机和队列的规则，定义了消息从交换机路由到队列的条件。

6. 路由键（Routing Key）— 生产者在发送消息时指定的一个标签，用于交换机判断消息应路由到哪个队列。

7. 虚拟主机（Virtual Host，vhost）— RabbitMQ 中的隔离环境，类似于MySQL 的数据库。

虚拟主机的目的是实现多租户隔离，不同虚拟主机的交换机、队列、绑定是完全隔离的。默认的虚拟主机是/。

8. 连接（Connection）— 客户端和RabbitMQ 服务器之间的TCP 长连接。

一个连接可以包含多个通道（Channel），连接的创建和销毁开销比较大。

9. 通道（Channel）— 在Connection 内部创建的轻量级虚拟连接。

多个通道可以复用同一个连接（回顾：TCP 的多路复用）。每个通道都有自己独立的ID。大部分AMQP 协议的操作

都可以在通道上完成。

这些核心概念是理解和使用RabbitMQ 的基础，掌握它们就能应对90% 以上的消息队列使用场景。

## 4.5 控制台操作

[[上课演示]]

## 4.6 安装SimpleAmqpClient

```bash
# 更新软件包列表，获取最新的可用版本信息
sudo apt update
# SimpleAmqpClient 依赖boost 库，安装Boost 的全套开发文件（头文件及预编译库）
sudo apt install libboost-all-dev
# SimpleAmqpClient 依赖rabbitmq-c，先安装rabbitmq-c
cd rabbitmq-c-0.11.0/
mkdir build && cd build
cmake ..
make
sudo make install
sudo ldconfig
# 安装SimpleAmqpClient
cd SimpleAmqpClient-2.5.1/
mkdir build && cd build
cmake ..
make
sudo make install
sudo ldconfig
```

安装成功后，/usr/local/lib/目录下会多三个库文件libSimpleAmqpClient.so、libSimpleAmqpClient.so.7、

```bash
libSimpleAmqpClient.so.7.0.1；/usr/local/include/目录下会多一个文件夹SimpleAmqpClient，里面放的是头文
```

件。

## 4.7 示例：生产者发送消息

**rabbitmq_producer.cc**

```cpp
#include <SimpleAmqpClient/SimpleAmqpClient.h>
#include <string>
using namespace std;
using namespace AmqpClient;
int main()
{
9
// 1. 创建Channel
10
string host = "127.0.0.1";
11
int port = 5672;
12
string username = "guest";
13
string password = "guest";
14
string vhost = "/";
15
Channel::ptr_t channel = Channel::Create(host, port, username, password, vhost);
16
// 2. 构建消息
17
BasicMessage::ptr_t message = BasicMessage::Create("Hello RabbitMQ");
18
// 3. 发送消息
19
string exchange = "oss.direct"; // 交换机
20
string routingKey = "oss"; // 消息的routingKey
21
channel->BasicPublish(exchange, routingKey, message); // 发布消息
}
```

## 4.8 示例：消费者消费消息

**rabbitmq_consumer.cc**

```cpp
#include <SimpleAmqpClient/SimpleAmqpClient.h>
#include <iostream>
#include <string>
using namespace std;
using namespace AmqpClient;
int main()
{
10
// 1. 以URI 的方式创建Channel
11
string uri = "amqp://guest:guest@localhost:5672/%2f";
12
Channel::ptr_t channel = Channel::CreateFromUri(uri);
14
// 2. 获取消息
15
// 方式1：拉取模式--- 消费者主动从队列中拉取消息（非阻塞式）
16
// const string& q = "oss.queue";
```

```bash
18
// 如果队列中有消息，将消息放入到evelope 中，并返回true
19
// 如果队列中没有消息，BasicGet 会立刻返回false
20
// Envelope::ptr_t envelope;
21
// channel->BasicGet(envelope, q);
22
// if (envelope && envelope->Message()) { */
23
//
cout << envelope->Message()->Body() << endl;
24
// }
26
// 方式2：推送模式--- 等待RabbitMQ 推送消息(阻塞式)
27
const string& q = "oss.queue";
28
channel->BasicConsume(q); // 订阅队列
29
// 阻塞：等待RabbitMQ 推送消息
30
Envelope::ptr_t envelope = channel->BasicConsumeMessage();
31
// 打印消息
32
if (envelope && envelope->Message()) {
33
cout << envelope->Message()->Body() << endl;
34
}
}
```

#

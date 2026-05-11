# TCPServer

这是一个基于 Linux epoll 的 C++17 HTTP/TCP 服务器。当前项目支持普通 Reactor 模式和 `SO_REUSEPORT` 多线程模式，并提供注册、登录、图片上传、图片列表和图片下载功能。

## 当前功能

- 非阻塞 socket
- epoll 事件驱动
- 支持 listenfd / connfd 的 LT、ET 触发组合
- 普通模式：一个监听 socket，按 `N` 分发到 subreactor
- `SO_REUSEPORT` 模式：创建 `N` 个服务器线程，每个线程监听同一端口
- HTTP GET / POST 解析
- 支持长连接和短连接
- Cookie session 登录状态
- 注册、登录、退出
- MySQL 保存用户和上传文件记录
- 密码加盐 hash 后入库，不保存明文密码
- 上传图片到 `uploads/`
- 上传文件按原文件名加时间戳保存，避免固定文件名互相覆盖
- `/api/images` 返回可下载图片列表
- `/download?file=...` 下载指定图片

## 目录结构

```text
TCPServer/
├── main.cpp
├── Makefile
├── ThreadPool.h
├── common.h
├── server/
│   └── TCPServer.h
├── reactor/
│   ├── Eventloop.h
│   ├── Eventloop.cpp
│   ├── Epoller.h
│   └── Epoller.cpp
├── http/
│   ├── Connection.h
│   ├── Connection.cpp
│   └── HttpParser.h
├── db/
│   ├── MySQLStore.h
│   ├── MySQLStore.cpp
│   ├── PasswordHash.h
│   ├── PasswordHash.cpp
│   └── schema.sql
├── static/
│   ├── index.html
│   ├── login.html
│   └── register.html
├── uploads/
├── utils/
│   ├── Buffer.h
│   ├── Buffer.cpp
│   └── SocketUtil.h
├── logs/
│   ├── Logger.h
│   ├── Logger.cpp
│   ├── server.log
│   └── client.log
└── tools/
    ├── bench_close.sh
    ├── bench_compare.sh
    ├── bench_keepalive.sh
    ├── load_test
    ├── load_test.cpp
    └── README.md
```

## 编译

直接使用项目根目录的 Makefile：

```bash
make
```

当前 Makefile 会编译：

```text
main.cpp
config.cpp
db/MySQLStore.cpp
db/PasswordHash.cpp
http/Connection.cpp
reactor/Eventloop.cpp
reactor/Epoller.cpp
logs/Logger.cpp
utils/Buffer.cpp
```

需要安装 MySQL 客户端开发库和 OpenSSL 开发库，例如：

```bash
sudo apt install libmysqlclient-dev libssl-dev
```

清理：

```bash
make clean
```

## 启动参数

启动格式：

```bash
./main [-p port] [-M mode] [-N count] [-S poolsize] [-l 0|1] [-c 0|1] [-T 0..3] [--db-host host] [--db-port port] [--db-user user] [--db-password password] [--db-name name]
```

参数必须用空格分隔，例如 `-p 9006`，不能写成 `-p9006`。

| 参数 | 说明 | 默认值 |
| --- | --- | --- |
| `-p` | 监听端口 | `9006` |
| `-M` | 运行模式：`0` 普通模式，`1` SO_REUSEPORT 模式 | `1` |
| `-N` | 数量参数，含义由 `-M` 决定 | `1` |
| `-S` | 每个 reactor 的 worker 线程池大小 | `3` |
| `-l` | 日志写入方式：`0` 同步，`1` 异步 | `1` |
| `-c` | 是否关闭日志：`0` 开启，`1` 关闭 | `0` |
| `-T` | 触发组合：`0` LT/LT，`1` LT/ET，`2` ET/LT，`3` ET/ET | `3` |
| `--db-host` | MySQL 主机 | `127.0.0.1` |
| `--db-port` | MySQL 端口 | `3306` |
| `--db-user` | MySQL 用户 | `tcpserver` |
| `--db-password` | MySQL 密码 | `123456` |
| `--db-name` | MySQL 数据库名 | `tcpserver` |

`-N` 的具体含义：

- `-M 0`：`N` 是 subreactor 数量，传给 `server.running(N)`。`N=0` 表示主 reactor 自己处理连接。
- `-M 1`：`N` 是 `SO_REUSEPORT` 服务器线程数量，传给 `TCPServer::REUSEPORT_RUNNING(N, port)`。如果没有指定 `-N` 或 `N<=0`，程序会按 `N=1` 启动，避免直接退出。

示例：

```bash
./main
./main -p 9006
./main -p 9006 -M 0
./main -p 9006 -N 4 -M 0
./main -p 9006 -N 4 -M 1
./main -p 9006 -M 0 -N 4 -S 8 -l 1 -c 0 -T 3
```

## MySQL

服务器会在第一次访问数据库时自动创建数据库和表。数据库连接参数由 `Config` 解析，默认值如下：

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `--db-host` | `127.0.0.1` | MySQL 主机 |
| `--db-port` | `3306` | MySQL 端口 |
| `--db-user` | `tcpserver` | MySQL 用户 |
| `--db-password` | `123456` | MySQL 密码 |
| `--db-name` | `tcpserver` | 数据库名 |

如果本机 root 不能免密访问，可以先创建专用用户：

```sql
CREATE DATABASE IF NOT EXISTS tcpserver DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE USER IF NOT EXISTS 'tcpserver'@'localhost' IDENTIFIED BY 'your_password';
GRANT ALL PRIVILEGES ON tcpserver.* TO 'tcpserver'@'localhost';
FLUSH PRIVILEGES;
```

启动时可以直接指定数据库参数：

```bash
./main -p 9006 --db-host 127.0.0.1 --db-user tcpserver --db-password your_password --db-name tcpserver
```

建表 SQL 也保存在：

```text
db/schema.sql
```

当前注册和登录仍通过 HTTP 表单提交，传输层仍是明文；服务端收到密码后会加盐 hash，数据库只保存 `password_hash`，不会保存明文密码。生产环境仍应使用 HTTPS。

## Web 页面

启动服务后访问：

```text
http://localhost:9006/
```

未登录会跳转到：

```text
http://localhost:9006/login
```

注册页面：

```text
http://localhost:9006/register
```

登录后可以上传图片、刷新图片列表、选择图片下载。

## HTTP 接口

### 注册

```bash
curl -i -d "username=test&password=123" http://localhost:9006/register
```

成功后返回 `302 Location: /login`。

### 登录

```bash
curl -i -c cookie.txt -d "username=test&password=123" http://localhost:9006/login
```

成功后返回 `Set-Cookie: SID=...`，并跳转到 `/`。

### 访问首页

```bash
curl -i -b cookie.txt http://localhost:9006/
```

### 上传图片

```bash
curl -i -b cookie.txt -F "file=@/path/to/image.jpg" http://localhost:9006/upload
```

上传后的文件保存在 `uploads/`，文件名类似：

```text
image_20260504_123507_118.jpg
```

### 查询图片列表

```bash
curl -i -b cookie.txt http://localhost:9006/api/images
```

返回示例：

```json
[
  {
    "name": "image_20260504_123507_118.jpg",
    "url": "/download?file=image_20260504_123507_118.jpg"
  }
]
```

### 下载指定图片

```bash
curl -i -b cookie.txt "http://localhost:9006/download?file=image_20260504_123507_118.jpg"
```

下载响应包含：

```http
Content-Disposition: attachment; filename="image_20260504_123507_118.jpg"
```

### 退出登录

```bash
curl -i -b cookie.txt http://localhost:9006/logout
```

## 压测

项目提供两类压测工具：

- `tools/load_test`：自定义短连接压测程序，适合学习 socket 压测原理。
- `tools/bench_*.sh`：对 `wrk` 的简单封装，适合快速进行标准压测。

### wrk 脚本

进入 `tools/` 目录：

```bash
cd tools
```

长连接压测：

```bash
./bench_keepalive.sh http://127.0.0.1:9006/login 10s 100 4
```

短连接压测：

```bash
./bench_close.sh http://127.0.0.1:9006/login 10s 100 4
```

长短连接对比：

```bash
./bench_compare.sh http://127.0.0.1:9006/login 10s 100 4
```

参数顺序统一为：

```text
url duration connections threads
```

例如：

```bash
./bench_compare.sh http://127.0.0.1:9006/login 10s 1000 4
```

等价于用 4 个 wrk 线程、1000 个并发连接，持续压测 10 秒。

### 自定义 load_test

运行：

```bash
cd tools
./load_test
```

这个工具主要用于和 `wrk -H "Connection: close"` 的短连接结果互相验证。

### 推荐测试顺序

先启动服务器：

```bash
./main -p 9006 -N 4 -M 0
```

再进行压测：

```bash
cd tools
./bench_compare.sh http://127.0.0.1:9006/login 10s 100 4
./bench_compare.sh http://127.0.0.1:9006/login 10s 500 4
./bench_compare.sh http://127.0.0.1:9006/login 10s 1000 4
```

建议先测 `/login`，因为它不需要 cookie，响应稳定。

重点观察：

```text
Requests/sec
Latency
Transfer/sec
Socket errors
```

更多压测说明见：

```text
tools/README.md
```

### 配置矩阵 QPS 测试

测试命令统一为：

```bash
wrk -t2 -c1000 -d20s http://localhost:9006
```

服务端启动参数以 `./main -c 1 -T 3 -M 0 -S 0 -N 0` 为基础，只修改 `M/T/S/N`。其中 `M=0` 只测一次；`M=1` 下测试 `T=0/2/3`、`S=0/2`、`N=1/2/4/6` 的组合。`S=0` 表示不创建 worker 线程池，请求直接在 reactor 线程处理；`S=2` 表示每个 reactor 创建 2 个 worker 线程。

| M | T | S | N | QPS |
| --- | --- | --- | --- | ---: |
| 0 | 3 | 0 | 0 | 86,715.64 |
| 1 | 0 | 0 | 1 | 86,509.71 |
| 1 | 0 | 0 | 2 | 138,749.60 |
| 1 | 0 | 0 | 4 | 124,792.83 |
| 1 | 0 | 0 | 6 | 125,605.64 |
| 1 | 0 | 2 | 1 | 58,372.05 |
| 1 | 0 | 2 | 2 | 83,133.36 |
| 1 | 0 | 2 | 4 | 115,344.85 |
| 1 | 0 | 2 | 6 | 112,669.67 |
| 1 | 2 | 0 | 1 | 105,894.38 |
| 1 | 2 | 0 | 2 | 137,999.27 |
| 1 | 2 | 0 | 4 | 146,809.18 |
| 1 | 2 | 0 | 6 | 142,709.68 |
| 1 | 2 | 2 | 1 | 48,152.57 |
| 1 | 2 | 2 | 2 | 74,675.98 |
| 1 | 2 | 2 | 4 | 100,301.62 |
| 1 | 2 | 2 | 6 | 112,042.17 |
| 1 | 3 | 0 | 1 | 105,189.11 |
| 1 | 3 | 0 | 2 | 139,836.07 |
| 1 | 3 | 0 | 4 | 157,144.77 |
| 1 | 3 | 0 | 6 | 155,898.12 |
| 1 | 3 | 2 | 1 | 51,200.62 |
| 1 | 3 | 2 | 2 | 79,633.79 |
| 1 | 3 | 2 | 4 | 111,841.45 |
| 1 | 3 | 2 | 6 | 115,345.49 |

结果说明：

- 本轮最高 QPS 是 `157,144.77`，对应 `M=1 T=3 S=0 N=4`，也就是 `SO_REUSEPORT`、listenfd ET + connfd ET、不创建 worker 线程池、4 个 server 线程。
- `T=3` 整体表现最好，说明在当前短响应、高并发 wrk 场景下，listenfd 和 connfd 都使用 ET 可以减少重复事件通知带来的开销。
- `S=0` 明显优于 `S=2`。本次 wrk 访问的是 `/`，请求处理路径较轻，直接在 reactor 线程生成响应避免了投递线程池、跨线程回调和唤醒的额外开销；当请求变成文件上传、复杂业务或阻塞 I/O 时，worker 线程池仍然更适合隔离慢任务。
- `N` 从 1 增加到 2、4 通常能提高 QPS；继续到 6 时收益变小或略有下降，说明已经接近当前机器和 wrk 压测条件下的调度/CPU 上限。
- `M=0` 单 reactor 测得 `86,715.64 QPS`；`M=1` 在合适的 `T/S/N` 组合下能明显提升吞吐。

## 注意事项

- 用户和 session 当前保存在进程内存中，服务重启后会丢失。
- 图片文件保存在本地 `uploads/` 目录。
- 当前密码传输仍是 HTTP 明文；数据库不保存明文密码，但生产环境必须使用 HTTPS。
- 项目依赖 Linux / WSL 环境。
- 高并发短连接压测时，`listen` backlog、`ulimit -n`、`somaxconn`、`tcp_max_syn_backlog` 都可能影响结果。

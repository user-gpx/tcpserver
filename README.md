# TCPServer

这是一个基于 Linux epoll 的 C++17 HTTP/TCP 服务器。当前项目支持普通 Reactor 模式和 `SO_REUSEPORT` 多线程模式，并提供注册、登录、图片上传、图片列表和图片下载功能。

## 当前功能

- 非阻塞 socket
- epoll 事件驱动
- Edge Trigger 模式
- 普通模式：一个监听 socket，按 `N` 分发到 subreactor
- `SO_REUSEPORT` 模式：创建 `N` 个服务器线程，每个线程监听同一端口
- HTTP GET / POST 解析
- 支持长连接和短连接
- Cookie session 登录状态
- 注册、登录、退出
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
├── static/
│   ├── index.html
│   ├── login.html
│   └── register.html
├── uploads/
├── utils/
│   ├── Buffer.h
│   ├── Buffer.cpp
│   ├── Logger.h
│   ├── Logger.cpp
│   └── SocketUtil.h
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
http/Connection.cpp
reactor/Eventloop.cpp
reactor/Epoller.cpp
utils/Logger.cpp
utils/Buffer.cpp
```

清理：

```bash
make clean
```

## 启动参数

启动格式：

```bash
./main [-p port] [-N count] [-M mode]
```

参数必须用空格分隔，例如 `-p 9006`，不能写成 `-p9006`。

| 参数 | 说明 | 默认值 |
| --- | --- | --- |
| `-p` | 监听端口 | `9006` |
| `-N` | 数量参数，含义由 `-M` 决定 | `0` |
| `-M` | 运行模式：`0` 普通模式，`1` SO_REUSEPORT 模式 | `1` |

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
```

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

## 注意事项

- 用户和 session 当前保存在进程内存中，服务重启后会丢失。
- 图片文件保存在本地 `uploads/` 目录。
- 当前密码处理是示例级实现，不适合作为生产环境认证方案。
- 项目依赖 Linux / WSL 环境。
- 高并发短连接压测时，`listen` backlog、`ulimit -n`、`somaxconn`、`tcp_max_syn_backlog` 都可能影响结果。

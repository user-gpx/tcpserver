# Tools

这个目录放压测相关工具。

## wrk 脚本

这几个脚本是对 `wrk` 的简单封装，用来快速测试长连接和短连接。

默认测试地址是：

```text
http://127.0.0.1:9006/login
```

默认参数是：

```text
duration=10s
connections=100
threads=4
```

### bench_keepalive.sh

长连接压测。`wrk` 默认使用 keep-alive，连接建立后会复用连接反复发送请求。

```bash
./bench_keepalive.sh [url] [duration] [connections] [threads]
```

示例：

```bash
./bench_keepalive.sh http://127.0.0.1:9006/login 10s 100 4
```

等价于：

```bash
wrk -t4 -c100 -d10s http://127.0.0.1:9006/login
```

### bench_close.sh

短连接压测。脚本会添加请求头：

```http
Connection: close
```

服务器处理完一个请求后会关闭连接，因此这个脚本主要测试 TCP 建连、accept、HTTP 处理和关闭连接的综合能力。

```bash
./bench_close.sh [url] [duration] [connections] [threads]
```

示例：

```bash
./bench_close.sh http://127.0.0.1:9006/login 10s 100 4
```

等价于：

```bash
wrk -t4 -c100 -d10s -H "Connection: close" http://127.0.0.1:9006/login
```

### bench_compare.sh

对比脚本。它不是一种新的压测模式，而是用同一组参数连续执行：

```bash
bench_keepalive.sh
bench_close.sh
```

用途是方便比较同一个 URL、同一个并发数、同一个测试时长下：

- 长连接性能
- 短连接性能

用法：

```bash
./bench_compare.sh [url] [duration] [connections] [threads]
```

示例：

```bash
./bench_compare.sh http://127.0.0.1:9006/login 10s 100 4
```

它等价于依次执行：

```bash
./bench_keepalive.sh http://127.0.0.1:9006/login 10s 100 4
./bench_close.sh http://127.0.0.1:9006/login 10s 100 4
```

## 参数说明

| 参数 | 含义 | 示例 |
| --- | --- | --- |
| `url` | 压测目标 URL | `http://127.0.0.1:9006/login` |
| `duration` | 压测持续时间 | `10s`、`30s` |
| `connections` | 并发连接数 | `100`、`1000` |
| `threads` | wrk 工作线程数 | `4`、`8` |

## 推荐测试顺序

先启动服务器：

```bash
cd ..
./main -p 9006 -N 4 -M 0
```

然后在 `tools/` 目录压测：

```bash
cd tools
./bench_compare.sh http://127.0.0.1:9006/login 10s 100 4
./bench_compare.sh http://127.0.0.1:9006/login 10s 500 4
./bench_compare.sh http://127.0.0.1:9006/login 10s 1000 4
```

建议先测 `/login`，因为它不需要 cookie，响应稳定。

## 看哪些指标

重点看：

```text
Requests/sec
Latency Avg
Latency Max
Socket errors
Transfer/sec
```

如果出现 `Socket errors`，说明连接建立、读、写或超时阶段出现了失败。短连接高并发时更容易出现 connect error。

## load_test

`load_test` 是自定义压测程序，适合学习 socket 压测原理。它和 `wrk -H "Connection: close"` 的短连接结果可以互相验证。

// 多reator server
#pragma once

#include "../common.h"
#include "../reactor/Eventloop.h"
#include "../utils/SocketUtil.h"

class TCPServer {
  private:
    uint16_t port;
    int _listenfd; // 监听套接字
    EventLoop mainreactor;
    int N; // subreactor的数量
    int _connectionTimeoutMs;

  public:
    TCPServer() : port(0), _listenfd(-1), N(0), _connectionTimeoutMs(60000) {}
    int init(uint16_t port = 9006) { // 初始化
        this->port = port;
        Logger::server().setLevel(LogLevel::Info);
        Logger::server().setConsoleLevel(LogLevel::Error);
        if (!Logger::server().init("logs/server.log")) {
            LOG_WARN("server logger init failed, fallback to stderr only");
        }
        _listenfd = socket(AF_INET, SOCK_STREAM, 0);
        if (_listenfd < 0) {
            LOG_ERR("socket(AF_INET, SOCK_STREAM)");
            return -1;
        }
        int opt = 1;
        int rv = setsockopt(_listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        if (rv < 0) {
            LOG_ERR("setsockopt(SO_REUSEADDR)");
            return -1;
        }
        // 设置 SO_REUSEPORT 而不是 SO_REUSEADDR
        opt = 1;
        rv = setsockopt(_listenfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
        if (rv < 0) {
            LOG_ERR("setsockopt(SO_REUSEPORT)");
            return -1;
        }
        struct sockaddr_in addr{};
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

        rv = bind(_listenfd, (const sockaddr*)&addr, sizeof(addr));

        if (rv < 0) {
            LOG_ERR("bind(listenfd)");
            return -1;
        }
        if (setNonBlock(_listenfd) < 0) { return -1; }

        return _listenfd;
    }
    int Listening(int _n = 1024) {
        int rv = listen(_listenfd, _n);
        if (rv < 0) {
            LOG_ERR("listen(listenfd)");
            return -1;
        }
        LOG_INFO("server listen on port=%u backlog=%d", static_cast<unsigned>(port), _n);
        return 0;
    }
    int running(int N) {
        if (mainreactor.init(TriggerMode::EdgeTrigger, _listenfd, _connectionTimeoutMs) < 0) {
            return -1;
        }
        this->N = N;
        LOG_INFO("server start subreactor=%d timeout_ms=%d", N, _connectionTimeoutMs);
        return mainreactor.mainreacotloop(N, TriggerMode::EdgeTrigger);
    };
    void setConnectionTimeoutMs(int timeout_ms) {
        if (timeout_ms > 0) { _connectionTimeoutMs = timeout_ms; }
    }
    static int REUSEPORT_RUNNING(int N, int port) {
        std::vector<std::thread> threads;
        for (int i = 0; i < N; i++) {
            threads.emplace_back([port, i]() {
                TCPServer server;
                int fd = server.init(port);
                if (fd < 0) { return; }
                if (server.Listening() < 0) { return; }
                if (server.running(0) < 0) { return; }
            });
        }
        for (auto& t : threads) { t.join(); }
        return 0;
    }
};

#pragma once

#include "../common.h"
#include "../config.h"
#include "../reactor/Eventloop.h"
#include "../utils/SocketUtil.h"

class TCPServer {
  private:
    uint16_t port;
    int _listenfd;
    EventLoop mainreactor;
    int N;
    int _connectionTimeoutMs;
    Config _config;

    void initLoggers() {
        Logger::server().setLevel(LogLevel::Info);
        Logger::server().setConsoleLevel(LogLevel::Error);
        Logger::server().setAsync(_config.logWrite != 0);
        Logger::server().setEnabled(_config.closeLog == 0);

        Logger::client().setLevel(LogLevel::Info);
        Logger::client().setConsoleLevel(LogLevel::Error);
        Logger::client().setAsync(_config.logWrite != 0);
        Logger::client().setEnabled(_config.closeLog == 0);

        if (!Logger::server().init("logs/server.log")) {
            LOG_WARN("server logger init failed, fallback to stderr only");
        }
        if (!Logger::client().init("logs/client.log")) {
            LOG_WARN("client logger init failed, fallback to stderr only");
        }
    }

  public:
    TCPServer() : port(0), _listenfd(-1), N(0), _connectionTimeoutMs(60000) {}

    int init(const Config& config) {
        _config = config;
        port = static_cast<uint16_t>(config.port);
        initLoggers();

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

        opt = 1;
        rv = setsockopt(_listenfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
        if (rv < 0) {
            LOG_ERR("setsockopt(SO_REUSEPORT)");
            return -1;
        }

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

        rv = bind(_listenfd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
        if (rv < 0) {
            LOG_ERR("bind(listenfd)");
            return -1;
        }
        if (setNonBlock(_listenfd) < 0) { return -1; }

        return _listenfd;
    }

    int init(uint16_t port = 9006) {
        Config config;
        config.port = port;
        return init(config);
    }

    int Listening(int backlog = 1024) {
        int rv = listen(_listenfd, backlog);
        if (rv < 0) {
            LOG_ERR("listen(listenfd)");
            return -1;
        }
        LOG_INFO("server listen on port=%u backlog=%d", static_cast<unsigned>(port), backlog);
        return 0;
    }

    int running(int subreactorCount) {
        if (mainreactor.init(_config.listenTriggerMode, _config.connTriggerMode, _listenfd,
                             _connectionTimeoutMs, _config.poolSize) < 0) {
            return -1;
        }
        N = subreactorCount;
        LOG_INFO("server start subreactor=%d pool_size=%d timeout_ms=%d", N, _config.poolSize,
                 _connectionTimeoutMs);
        return mainreactor.mainreacotloop(N, _config.listenTriggerMode, _config.connTriggerMode,
                                          _config.poolSize);
    }

    void setConnectionTimeoutMs(int timeout_ms) {
        if (timeout_ms > 0) { _connectionTimeoutMs = timeout_ms; }
    }

    static int REUSEPORT_RUNNING(const Config& config) {
        std::vector<std::thread> threads;
        for (int i = 0; i < config.N; i++) {
            threads.emplace_back([config]() {
                TCPServer server;
                int fd = server.init(config);
                if (fd < 0) { return; }
                if (server.Listening() < 0) { return; }
                if (server.running(0) < 0) { return; }
            });
        }
        for (auto& t : threads) { t.join(); }
        return 0;
    }
};

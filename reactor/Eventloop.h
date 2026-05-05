#pragma once

#include "../common.h"
#include "../http/Connection.h"
#include "../utils/SocketUtil.h"
#include "Epoller.h"

class EventLoop {
    Epoller _epoller;
    std::unordered_map<int, std::shared_ptr<Connection>> _connections;
    int _listenfd;
    TriggerMode _listenTriggerMode;
    static const int _maxEvents = 256;
    epoll_event events[_maxEvents];
    std::unique_ptr<ThreadPool> pool;
    IoQueueContext ioque;
    ConnfdQueue connfdque;

    EventLoop* subreactor;
    int N = 0;
    int lastsub = 0;
    int _connectionTimeoutMs = 60000;
    std::chrono::steady_clock::time_point _lastTimeoutSweep;
    std::atomic<bool> _stop{false};
    std::vector<std::thread> _threads;

  public:
    EventLoop()
        : _listenfd(-1), _listenTriggerMode(TriggerMode::LevelTrigger), subreactor(nullptr) {}
    int init(TriggerMode listenTriggerMode, TriggerMode connTriggerMode, int _listenfd,
             int connectionTimeoutMs, int poolSize);
    bool acceptclient();
    int mainreacotloop(int N, TriggerMode listenTriggerMode, TriggerMode connTriggerMode,
                       int poolSize);
    int loop();
    void close_connfd(int connfd, const char* reason = "closed");
    ~EventLoop();
    void stop();

  private:
    void addConnection(int connfd);
    void closeIdleConnections();
};

#include "Eventloop.h"

int EventLoop::init(TriggerMode triggermode, int _listenfd, int connectionTimeoutMs) {
    if (_epoller.create(triggermode) < 0) { return -1; }
    ioque.init(_epoller.wakeupfd);
    connfdque.init(_epoller.wakeupfd);
    this->_listenfd = _listenfd;
    _connectionTimeoutMs = connectionTimeoutMs;
    _lastTimeoutSweep = std::chrono::steady_clock::now();

    if (_listenfd >= 0) {
        uint32_t baseevent = EPOLLIN;
        if (triggermode == TriggerMode::EdgeTrigger) { baseevent |= EPOLLET; }
        if (_epoller.addfd(_listenfd, baseevent) < 0) { return -1; }
    }

    return 0;
}

bool EventLoop::acceptclient() {
    do {
        sockaddr client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        int connfd = accept(_listenfd, &client_addr, &addr_len);
        if (connfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else if (errno == EINTR) {
                continue;
            } else {
                LOG_ERR("accept(listenfd)");
                return false;
            }
        }

        if (setNonBlock(connfd) < 0) {
            close(connfd);
            return false;
        }

        if (N == 0) {
            uint32_t conn_events = EPOLLIN;
            if (_epoller.triggermode == TriggerMode::EdgeTrigger) { conn_events |= EPOLLET; }
            if (_epoller.addfd(connfd, conn_events) < 0) {
                close(connfd);
                return false;
            }
            addConnection(connfd);
        } else {
            subreactor[(lastsub++) % N].connfdque.enqueue(connfd);
        }
    } while (_epoller.triggermode == TriggerMode::EdgeTrigger);

    return true;
}

int EventLoop::mainreacotloop(int N, TriggerMode triggermode) {
    // 主reactor线程，负责accept连接和分发连接到subreactor线程
    this->N = N;
    if (N == 0) { return loop(); } // N=0，不使用subreactor，主reactor线程自己处理所有连接
    subreactor = new EventLoop[N];
    int started = 0; // 已经成功启动的subreactor线程数量,用于出错时清理已经启动的线程
    for (int i = 0; i < N; i++) {
        if (subreactor[i].init(triggermode, -1, _connectionTimeoutMs) < 0) {
            LOG_ERROR("subreactor init failed index=%d", i);
            uint64_t one = 1;
            for (int j = 0; j < started; ++j) {
                subreactor[j]._stop = true;
                ssize_t n = write(subreactor[j]._epoller.wakeupfd, &one, sizeof(one));
                if (n != sizeof(one)) { LOG_ERR("write(subreactor wakeupfd)"); }
            }
            for (auto& t : _threads) {
                if (t.joinable()) { t.join(); }
            }
            delete[] subreactor;
            subreactor = nullptr;
            this->N = 0;
            return -1;
        }
        _threads.emplace_back([this, i] { subreactor[i].loop(); });
        started++;
    }

    return loop();
}

int EventLoop::loop() {
    while (!_stop) {
        int n = _epoller.wait(events, _maxEvents, 1000);
        // 超过1000ms没有事件就返回，进行一次超时连接检查
        if (n < 0) { return -1; }
        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;
            
            if (_listenfd >= 0 && fd == _listenfd) {
                if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                    LOG_ERROR("listen fd error fd=%d events=0x%x", fd, events[i].events);
                    return -1;
                } else if (events[i].events & EPOLLIN) {
                    if (!acceptclient()) { return -1; }
                }
                continue;
            }

            if (fd == _epoller.wakeupfd) {
                uint64_t one;
                while (true) {
                    ssize_t rv = read(_epoller.wakeupfd, &one, sizeof(one));
                    if (rv > 0) { continue; }
                    if (rv < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { break; }
                    if (rv < 0) { LOG_ERR("read(wakeupfd)"); }
                    break;
                }

                std::queue<int> *que = connfdque.dequeue();
                while (!que->empty()) {
                    int connfd = que->front();
                    que->pop();

                    if (setNonBlock(connfd) < 0) {
                        close(connfd);
                        continue;
                    }

                    uint32_t conn_events = EPOLLIN;
                    if (_epoller.triggermode == TriggerMode::EdgeTrigger) {
                        conn_events |= EPOLLET;
                    }
                    if (_epoller.addfd(connfd, conn_events) < 0) {
                        close(connfd);
                        continue;
                    }
                    addConnection(connfd);
                }
                delete que;
                
                ioque.runTasks();
                continue;
            }

            if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                close_connfd(fd, "epoll hup/error");
                continue;
            }

            if (events[i].events & EPOLLIN) {
                auto it = _connections.find(fd);
                if (it == _connections.end()) { continue; }
                if (it->second->handleread() < 0) {
                    close_connfd(fd, "read closed");
                    continue;
                }
            }

            if (events[i].events & EPOLLOUT) {
                auto it = _connections.find(fd);
                if (it == _connections.end()) { continue; }
                if (it->second->handlewrite() < 0) {
                    close_connfd(fd, "write closed");
                    continue;
                }
            }
        }

        closeIdleConnections(); // 检查是否有空闲连接超时需要关闭
    }

    return 0;
}

void EventLoop::close_connfd(int connfd, const char* reason) {
    (void)reason;
    auto it = _connections.find(connfd);
    _epoller.delfd(connfd);
    close(connfd);
    if (it != _connections.end()) { _connections.erase(it); }
}

EventLoop::~EventLoop() {
    if (subreactor != nullptr) {
        stop();
        for (auto& t : _threads) {
            if (t.joinable()) { t.join(); }
        }
        delete[] subreactor;
        subreactor = nullptr;
    }
}

void EventLoop::stop() {
    _stop = true;
    uint64_t one = 1;
    ssize_t n = write(_epoller.wakeupfd, &one, sizeof(one));
    if (n != sizeof(one)) { LOG_ERR("write(main wakeupfd)"); }

    if (subreactor != nullptr) {
        for (int i = 0; i < N; ++i) {
            subreactor[i]._stop = true;
            n = write(subreactor[i]._epoller.wakeupfd, &one, sizeof(one));
            if (n != sizeof(one)) { LOG_ERR("write(subreactor wakeupfd)"); }
        }
    }
}

void EventLoop::addConnection(int connfd) {
    auto conn = std::make_shared<Connection>(connfd, _epoller, pool, ioque);
    _connections[connfd] = std::move(conn);
}

void EventLoop::closeIdleConnections() {
    if (_connectionTimeoutMs <= 0) { return; } // 超时时间小于等于0不检查超时连接

    auto now = std::chrono::steady_clock::now(); // 获取当前时间点
    if (now - _lastTimeoutSweep < std::chrono::seconds(1)) { return; }
    // 距离上次检查超时连接不到1秒就不检查了，避免频繁检查带来的性能消耗
    _lastTimeoutSweep = now;

    std::vector<int> expired;
    expired.reserve(_connections.size());

    for (const auto& [fd, conn] : _connections) {
        if (conn->isTimedOut(now, std::chrono::milliseconds(_connectionTimeoutMs))) {
            expired.push_back(fd);
        }
    }

    for (int fd : expired) { close_connfd(fd, "idle timeout"); }
}

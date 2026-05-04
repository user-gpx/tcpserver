#pragma once

#include "../ThreadPool.h"
#include "../common.h"
#include "../reactor/Epoller.h"
#include "../utils/Buffer.h"
#include "../utils/SocketUtil.h"
#include "HttpParser.h"

class Connection : public std::enable_shared_from_this<Connection> {
    int connfd;
    uint32_t baseevent;
    Epoller& _epoller;
    Buffer inputbuffer;
    Buffer outputbuffer;
    ThreadPool& pool;
    HttpParser parser;
    IoQueueContext& ioque;
    bool _isprocessing;
    std::queue<HttpRequest> _pendingReqs;
    std::chrono::steady_clock::time_point _lastActive;
    bool _closeAfterWrite;//是否在写完响应后关闭连接

  public:
    Connection(int fd, Epoller& epoller, ThreadPool& pool, IoQueueContext& ioque);

    int handleread();
    int handlewrite();
    int try_one_request();
    void processNextSlowRequest();
    void send_response(const std::string& resp);

    void touch()//更新最后活跃时间
     { _lastActive = std::chrono::steady_clock::now(); }

    bool isTimedOut(std::chrono::steady_clock::time_point now,
                    std::chrono::milliseconds timeout) const {
        if (_isprocessing || !_pendingReqs.empty()) { return false; }
        return now - _lastActive >= timeout;
    }

    bool isfastresponse(const HttpRequest& req) const { return true; }

    int fd() const { return connfd; }

    std::string URL(const HttpRequest& req);
    std::string make_response(int code, const std::string& reason, const std::string& content_type,
                              const std::string& body, bool keep_alive);
    std::string read_file(const std::string& path);
    std::string make_home_page(bool keep_alive);
    std::string handle_upload(const HttpRequest& req);
};

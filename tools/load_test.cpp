#include <arpa/inet.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
std::atomic<int> success{0};
std::atomic<int> failed{0};
std::atomic<int> remain{0};
bool one_request(const std::string& host, int port) {
    // 发送一个 HTTP 请求并检查响应是否包含 "HTTP/1.1 200"
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return false;
    }
    std::string req = "GET /login HTTP/1.1\r\n"
                      "Host: " +
                      host +
                      "\r\n"
                      "Connection: close\r\n"
                      "\r\n"; // 请求头中添加 Connection: close，告诉服务器处理完请求后关闭连接
    if (send(fd, req.c_str(), req.size(), 0) < 0) { // 发送 HTTP 请求
        close(fd);
        return false;
    }
    char buf[4096];
    int n = recv(fd, buf, sizeof(buf) - 1, 0); // 接收 HTTP 响应
    close(fd);

    if (n <= 0) return false;

    buf[n] = '\0';
    std::string resp(buf);
    return resp.find("HTTP/1.1 200") != std::string::npos;
}

void worker(const std::string& host, int port) {
    while (true) {
        int old = remain.fetch_sub(1); // 原子地减少 remain 的值，并返回减少前的值
        if (old <= 0) break;
        if (one_request(host, port)) {
            success++;
        } else {
            failed++;
        }
    }
}

int main() {
    std::string host = "127.0.0.1";
    int port = 9006;
    int total_requests = 10000;
    int concurrency = 100; // 并发数
    remain = total_requests;
    auto start = std::chrono::steady_clock::now();

    std::vector<std::thread> threads;
    for (int i = 0; i < concurrency; i++) { threads.emplace_back(worker, host, port); }

    for (auto& t : threads) {
        t.join(); // 等待所有线程完成
    }

    auto end = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count();

    std::cout << "success: " << success << "\n";
    std::cout << "failed: " << failed << "\n";
    std::cout << "time: " << seconds << "s\n";
    std::cout << "qps: " << success / seconds << "\n";

    return 0;
}

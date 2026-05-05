#pragma once
#include "../common.h"
#include "../logs/Logger.h"

#define LOG_ERR(msg)                                                                               \
    do {                                                                                           \
        int _saved_errno = errno;                                                                  \
        Logger::server().logSystemError((msg), _saved_errno, __FILE__, __LINE__, __func__);        \
    } while (0)

#define LOG_ERROR(fmt, ...)                                                                        \
    do {                                                                                           \
        Logger::server().logf(LogLevel::Error, __FILE__, __LINE__, __func__, (fmt),                \
                              ##__VA_ARGS__);                                                      \
    } while (0)

#define LOG_INFO(fmt, ...)                                                                         \
    do {                                                                                           \
        Logger::server().logf(LogLevel::Info, __FILE__, __LINE__, __func__, (fmt), ##__VA_ARGS__); \
    } while (0)

#define LOG_WARN(fmt, ...)                                                                         \
    do {                                                                                           \
        Logger::server().logf(LogLevel::Warn, __FILE__, __LINE__, __func__, (fmt), ##__VA_ARGS__); \
    } while (0)

#define LOG_DEBUG(fmt, ...)                                                                        \
    do {                                                                                           \
        Logger::server().logf(LogLevel::Debug, __FILE__, __LINE__, __func__, (fmt),                \
                              ##__VA_ARGS__);                                                      \
    } while (0)

#define CLIENT_LOG_ERR(msg)                                                                        \
    do {                                                                                           \
        int _saved_errno = errno;                                                                  \
        Logger::client().logSystemError((msg), _saved_errno, __FILE__, __LINE__, __func__);        \
    } while (0)

#define CLIENT_LOG_ERROR(fmt, ...)                                                                 \
    do {                                                                                           \
        Logger::client().logf(LogLevel::Error, __FILE__, __LINE__, __func__, (fmt),                \
                              ##__VA_ARGS__);                                                      \
    } while (0)

#define CLIENT_LOG_INFO(fmt, ...)                                                                  \
    do {                                                                                           \
        Logger::client().logf(LogLevel::Info, __FILE__, __LINE__, __func__, (fmt), ##__VA_ARGS__); \
    } while (0)

#define CLIENT_LOG_WARN(fmt, ...)                                                                  \
    do {                                                                                           \
        Logger::client().logf(LogLevel::Warn, __FILE__, __LINE__, __func__, (fmt), ##__VA_ARGS__); \
    } while (0)

#define CLIENT_LOG_DEBUG(fmt, ...)                                                                 \
    do {                                                                                           \
        Logger::client().logf(LogLevel::Debug, __FILE__, __LINE__, __func__, (fmt),                \
                              ##__VA_ARGS__);                                                      \
    } while (0)

inline int setNonBlock(int fd) { // 设置非阻塞
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        LOG_ERR("fcntl(F_GETFL)");
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        LOG_ERR("fcntl(F_SETFL)");
        return -1;
    }
    return 0;
}
struct IoQueueContext {
    void enqueue(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            queue.emplace(std::move(task));
        }

        uint64_t one = 1;
        ssize_t n = write(wakeupfd, &one, sizeof(one));
        if (n != sizeof(one)) { LOG_ERR("write(wakeupfd)"); }
    }
    void runTasks() {
        // uint64_t one;
        // read(wakeupfd, &one, sizeof(one)); //在外面读
        std::queue<std::function<void()>> tmp;

        {
            std::lock_guard<std::mutex> lock(mutex);
            queue.swap(tmp);
        }

        while (!tmp.empty()) {
            auto task = std::move(tmp.front());
            tmp.pop();
            task();
        }
    };
    void init(int wkupfd) { wakeupfd = wkupfd; }

  private:
    std::mutex mutex;
    std::queue<std::function<void()>> queue;
    int wakeupfd{-1};
};

struct ConnfdQueue {
    void init(int wkupfd) { wakeupfd = wkupfd; }
    void enqueue(int connfd) {
        {
            std::unique_lock<std::mutex> lock(queMutex);
            _connfdque.push(connfd);
        }
        uint64_t one = 1;
        ssize_t n = write(wakeupfd, &one, sizeof(one));
        if (n != sizeof(one)) { LOG_ERR("write(wakeupfd)"); }
    };
    std::queue<int>* dequeue() {
        // uint64_t one;
        // read(wakeupfd, &one, sizeof(one));//在外面读
        std::queue<int>* tmp = new std::queue<int>;
        {
            std::unique_lock<std::mutex> lock(queMutex);
            tmp->swap(_connfdque);
        }
        return tmp;
    };

  private:
    std::queue<int> _connfdque;
    std::mutex queMutex;
    int wakeupfd{-1}; // 需要初始化
};

// struct IoQueueContext {  //无锁实现CAS
//     struct Node {
//         std::function<void()> task;
//         Node* next;
//     };
//     void enqueue(std::function<void()> task) {
//         Node* node = new Node{std::move(task), nullptr};
//         Node* old = head.load(std::memory_order_relaxed);
//         do {
//             node->next = old;
//         } while (!head.compare_exchange_weak(
//             old,
//             node,
//             std::memory_order_release,
//             std::memory_order_relaxed
//         ));
//         uint64_t one = 1;
//         ssize_t n = write(wakeupfd, &one, sizeof(one));
//         if (n != sizeof(one)) {
//             LOG_ERR("write(wakeupfd)");
//         }
//     }
//     void runTasks() {
//         uint64_t one;
//         while (read(wakeupfd, &one, sizeof(one)) > 0) {
//             // 如果 wakeupfd 是非阻塞，这里可以读空
//         }
//         Node* list = head.exchange(nullptr, std::memory_order_acquire);
//         // 因为 enqueue 是头插，所以这里反转，保证大致 FIFO
//         Node* rev = nullptr;
//         while (list) {
//             Node* next = list->next;
//             list->next = rev;
//             rev = list;
//             list = next;
//         }
//         while (rev) {
//             Node* next = rev->next;
//             rev->task();
//             delete rev;
//             rev = next;
//         }
//     }
//     void setWakeupfd(int wkfd) {
//         wakeupfd = wkfd;
//     }
// private:
//     std::atomic<Node*> head{nullptr};
//     int wakeupfd{-1};
//     public:
// };

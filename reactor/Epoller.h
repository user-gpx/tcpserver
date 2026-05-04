#pragma once
#include "../common.h"
#include "sys/eventfd.h"
#include "../utils/SocketUtil.h"

enum class TriggerMode { EdgeTrigger, LevelTrigger };

class Epoller {
    int epfd; // epoll实例

  public:
    int wakeupfd; // 用于其他线程通知本线程
    TriggerMode triggermode; // 触发模式

    Epoller() : epfd(-1), wakeupfd(-1), triggermode(TriggerMode::LevelTrigger) {}
    int create(TriggerMode triggermode = TriggerMode::LevelTrigger);
    int addfd(int fd, uint32_t events); // 向epoll实例添加套接字和监听事件，只能添加一次。
    int delfd(int fd);                  // 删除epoll实例中的套接字
    int modfd(int fd, uint32_t events); // 修改epoll实例中的套接字（修改监听的事件）
    int wait(epoll_event* _event, int _maxEvents, int _timeout); // 等待epoll实例中监听的事件发生
    ~Epoller();
    Epoller(const Epoller&) = delete;
    Epoller& operator=(const Epoller&) = delete;
};

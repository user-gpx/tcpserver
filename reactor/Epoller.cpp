#include "Epoller.h"

int Epoller::create(TriggerMode triggermode) {
    epfd = epoll_create1(0);
    if (epfd < 0) {
        LOG_ERR("epoll_create1()");
        return -1;
    }
    wakeupfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC); //非阻塞
    if (wakeupfd < 0) {
        LOG_ERR("eventfd()");
        return -1;
    }
    this->triggermode = triggermode;

    // 提前把wakeupfd挂进epoll，后续跨线程任务和主从reactor分发都靠它唤醒。
    if (addfd(wakeupfd, EPOLLIN) < 0) {
        close(wakeupfd);
        wakeupfd = -1;
        return -1;
    }
    return epfd;
}

int Epoller::addfd(int fd, uint32_t events) { // 向epoll实例添加套接字和监听事件，只能添加一次。
    epoll_event ev{};
    ev.events = events; // 关注的事件
    ev.data.fd = fd;    // 把fd绑定到事件，当事件发生时可以通过ev.data.fd获取到对应的fd
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        LOG_ERR("epoll_ctl(ADD)");
        return -1;
    }
    return 0;
}

int Epoller::delfd(int fd) { // 删除epoll实例中的套接字
    if (epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr) < 0) {
        LOG_ERR("epoll_ctl(DEL)");
        return -1;
    }
    return 0;
}

int Epoller::modfd(int fd, uint32_t events) { // 修改epoll实例中的套接字（修改监听的事件）
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev) < 0) {
        LOG_ERR("epoll_ctl(MOD)");
        return -1;
    }
    return 0;
}

int Epoller::wait(epoll_event* _event, int _maxEvents, int _timeout) { // 等待epoll实例中监听的事件发生
    int n = epoll_wait(epfd, _event, _maxEvents, _timeout);
    if (n < 0) {
        if (errno == EINTR) {
            return 0;
        } else {
            LOG_ERR("epoll_wait()");
            return -1;
        }
    }
    return n;
}

Epoller::~Epoller() {
    if (epfd >= 0) {
        close(epfd); // 关闭文件
    }
    if (wakeupfd >= 0) {
        close(wakeupfd);
    }
}

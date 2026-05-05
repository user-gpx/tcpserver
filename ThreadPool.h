#pragma once
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

class ThreadPool {
  private:
    std::mutex queueMutex; // 任务临界区的锁
    std::condition_variable condition;
    bool stopped;
    std::vector<std::thread> workers;        // 保存线程
    std::queue<std::function<void()>> tasks; // 保存任务

  public:
    int getSize() const { return workers.size(); }
    explicit ThreadPool(int size) : stopped(false) { // 创建size个线程,消费者
        for (int i = 0; i < size; i++) {
            workers.emplace_back([this]() { // 不移动直接原地创建
                while (true) {
                    std::function<void()> task; // 一个任务
                    {
                        std::unique_lock<std::mutex> lock(queueMutex); // 唯一锁管理器
                        condition.wait(lock, [this] {                  // 捕获this才能用tasks
                            return stopped || !tasks.empty();
                        });
                        if (tasks.empty() && stopped) return; // 停止且没有任务就关闭。
                        task = std::move(tasks.front());      // 直接移动，改动所有权
                        tasks.pop();
                    } // 自动释放锁
                    task();
                }
            });
        }
    }; // 创建size个线程,消费者
    template <class F, class... Args> //...表示参数包，多个参数   生产者
    auto submit(F&& f, Args&&... args) -> std::future<decltype(f(
        args...))> { // 万能引用（必须在模板），配合farword可以保留左右值的属性。高性能避免拷贝的消耗
        using Returntype = decltype(f(args...)); // 推导返回类型
        auto task = std::make_shared<
            std::packaged_task<Returntype()>>( // 共享指针（函数指针）来实现作用域防止内存泄漏。
            std::bind(                         // 用带参函数创建无参函数,函数名是函数指针
                std::forward<F>(f),
                std::forward<Args>(args)...)); // 用farword保留左右值的属性。
        std::future<Returntype> result = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            if (stopped) throw std::runtime_error("enqueue on stopped threadpool");
            tasks.emplace([task]() { (*task)(); }); // 捕获指针引用加一。
        }
        condition.notify_one();
        return result;
    }
    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            // 没人持有锁就加锁，有的话进入等待队列，等有人解锁时内核负责唤醒等待队列中的最前面的。
            stopped = true;
        }
        condition.notify_all();
        for (std::thread& worker : workers)
            if (worker.joinable()) { worker.join(); }
    };
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
};

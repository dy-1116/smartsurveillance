// 并发
// 固定线程数，条件变量调度，有界队列
// ThreadPool 实现（成员A）

#include "ThreadPool.h"

#include <iostream>
#include <stdexcept>

// 构造函数：初始化工作线程池
ThreadPool::ThreadPool(size_t numThreads) : m_stop(false) {
    // 处理默认线程数：自动适配 CPU 核心数
    if (numThreads == 0) {
        numThreads = std::thread::hardware_concurrency();
        // 保底处理：部分平台可能无法获取硬件并发数，默认使用 4 线程
        if (numThreads == 0) {
            numThreads = 4;
        }
    }

    // 启动指定数量的工作线程
    m_workers.reserve(numThreads);
    for (size_t i = 0; i < numThreads; ++i) {
        m_workers.emplace_back(&ThreadPool::worker, this);
    }
}

// 工作线程主函数：从任务队列取任务并执行，直到队列停止且清空
void ThreadPool::worker() {
    while (true) {
        std::function<void()> task;
        // 阻塞等待任务：队列被 stop 且已清空时返回 false，线程退出
        if (!m_tasks.waitAndPop(task)) {
            break;
        }

        // 执行任务，捕获异常避免单个任务崩溃导致整个工作线程退出
        if (task) {
            try {
                task();
            } catch (const std::exception& e) {
                std::cerr << "[ThreadPool] Task execution exception: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[ThreadPool] Unknown exception occurred in task" << std::endl;
            }
        }
    }
}

// 提交任务到线程池（公共模板 enqueue 转发到这里）
void ThreadPool::enqueueImpl(std::function<void()> task) {
    if (m_stop.load()) {
        throw std::runtime_error("Cannot enqueue task on a stopped ThreadPool");
    }
    m_tasks.push(std::move(task));
}

// 停止线程池：停止接收新任务，等已入队任务执行完后回收线程
void ThreadPool::stop() {
    // 原子操作防止重复停止，避免多次 join 线程
    if (m_stop.exchange(true)) {
        return;
    }

    // 通知任务队列停止，唤醒所有等待的工作线程
    m_tasks.stop();

    // 等待所有工作线程正常退出
    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    m_workers.clear();
}

// 析构函数：RAII 自动资源管理，确保线程池正确停止
ThreadPool::~ThreadPool() {
    if (!m_stop.load()) {
        stop();
    }
}

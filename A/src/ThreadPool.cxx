// 并发
// 固定线程数，条件变量调度，有界队列

#include "ThreadPool.h"
#include <iostream>
#include <stdexcept>
#include <functional>

// ThreadPool 构造函数：初始化工作线程池
ThreadPool::ThreadPool(size_t numThreads) : m_stop(false) {
    // 处理默认线程数：自动适配CPU核心数
    if (numThreads == 0) {
        numThreads = std::thread::hardware_concurrency();
        // 保底处理：部分平台可能无法获取硬件并发数，默认使用4线程
        if (numThreads == 0) {
            numThreads = 4;
        }
    }

    // 启动指定数量的工作线程
    for (size_t i = 0; i < numThreads; ++i) {
        m_workers.emplace_back(&ThreadPool::worker, this);
    }
}

// 工作线程主函数：循环从任务队列取任务并执行
void ThreadPool::worker() {
    while (true) {
        std::function<void()> task;
        // 阻塞等待任务，直到有任务可处理
        m_tasks.waitAndPop(task);

        // 如果线程池已停止且队列为空，退出线程
        if (m_stop && m_tasks.empty()) {
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

// 提交任务到线程池
void ThreadPool::enqueue(std::function<void()> task) {
    if (m_stop) {
        throw std::runtime_error("Cannot enqueue task on a stopped ThreadPool");
    }
    // 使用移动语义传递任务，避免std::function的拷贝开销
    m_tasks.push(std::move(task));
}

// 停止线程池，等待所有任务完成后回收线程
void ThreadPool::stop() {
    // 原子操作防止重复停止，避免多次join线程
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

// 析构函数：RAII自动资源管理，确保线程池正确停止
ThreadPool::~ThreadPool() {
    if (!m_stop) {
        stop();
    }
}

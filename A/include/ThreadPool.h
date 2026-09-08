#ifndef THREAD_POOL_H
#define THREAD_POOL_H

// 并发
// 固定线程数，条件变量调度，有界队列
// 成员A独立并发组件（README 4.4 / 5.3）

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// ============ 有界线程安全队列（生产者-消费者） ============
// README 4.4.3 / 5.3：
//  - push：队列满时丢弃最旧任务（视频监控场景旧帧不如新帧有价值，避免内存暴涨）
//  - waitAndPop：消费者阻塞等待；stop 后若队列已空则返回 false（线程可退出）
//  - stop：唤醒所有等待线程，让它们检查停止标志退出
template <typename T>
class ThreadSafeQueue {
public:
    explicit ThreadSafeQueue(size_t maxSize = 10)
        : m_maxSize(maxSize == 0 ? 1 : maxSize), m_stopped(false) {}

    // 生产者：推入任务。有界：满了丢最旧。
    void push(T item) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_stopped) {
                return;  // 已停止，丢弃（正常情况下 enqueue 层已拦截）
            }
            if (m_queue.size() >= m_maxSize) {
                m_queue.pop();  // 丢弃最旧任务，保证新帧能被及时处理
            }
            m_queue.push(std::move(item));
        }
        m_cv.notify_one();  // 唤醒一个等待的消费者
    }

    // 消费者：阻塞等待并取出任务。
    // 返回 true 表示取到任务；返回 false 表示队列已停止且为空（应退出）。
    // 谓词防止虚假唤醒：被唤醒但队列仍空时继续等待。
    bool waitAndPop(T& item) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return !m_queue.empty() || m_stopped; });
        if (m_queue.empty()) {
            return false;  // stopped 且无任务
        }
        item = std::move(m_queue.front());
        m_queue.pop();
        return true;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.empty();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.size();
    }

    // 停止：唤醒所有等待线程使其退出
    void stop() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stopped = true;
        }
        m_cv.notify_all();
    }

private:
    mutable std::mutex              m_mutex;
    std::condition_variable         m_cv;
    std::queue<T>                   m_queue;
    size_t                          m_maxSize;
    bool                            m_stopped;
};

// ============ 固定线程数线程池 ============
// 生产者线程把任务提交到队列，池内固定数量的工作线程消费执行。
// 相比"每路一线程"，线程数固定、上下文切换开销可控（README 4.4.2）。
class ThreadPool {
public:
    // numThreads == 0：自动适配 CPU 核心数（保底 4）
    explicit ThreadPool(size_t numThreads = 0);
    ~ThreadPool();

    // 禁止拷贝
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // 提交任务（支持任意可调用对象，移动语义避免拷贝开销）
    template <typename F>
    void enqueue(F&& f) {
        enqueueImpl(std::function<void()>(std::forward<F>(f)));
    }

    // 停止线程池：等已入队任务执行完后回收所有工作线程（可重复调用）
    void stop();

    size_t workerCount() const { return m_workers.size(); }
    bool   isRunning() const   { return !m_stop.load(); }

private:
    void worker();                          // 工作线程主循环
    void enqueueImpl(std::function<void()> task);

    std::atomic<bool> m_stop;
    std::vector<std::thread> m_workers;
    ThreadSafeQueue<std::function<void()>> m_tasks;   // 有界任务队列
};

#endif // THREAD_POOL_H

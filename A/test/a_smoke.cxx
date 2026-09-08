// A 部分自测骨架：三段冒烟测试
//   1) ThreadPool          —— 并发求和 / stop 后 enqueue 抛异常
//   2) MotionDetector/Tracker —— 合成移动块检测、目标 ID 生命周期
//   3) VideoWorker 端到端 —— 合成本地视频 → 抓帧→检测→报警信号 → 干净退出
//
// 构建运行：
//   cmake -S A -B A/build && cmake --build A/build -j && ./A/build/a_smoke

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QThread>

#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include "MotionDetector.h"
#include "ThreadPool.h"
#include "Tracker.h"
#include "VideoWorker.h"

static int g_fails = 0;

#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) {                                                             \
            std::printf("    [PASS] %s\n", msg);                                \
        } else {                                                                \
            std::printf("    [FAIL] %s   (file %s line %d)\n", msg,             \
                        __FILE__, __LINE__);                                    \
            ++g_fails;                                                          \
        }                                                                       \
    } while (0)

// ---------------- 1) ThreadPool ----------------
static bool testThreadPool()
{
    std::printf("[1] ThreadPool 冒烟测试\n");

    // (a) 容器层：有界队列“满则丢最旧”的语义可确定性验证
    {
        ThreadSafeQueue<int> q(3);
        q.push(1);
        q.push(2);
        q.push(3);
        q.push(4);   // 已满 → 丢弃最旧(1)，为新元素腾位
        CHECK(q.size() == 3, "有界队列满后再 push 不增长");
        int v = -1;
        CHECK(q.waitAndPop(v) && v == 2, "丢最旧而非丢最新（队首是 2）");
        q.stop();    // 让可能阻塞的 waitAndPop 退出（本例已空，安全）
    }

    // (b) 并发求和：分批提交并等批内任务全部执行完再交下一批，保证
    //     任意时刻在途任务数 ≤ 队列容量(10)，不触发“丢旧”→ 求和可精确断言。
    //     4 个线程在批内并发消费（批大小 8 < 10）。
    constexpr int N = 1000, BATCH = 8;
    ThreadPool pool(4);
    std::atomic<int> sum{0};
    std::atomic<int> done{0};
    CHECK(pool.workerCount() == 4, "pool 启动 4 个工作线程");

    for (int lo = 0; lo < N; lo += BATCH) {
        int hi = std::min(N, lo + BATCH);
        for (int i = lo; i < hi; ++i) {
            pool.enqueue([&sum, &done, i] {
                sum.fetch_add(i, std::memory_order_relaxed);
                done.fetch_add(1, std::memory_order_relaxed);
            });
        }
        // 等本批全部执行完（队列清空）再推下一批，确保不溢出有界队列
        while (done.load(std::memory_order_relaxed) < hi) {
            std::this_thread::yield();
        }
    }

    pool.stop();   // 等已入队任务全部执行完
    CHECK(sum.load() == N * (N - 1) / 2,
          "分批入队后 1000 个任务并发求和正确（无丢任务）");

    // stop 后再提交任务必须抛异常
    bool threw = false;
    try {
        pool.enqueue([] {});
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw, "stop 后再 enqueue 抛出 runtime_error");

    // stop() 可重复调用（幂等）
    pool.stop();
    CHECK(true, "重复 stop() 幂等无异常");
    return true;
}

// ---------------- 2) MotionDetector / Tracker ----------------
static bool testDetector()
{
    std::printf("[2] MotionDetector 检测合成运动块\n");
    MotionDetector md(30, 60);
    int detectedFrames = 0;
    const int width = 320, height = 240;

    cv::Mat frame = cv::Mat::zeros(height, width, CV_8UC3);
    // 块每帧位移 8px：帧差区域够宽，不会被 5×5 腐蚀(先腐蚀)整条抹掉
    for (int x = 0; x < width - 60; x += 8) {
        frame.setTo(cv::Scalar(0, 0, 0));
        cv::rectangle(frame, cv::Rect(x, 100, 40, 40),
                      cv::Scalar(220, 220, 220), cv::FILLED);
        if (!md.detect(frame).empty()) {
            ++detectedFrames;
        }
    }
    // 首帧仅缓存；运动块在多数后续帧都能检出
    CHECK(detectedFrames > 5, "多数帧检测到运动目标");
    md.reset();
    CHECK(md.detect(frame).empty(), "reset 后首帧只缓存、不误报");
    return true;
}

static bool testTracker()
{
    std::printf("[2] Tracker 目标 ID 生命周期\n");
    Tracker tr(/*maxLife=*/3, /*minIou=*/0.1, /*maxCentroidDist=*/100.0);

    int firstId = -1;
    // 一个缓慢移动的框：应一直匹配回同一个 ID
    for (int x = 0; x <= 120; x += 5) {
        const auto& objs = tr.update({cv::Rect(x, 10, 40, 40)});
        if (objs.empty()) {
            break;
        }
        if (firstId < 0) {
            firstId = objs[0].id;
        } else {
            CHECK(objs[0].id == firstId, "同一目标跨帧保持同一 ID");
        }
    }
    CHECK(firstId >= 0, "至少分配过一次 ID");

    // 目标连续消失 maxLife 帧后被移除
    for (int i = 0; i < 3; ++i) {
        tr.update({});
    }
    CHECK(tr.empty(), "目标失联 maxLife 帧后被移除");

    // 再次出现 → 分配递增的新 ID
    tr.update({cv::Rect(0, 10, 40, 40)});
    CHECK(tr.objectCount() == 1, "重新出现建立新目标");
    CHECK(tr.objects()[0].id > firstId, "新 ID 严格递增");

    tr.reset();
    CHECK(tr.empty() && tr.nextId() == 0, "reset 清空目标与 ID 计数");
    return true;
}

// ---------------- 3) VideoWorker 端到端 ----------------
static bool testVideoWorker(QCoreApplication* app)
{
    std::printf("[3] VideoWorker 端到端（合成视频）\n");
    const char* videoPath = "part_a_smoke_input.avi";

    // 生成一段 480x320 带移动亮块的视频
    {
        cv::VideoWriter writer;
        writer.open(videoPath, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
                    30, cv::Size(480, 320));
        if (!writer.isOpened()) {
            std::printf("    [SKIP] 无法创建测试视频（编码器不可用），跳过本段\n");
            return true;
        }
        cv::Mat f = cv::Mat::zeros(320, 480, CV_8UC3);
        int stepX = 0;
        for (int i = 0; i < 240; ++i) {
            f.setTo(cv::Scalar(0, 0, 0));
            cv::rectangle(f, cv::Rect(20 + stepX, 120, 40, 40),
                          cv::Scalar(230, 230, 230), cv::FILLED);
            writer.write(f);
            stepX += 12;   // 向右移动（每帧≥12px，帧差区域经腐蚀后仍清晰可测）
            if (stepX > 480 - 60) {
                stepX = 0;   // 回绕，保证全程有运动可测
            }
        }
        writer.release();
    }

    // 构造工作线程并连接信号（本测试把它放在主线程，等价于验证其线程模型）
    auto worker = std::make_unique<VideoWorker>(0, std::string(videoPath));
    worker->setDetectThreshold(30);
    worker->setMinArea(30);          // 检测平面(小图)上的最小面积
    worker->setProcessScale(2.0f);   // 480x320 → 240x160 检测
    worker->setFrameSkip(1);         // 每帧检测，缩短报警等待

    std::atomic<int> frameCount{0};
    std::atomic<int> motionCount{0};
    std::atomic<bool> connectedSeen{false};
    std::atomic<bool> disconnectedSeen{false};

    QObject::connect(worker.get(), &VideoWorker::frameReady, app,
            [&](int, const QImage&) { frameCount.fetch_add(1); });
    QObject::connect(worker.get(), &VideoWorker::motionDetected, app,
            [&](int, const QImage&, const std::vector<cv::Rect>&,
                const std::string& ts) {
                // 时间戳格式需与 MainWindow 解析一致
                if (ts.size() == 19 && ts[4] == '-' && ts[7] == '-' &&
                    ts[13] == ':' && ts[16] == ':') {
                    motionCount.fetch_add(1);
                }
            });
    QObject::connect(worker.get(), &VideoWorker::connectionStateChanged, app,
            [&](int, bool ok, const std::string&) {
                ok ? connectedSeen.store(true)
                   : disconnectedSeen.store(true);
            });

    worker->start();

    // 自旋事件循环，直到触发报警或超时
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 8000 && motionCount.load() == 0) {
        QThread::msleep(10);
        app->processEvents();
    }

    worker->stop();   // 干净退出（内部线程应能及时回收，不挂起）

    bool ok = true;
    ok &= (frameCount.load() > 0);
    ok &= (connectedSeen.load());
    ok &= (motionCount.load() >= 1);
    ok &= (!worker->isRunning());   // stop 后线程确已退出

    std::printf("    frameReady=%d motionDetected=%d connected=%d\n",
                frameCount.load(), motionCount.load(),
                connectedSeen.load() ? 1 : 0);

    CHECK(frameCount.load() > 0, "成功抓到帧并发出 frameReady");
    CHECK(connectedSeen.load(), "读到帧后上报已连接");
    CHECK(motionCount.load() >= 1,
          "持续运动≥1s 后触发 motionDetected（时间戳格式正确）");
    CHECK(!worker->isRunning(), "stop() 后内部采集线程干净退出");

    std::remove(videoPath);   // 清理临时视频
    return ok;
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    std::printf("A 部分自测（Qt + OpenCV）\n");

    testThreadPool();
    testDetector();
    testTracker();
    testVideoWorker(&app);

    if (g_fails == 0) {
        std::printf("\n全部通过 ✓\n");
        return 0;
    }
    std::printf("\n共 %d 项失败 ✗\n", g_fails);
    return 1;
}

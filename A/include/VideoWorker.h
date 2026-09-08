#ifndef VIDEO_WORKER_H
#define VIDEO_WORKER_H

// 视频处理
// 单路视频工作线程：抓帧→检测→追踪→画框→报警→emit 信号
// 成员A（README 4.1 / 4.5 / 4.6 / 4.8 / 5.5）
//
// 线程模型（与 B 侧 CameraManager 的 QThread 宿主协作）：
//  - 本对象被 B moveToThread 到某路 QThread 上；该线程事件循环负责处理
//    stop()/setXxx 等队列调用。
//  - start() 槽内部再启动一个 std::thread 跑采集主循环 run()，从而不阻塞
//    宿主线程的事件循环，保证 stop() 能随时被投递并回收。

#include <QImage>
#include <QMetaType>
#include <QObject>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>

#include "MotionDetector.h"
#include "Tracker.h"

class VideoWorker : public QObject {
    Q_OBJECT

public:
    // url 兼容 RTSP 流地址 / 本地视频文件路径 / USB 设备号（OpenCV 统一处理）
    VideoWorker(int cameraId, std::string url, QObject* parent = nullptr);
    ~VideoWorker() override;

    // ---- 检测参数 setter（由 B 侧在宿主线程以队列 lambda 调用，原子量线程安全）----
    void setDetectThreshold(int value);   // 帧差灰度阈值，默认 30
    void setMinArea(int value);           // 目标最小面积（检测平面像素），默认 150
    void setProcessScale(float value);    // 降采样除数（分辨率缩为 1/value），默认 4
    void setFrameSkip(int value);         // 每 N 帧做一次检测，默认 3

    int  cameraId() const { return m_cameraId; }
    bool isRunning() const { return m_running.load(); }

public slots:
    void start();   // 开启内部采集线程（在宿主线程调用）
    void stop();    // 停止并回收内部采集线程（在宿主线程调用）

signals:
    // 与 B 侧 CameraManager / MainWindow 对接
    void frameReady(int cameraId, const QImage& image);
    void motionDetected(int cameraId, const QImage& snapshot,
                        const std::vector<cv::Rect>& boxes,
                        const std::string& timestamp);
    void connectionStateChanged(int cameraId, bool connected,
                                const std::string& message);

private:
    // cv::Mat(BGR) → QImage(RGB)，必须 .copy() 深拷贝（README 5.5）
    static QImage matToQImage(const cv::Mat& bgr);

    void run();                     // 采集主循环（内部 std::thread 上运行）
    bool openCapture();             // 打开视频源（先设超时属性再 open）
    bool waitBackoff(int attempt);  // 指数退避分段等待；被 stop 时返回 false
    void notifyConnected();
    void notifyDisconnected(const std::string& message);

    void processFrame(cv::Mat& frame);     // 降采样→检测→追踪→画框→报警→emit frameReady
    void drawOverlay(cv::Mat& frame, const std::vector<TrackedObject>& objects,
                     double sx, double sy);
    std::vector<cv::Rect> upscaleBoxes(const std::vector<TrackedObject>& objects,
                                       double sx, double sy) const;

    // 报警状态机（README 4.8.1）：目标持续达标 + 冷却，均只在检测帧上评估
    void updateMotionState(bool hasTarget);
    void maybeTriggerAlarm(const cv::Mat& frame, double sx, double sy);
    void resetAlarmState();

    int         m_cameraId;
    std::string m_url;

    cv::VideoCapture m_capture;   // 仅在采集线程访问
    bool   m_isFiniteFile = false;  // 本地文件(true) vs 实时流(false)，采集线程访问
    double m_totalFrames   = 0.0;

    MotionDetector m_detector;    // 检测平面(降采样后)上工作
    Tracker        m_tracker;

    // ---- 跨线程共享参数：全部原子量 ----
    std::atomic<bool>  m_running  { false };
    std::atomic<int>   m_threshold { 30 };
    std::atomic<int>   m_minArea   { 150 };
    std::atomic<float> m_scale     { 4.0f };
    std::atomic<int>   m_frameSkip { 3 };

    std::thread m_thread;         // start() 启动的内部采集线程
    long long   m_frameCounter = 0;   // 采集线程内帧计数（跳帧用）

    // ---- 报警状态（仅采集线程访问）----
    std::chrono::steady_clock::time_point m_motionSince;
    std::chrono::steady_clock::time_point m_lastTargetTime;
    std::chrono::steady_clock::time_point m_lastAlarmAt;
    bool m_motionActive = false;
};

// 让跨线程队列连接能投递这些参数类型。
// Qt 对 std::vector 提供容器元类型，元素 cv::Rect 声明后即得 std::vector<cv::Rect>；
// std::string 需显式声明（Qt5/Qt6 均不内置）。若在 Qt5 个别版本出现重复声明的编译
// 错误，删除下面 std::string 那行即可（届时它已内置）。
Q_DECLARE_METATYPE(std::string)
Q_DECLARE_METATYPE(cv::Rect)

#endif // VIDEO_WORKER_H

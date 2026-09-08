// 视频处理
// VideoWorker 实现：抓帧→降采样检测→追踪→画框→报警→emit 信号（成员A）
// README 4.1 / 4.5 / 4.6 / 4.8 / 5.5

#include "VideoWorker.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include <QDateTime>

#include <opencv2/imgproc.hpp>

namespace {
constexpr int         kMaxReconnect   = 10;                       // 最大重连次数
constexpr int         kOpenTimeoutMs  = 4000;                     // 打开超时
constexpr int         kReadTimeoutMs  = 4000;                     // 读取超时
constexpr int         kTargetFps      = 15;                       // 目标帧率
const auto            kPersistFor     = std::chrono::milliseconds(1000);  // 目标需持续≥1s
const auto            kCooldown       = std::chrono::milliseconds(5000);  // 报警冷却≥5s
const auto            kMotionGrace    = std::chrono::milliseconds(500);    // 运动中断宽限

const std::string kMsgConnected     = "已连接";
const std::string kMsgDisconnected  = "连接断开，准备重连";
const std::string kMsgReconnectFail = "重连失败，已停止";
const std::string kMsgOpenFail      = "连接失败，已停止重连";
}  // namespace

VideoWorker::VideoWorker(int cameraId, std::string url, QObject* parent)
    : QObject(parent)
    , m_cameraId(cameraId)
    , m_url(std::move(url)) {
    // 自定义类型若不经注册，跨线程队列投递会报 "Cannot queue arguments ..."。
    // 本对象在 B 侧 addCamera 的 connect 之前构造，注册时机满足要求。
    qRegisterMetaType<std::string>("std::string");
    qRegisterMetaType<std::vector<cv::Rect>>("std::vector<cv::Rect>");
}

VideoWorker::~VideoWorker() {
    // 兜底回收：若调用方忘记 stop()，防止采集线程访问已析构成员
    stop();
}

// ---------------- 槽 ----------------

void VideoWorker::start() {
    if (m_running.exchange(true)) {
        return;   // 已在运行
    }
    m_thread = std::thread(&VideoWorker::run, this);
}

void VideoWorker::stop() {
    // 置停止标志并回收采集线程；run() 内的阻塞读受超时属性约束，可在有限时间内退出
    m_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

// ---------------- 参数 setter ----------------

void VideoWorker::setDetectThreshold(int value) { m_threshold = value; }
void VideoWorker::setMinArea(int value)         { m_minArea = value; }
void VideoWorker::setProcessScale(float value)  { m_scale = value; }
void VideoWorker::setFrameSkip(int value)       { m_frameSkip = value; }

// ---------------- 采集主循环 ----------------

// cv::Mat(BGR) → QImage(RGB)。QImage(uchar*,...) 只引用数据不拷贝，
// 局部 Mat 析构后数据失效，必须 .copy() 让 QImage 持有独立内存（README 5.5）。
QImage VideoWorker::matToQImage(const cv::Mat& bgr) {
    if (bgr.empty()) {
        return QImage();
    }

    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);

    // rgb.step 才是真实每行字节数（OpenCV 可能有行对齐填充），不能想当然 width*3
    QImage img(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step),
               QImage::Format_RGB888);
    return img.copy();   // 深拷贝，跨线程安全
}

bool VideoWorker::openCapture() {
    if (m_capture.isOpened()) {
        m_capture.release();
    }

    // 超时属性只在 open 时生效，必须以参数形式传入 open()（README 4.6 / best-effort）
    std::vector<int> params{cv::CAP_PROP_OPEN_TIMEOUT_MSEC, kOpenTimeoutMs,
                            cv::CAP_PROP_READ_TIMEOUT_MSEC, kReadTimeoutMs};
    if (!m_capture.open(m_url, cv::CAP_ANY, params)) {
        return false;
    }

    // 区分本地文件与实时流：文件放完正常结束，不触发重连
    m_totalFrames = m_capture.get(cv::CAP_PROP_FRAME_COUNT);
    m_isFiniteFile = m_totalFrames > 1.0;
    return true;
}

// 指数退避等待：wait = min(3 * 2^(n-1), 60) 秒。
// 分段 100ms 睡眠并在每次醒来检查 m_running，保证 stop 时 100ms 内可响应（README 5.4）。
bool VideoWorker::waitBackoff(int attempt) {
    long long waitMs = 3000LL * (1LL << (attempt - 1));
    waitMs = std::min(waitMs, 60000LL);

    constexpr long long kStepMs = 100;
    for (long long waited = 0; waited < waitMs && m_running.load(); waited += kStepMs) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kStepMs));
    }
    return m_running.load();
}

void VideoWorker::notifyConnected() {
    emit connectionStateChanged(m_cameraId, true, kMsgConnected);
}

void VideoWorker::notifyDisconnected(const std::string& message) {
    emit connectionStateChanged(m_cameraId, false, message);
}

void VideoWorker::run() {
    int  reconnectAttempt = 0;
    bool connected        = false;   // 当前是否处于"已上报连接"状态
    bool downReported     = false;   // 本段断连是否已上报过 false

    while (m_running.load()) {
        // ============ 状态1：尝试打开视频源 ============
        if (!m_capture.isOpened()) {
            if (!openCapture()) {
                ++reconnectAttempt;
                if (reconnectAttempt > kMaxReconnect) {
                    if (!downReported) {
                        notifyDisconnected(kMsgOpenFail);
                        downReported = true;
                    }
                    break;   // 彻底放弃，通知 UI 后退出
                }
                if (!waitBackoff(reconnectAttempt)) {
                    break;   // stop() 请求
                }
                continue;
            }

            // 打开/重连成功：重置检测与追踪缓存（README 4.6.4）。
            // 否则新流第一帧会与旧流最后一帧做差 → 全画面误检；
            // 旧目标 ID 也会与新画面物体错误关联。
            m_detector.reset();
            m_tracker.reset();
            m_frameCounter = 0;
            resetAlarmState();
            continue;
        }

        // ============ 状态2：正常抓帧 ============
        cv::Mat frame;
        if (!m_capture.read(frame)) {
            if (m_isFiniteFile) {
                break;   // 本地视频播放完毕，正常结束（不做重连）
            }

            // 实时流读帧失败 → 关闭并进入指数退避重连
            m_capture.release();
            if (connected && !downReported) {
                connected = false;
                downReported = true;
                notifyDisconnected(kMsgDisconnected);
            }
            ++reconnectAttempt;
            if (reconnectAttempt > kMaxReconnect) {
                if (!downReported) {
                    notifyDisconnected(kMsgReconnectFail);
                    downReported = true;
                }
                break;
            }
            if (!waitBackoff(reconnectAttempt)) {
                break;   // stop() 请求
            }
            continue;
        }

        // 成功读到帧：连接恢复，重置重连计数
        reconnectAttempt = 0;
        if (!connected) {
            connected = true;
            downReported = false;
            notifyConnected();
        }

        // 降采样检测 → 追踪 → 画框 → 报警 → 显示
        processFrame(frame);

        // 帧节奏控制（目标 15fps）
        std::this_thread::sleep_for(std::chrono::milliseconds(1000 / kTargetFps));
    }

    m_capture.release();
    m_running = false;
}

// ---------------- 每帧处理 ----------------

void VideoWorker::processFrame(cv::Mat& frame) {
    if (frame.empty()) {
        return;
    }

    ++m_frameCounter;

    // 1. 降采样：在小图上做检测/追踪，框坐标最后放大回原图（README 4.5.4）
    float scale = m_scale.load();
    if (scale < 1.0f) scale = 1.0f;
    if (scale > 8.0f) scale = 8.0f;

    int smallW = std::max(32, static_cast<int>(std::lround(frame.cols / scale)));
    int smallH = std::max(32, static_cast<int>(std::lround(frame.rows / scale)));
    double sx = static_cast<double>(frame.cols) / static_cast<double>(smallW);
    double sy = static_cast<double>(frame.rows) / static_cast<double>(smallH);

    cv::Mat small;
    cv::resize(frame, small, cv::Size(smallW, smallH), 0, 0, cv::INTER_AREA);

    // 2. 跳帧：每 N 帧做一次完整检测（README 4.5.3）。
    //    用 (counter-1)%N==0：第 1、1+N、1+2N…帧检测；
    //    N==1 时恒成立，即逐帧检测。
    int skip = m_frameSkip.load();
    if (skip < 1) skip = 1;
    bool doDetect = ((m_frameCounter - 1) % skip) == 0;

    const std::vector<TrackedObject>* targets = nullptr;
    if (doDetect) {
        // 把用户实时调节的参数同步给检测器（原子量 → 检测平面参数）
        m_detector.setThreshold(m_threshold.load());
        m_detector.setMinArea(m_minArea.load());

        std::vector<cv::Rect> detections = m_detector.detect(small);
        m_tracker.update(detections);
        targets = &m_tracker.objects();
        updateMotionState(!targets->empty());   // 只在检测帧上评估运动
    } else {
        targets = &m_tracker.objects();         // 直接沿用上一次检测的目标位置
    }

    // 3. 在原分辨率帧上画跟踪框与 ID
    drawOverlay(frame, *targets, sx, sy);

    // 4. 报警判定（快照需带框，故在画框之后触发）
    if (doDetect) {
        maybeTriggerAlarm(frame, sx, sy);
    }

    // 5. 把带框画面发给 UI 线程显示
    emit frameReady(m_cameraId, matToQImage(frame));
}

void VideoWorker::drawOverlay(cv::Mat& frame,
                              const std::vector<TrackedObject>& objects,
                              double sx, double sy) {
    for (const auto& o : objects) {
        // 检测平面坐标 → 原图坐标
        cv::Rect r(cvRound(o.bbox.x * sx), cvRound(o.bbox.y * sy),
                   cvRound(o.bbox.width * sx), cvRound(o.bbox.height * sy));
        cv::rectangle(frame, r, cv::Scalar(0, 255, 0), 2);

        // hitCount < 2 的新目标不画 ID，避免刚出现就闪烁（README 4.3.4）
        if (o.hitCount < 2) {
            continue;
        }
        std::string label = "ID " + std::to_string(o.id);
        int baseline = 0;
        cv::Size textSize = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX,
                                            0.6, 1, &baseline);
        cv::Point org(r.x, r.y - 6);
        if (org.y < 0) {
            org.y = r.y + textSize.height + 6;   // 框贴顶时把标签画到框内下方
        }
        cv::putText(frame, label, org, cv::FONT_HERSHEY_SIMPLEX, 0.6,
                    cv::Scalar(0, 255, 0), 1, cv::LINE_AA);
    }
}

std::vector<cv::Rect> VideoWorker::upscaleBoxes(
    const std::vector<TrackedObject>& objects, double sx, double sy) const {
    std::vector<cv::Rect> boxes;
    boxes.reserve(objects.size());
    for (const auto& o : objects) {
        boxes.push_back(cv::Rect(cvRound(o.bbox.x * sx), cvRound(o.bbox.y * sy),
                                 cvRound(o.bbox.width * sx),
                                 cvRound(o.bbox.height * sy)));
    }
    return boxes;
}

// ---------------- 报警状态机（README 4.8.1） ----------------

void VideoWorker::updateMotionState(bool hasTarget) {
    const auto now = std::chrono::steady_clock::now();
    if (hasTarget) {
        m_lastTargetTime = now;
        if (!m_motionActive) {
            m_motionActive = true;
            m_motionSince  = now;
        }
    } else if (m_motionActive) {
        // 偶尔丢一帧（目标被遮挡）不算运动结束，宽限期后再判停
        if (now - m_lastTargetTime > kMotionGrace) {
            m_motionActive = false;
        }
    }
}

void VideoWorker::maybeTriggerAlarm(const cv::Mat& frame, double sx, double sy) {
    if (!m_motionActive) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    // 1) 目标持续时间 ≥ 1s：避免飞虫/光影等瞬时噪点误报
    if (now - m_motionSince < kPersistFor) {
        return;
    }
    // 2) 报警冷却 ≥ 5s：避免连续运动反复刷屏（首次报警无冷却）
    const bool hasAlarmed = (m_lastAlarmAt.time_since_epoch().count() != 0);
    if (hasAlarmed && now - m_lastAlarmAt < kCooldown) {
        return;
    }

    m_lastAlarmAt = now;

    // 快照 = 当前带框原帧；boxes = 放大回原图坐标的目标框；时间戳与 MainWindow 解析格式一致
    std::vector<cv::Rect> boxes = upscaleBoxes(m_tracker.objects(), sx, sy);
    std::string timestamp = QDateTime::currentDateTime()
                                .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                                .toStdString();

    emit motionDetected(m_cameraId, matToQImage(frame), boxes, timestamp);
}

void VideoWorker::resetAlarmState() {
    m_motionActive = false;
    m_motionSince  = std::chrono::steady_clock::time_point{};
    m_lastTargetTime = std::chrono::steady_clock::time_point{};
    m_lastAlarmAt    = std::chrono::steady_clock::time_point{};
}

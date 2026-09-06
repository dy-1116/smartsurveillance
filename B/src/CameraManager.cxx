// 摄像头管理	
// map管理VideoWorker，信号转发


#include "CameraManager.h"
#include <QDebug>

CameraManager::CameraManager(QObject* parent)
    : QObject(parent) {}

CameraManager::~CameraManager() {
    stopAll();
}

void CameraManager::addCamera(const CameraConfig& config) {
    if (m_cameras.contains(config.id)) {
        qDebug() << "摄像头" << config.id << "已存在，更新配置";
        m_cameras[config.id]->config = config;
        return;
    }

    auto ctx = std::make_unique<CameraContext>();
    ctx->config = config;
    ctx->running = false;

    // 创建工作线程和Worker
    ctx->workerThread = std::make_unique<QThread>();
    ctx->worker = std::make_unique<VideoWorker>(config.id, config.rtspUrl.toStdString());
    ctx->worker->moveToThread(ctx->workerThread.get());

    // 创建录制器
    ctx->recorder = std::make_unique<Recorder>();

    // 连接Worker信号
    connect(ctx->worker.get(), &VideoWorker::frameReady,
            this, &CameraManager::frameReady);
    connect(ctx->worker.get(), &VideoWorker::motionDetected,
            this, &CameraManager::motionDetected);
    connect(ctx->worker.get(), &VideoWorker::connectionStateChanged,
            this, &CameraManager::connectionStateChanged);

    // 连接录制器信号
    connect(ctx->recorder.get(), &Recorder::recordingStarted,
            this, &CameraManager::recordingStarted);
    connect(ctx->recorder.get(), &Recorder::recordingStopped,
            this, &CameraManager::recordingStopped);

    ctx->workerThread->start();
    m_cameras[config.id] = std::move(ctx);
}

void CameraManager::removeCamera(int cameraId) {
    if (!m_cameras.contains(cameraId)) return;
    stopCamera(cameraId);
    auto& ctx = m_cameras[cameraId];
    if (ctx->workerThread) {
        ctx->workerThread->quit();
        ctx->workerThread->wait();
    }
    m_cameras.remove(cameraId);
}

bool CameraManager::startCamera(int cameraId) {
    if (!m_cameras.contains(cameraId)) return false;
    auto& ctx = m_cameras[cameraId];
    if (ctx->running) return true;

    // 更新RTSP地址
    ctx->worker.reset(new VideoWorker(cameraId, ctx->config.rtspUrl.toStdString()));
    ctx->worker->moveToThread(ctx->workerThread.get());

    // 重新连接信号
    connect(ctx->worker.get(), &VideoWorker::frameReady,
            this, &CameraManager::frameReady);
    connect(ctx->worker.get(), &VideoWorker::motionDetected,
            this, &CameraManager::motionDetected);
    connect(ctx->worker.get(), &VideoWorker::connectionStateChanged,
            this, &CameraManager::connectionStateChanged);

    QMetaObject::invokeMethod(ctx->worker.get(), "start", Qt::QueuedConnection);
    ctx->running = true;
    return true;
}

void CameraManager::stopCamera(int cameraId) {
    if (!m_cameras.contains(cameraId)) return;
    auto& ctx = m_cameras[cameraId];
    if (!ctx->running) return;

    if (ctx->recorder && ctx->recorder->isRecording()) {
        ctx->recorder->stopRecording();
    }

    QMetaObject::invokeMethod(ctx->worker.get(), "stop", Qt::BlockingQueuedConnection);
    ctx->running = false;
}

void CameraManager::startAll() {
    for (auto it = m_cameras.begin(); it != m_cameras.end(); ++it) {
        if (it.value()->config.autoConnect) {
            startCamera(it.key());
        }
    }
}

void CameraManager::stopAll() {
    for (auto it = m_cameras.begin(); it != m_cameras.end(); ++it) {
        stopCamera(it.key());
    }
}

bool CameraManager::startRecording(int cameraId, int width, int height, double fps) {
    if (!m_cameras.contains(cameraId)) return false;
    auto& ctx = m_cameras[cameraId];
    return ctx->recorder->startRecording(cameraId, ctx->config.name, width, height, fps);
}

void CameraManager::stopRecording(int cameraId) {
    if (!m_cameras.contains(cameraId)) return;
    m_cameras[cameraId]->recorder->stopRecording();
}

CameraConfig CameraManager::getCameraConfig(int cameraId) const {
    if (m_cameras.contains(cameraId)) {
        return m_cameras[cameraId]->config;
    }
    return CameraConfig{-1, "", "", false};
}

QList<int> CameraManager::getAllCameraIds() const {
    return m_cameras.keys();
}

void CameraManager::setDetectParams(int cameraId, int threshold, int minArea, float scale, int skip) {
    if (!m_cameras.contains(cameraId)) return;
    auto& worker = m_cameras[cameraId]->worker;
    QMetaObject::invokeMethod(worker.get(), [=]() {
        worker->setDetectThreshold(threshold);
        worker->setMinArea(minArea);
        worker->setProcessScale(scale);
        worker->setFrameSkip(skip);
    }, Qt::QueuedConnection);
}

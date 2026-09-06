#ifndef CAMERA_MANAGER_H
#define CAMERA_MANAGER_H

#include <QObject>
#include <QMap>
#include <QString>
#include <memory>
#include "VideoWorker.h"
#include "Recorder.h"

// 摄像头配置
struct CameraConfig {
    int id;
    QString name;
    QString rtspUrl;
    bool autoConnect;
};

// 摄像头管理器：统一管理多路摄像头生命周期（成员B负责）
class CameraManager : public QObject {
    Q_OBJECT

public:
    explicit CameraManager(QObject* parent = nullptr);
    ~CameraManager();

    // 添加摄像头配置
    void addCamera(const CameraConfig& config);

    // 移除摄像头
    void removeCamera(int cameraId);

    // 启动/停止指定摄像头
    bool startCamera(int cameraId);
    void stopCamera(int cameraId);

    // 启动/停止所有摄像头
    void startAll();
    void stopAll();

    // 开始/停止录制
    bool startRecording(int cameraId, int width, int height, double fps);
    void stopRecording(int cameraId);

    // 获取摄像头配置
    CameraConfig getCameraConfig(int cameraId) const;
    QList<int> getAllCameraIds() const;
    int cameraCount() const { return m_cameras.size(); }

    // 设置检测参数
    void setDetectParams(int cameraId, int threshold, int minArea, float scale, int skip);

signals:
    void frameReady(int cameraId, const QImage& image);
    void motionDetected(int cameraId, const QImage& snapshot,
                        const std::vector<cv::Rect>& boxes, const std::string& timestamp);
    void connectionStateChanged(int cameraId, bool connected, const std::string& message);
    void recordingStarted(int cameraId, const QString& filePath);
    void recordingStopped(int cameraId, const QString& filePath,
                          const QDateTime& startTime, const QDateTime& endTime, qint64 fileSize);

private:
    struct CameraContext {
        CameraConfig config;
        std::unique_ptr<VideoWorker> worker;
        std::unique_ptr<Recorder> recorder;
        std::unique_ptr<QThread> workerThread;
        bool running;
    };

    QMap<int, std::unique_ptr<CameraContext>> m_cameras;
};

#endif // CAMERA_MANAGER_H

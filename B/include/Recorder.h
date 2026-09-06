#ifndef RECORDER_H
#define RECORDER_H

#include <QObject>
#include <opencv2/opencv.hpp>
#include <QDateTime>
#include <QString>
#include <mutex>

// MP4视频录制器（成员B负责）
class Recorder : public QObject {
    Q_OBJECT

public:
    explicit Recorder(QObject* parent = nullptr);
    ~Recorder();

    // 开始录制
    bool startRecording(int cameraId, const QString& cameraName,
                        int width, int height, double fps,
                        const QString& outputDir = "recordings");

    // 写入一帧
    void writeFrame(const cv::Mat& frame);

    // 停止录制
    void stopRecording();

    bool isRecording() const { return m_recording; }
    QString getCurrentFilePath() const { return m_filePath; }

signals:
    void recordingStarted(int cameraId, const QString& filePath);
    void recordingStopped(int cameraId, const QString& filePath,
                          const QDateTime& startTime, const QDateTime& endTime,
                          qint64 fileSize);

private:
    cv::VideoWriter m_writer;
    std::mutex m_mutex;
    bool m_recording;
    int m_cameraId;
    QString m_cameraName;
    QString m_filePath;
    QDateTime m_startTime;
};

#endif // RECORDER_H

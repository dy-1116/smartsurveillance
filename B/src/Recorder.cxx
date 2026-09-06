#include "Recorder.h"
#include <QDir>
#include <QFileInfo>
#include <QDebug>

Recorder::Recorder(QObject* parent)
    : QObject(parent), m_recording(false), m_cameraId(-1) {}

Recorder::~Recorder() {
    stopRecording();
}

bool Recorder::startRecording(int cameraId, const QString& cameraName,
                               int width, int height, double fps,
                               const QString& outputDir) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_recording) return false;

    // 确保输出目录存在
    QDir dir(outputDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    // 生成文件名：摄像头名_时间戳.mp4
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString safeName = cameraName.replace("/", "_").replace(":", "_");
    m_filePath = QString("%1/%2_%3.mp4").arg(outputDir, safeName, timestamp);

    // 使用H.264编码（MP4V兼容性更好）
    int fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
    if (!m_writer.open(m_filePath.toStdString(), fourcc, fps,
                       cv::Size(width, height), true)) {
        qDebug() << "录制器打开失败:" << m_filePath;
        return false;
    }

    m_cameraId = cameraId;
    m_cameraName = cameraName;
    m_startTime = QDateTime::currentDateTime();
    m_recording = true;

    emit recordingStarted(cameraId, m_filePath);
    return true;
}

void Recorder::writeFrame(const cv::Mat& frame) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_recording || !m_writer.isOpened()) return;
    if (frame.empty()) return;
    m_writer.write(frame);
}

void Recorder::stopRecording() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_recording) return;

    m_writer.release();
    m_recording = false;

    QDateTime endTime = QDateTime::currentDateTime();

    // 获取文件大小
    qint64 fileSize = 0;
    QFileInfo fileInfo(m_filePath);
    if (fileInfo.exists()) {
        fileSize = fileInfo.size();
    }

    QString filePath = m_filePath;
    int cameraId = m_cameraId;
    QDateTime startTime = m_startTime;

    emit recordingStopped(cameraId, filePath, startTime, endTime, fileSize);
}

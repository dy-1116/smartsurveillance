#ifndef VIDEO_WIDGET_H
#define VIDEO_WIDGET_H

#include <QWidget>
#include <QImage>
#include <QTimer>
#include <QPainter>
#include <QString>
#include <QMouseEvent>

// 自定义视频显示控件（成员B负责）
// 支持多状态渲染：连接中/已连接/断开/报警闪烁/录制中
class VideoWidget : public QWidget {
    Q_OBJECT

public:
    enum ConnectionState {
        Disconnected,
        Connecting,
        Connected,
        Error
    };

    explicit VideoWidget(int cameraId, QWidget* parent = nullptr);
    ~VideoWidget();

    // 设置显示的帧
    void setFrame(const QImage& image);

    // 设置连接状态
    void setConnectionState(ConnectionState state, const QString& message = "");

    // 设置摄像头名称
    void setCameraName(const QString& name) { m_cameraName = name; update(); }

    // 开始/停止报警闪烁
    void startAlarmBlink();
    void stopAlarmBlink();

    // 开始/停止录制指示
    void setRecording(bool recording) { m_recording = recording; update(); }

    // 全屏切换
    void toggleFullscreen();

    int cameraId() const { return m_cameraId; }
    ConnectionState connectionState() const { return m_state; }

signals:
    void doubleClicked(int cameraId);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private slots:
    void onBlinkTimer();

private:
    void drawPlaceholder(QPainter& painter);
    void drawStatusOverlay(QPainter& painter);

    int m_cameraId;
    QString m_cameraName;
    QImage m_currentFrame;
    ConnectionState m_state;
    QString m_statusMessage;

    // 报警闪烁
    QTimer* m_blinkTimer;
    bool m_blinkOn;
    int m_blinkCount;

    // 录制状态
    bool m_recording;

    // 全屏
    bool m_isFullscreen;
};

#endif // VIDEO_WIDGET_H

// UI	
// 状态切换、报警闪烁、双击全屏


#include "VideoWidget.h"
#include <QMouseEvent>
#include <QDateTime>
VideoWidget::VideoWidget(int cameraId, QWidget* parent)
    : QWidget(parent)
    , m_cameraId(cameraId)
    , m_cameraName(QString("摄像头%1").arg(cameraId))
    , m_state(Disconnected)
    , m_blinkOn(false)
    , m_blinkCount(0)
    , m_recording(false)
    , m_isFullscreen(false) {
    setMinimumSize(320, 240);
    setStyleSheet("background-color: #1a1a2e; border: 1px solid #16213e; border-radius: 4px;");

    m_blinkTimer = new QTimer(this);
    m_blinkTimer->setInterval(300);
    connect(m_blinkTimer, &QTimer::timeout, this, &VideoWidget::onBlinkTimer);
}

VideoWidget::~VideoWidget() {
    if (m_blinkTimer->isActive()) m_blinkTimer->stop();
}

void VideoWidget::setFrame(const QImage& image) {
    if (image.isNull()) return;
    m_currentFrame = image;
    update();
}

void VideoWidget::setConnectionState(ConnectionState state, const QString& message) {
    m_state = state;
    m_statusMessage = message;
    if (state != Connected) {
        m_currentFrame = QImage();
    }
    update();
}

void VideoWidget::startAlarmBlink() {
    m_blinkCount = 0;
    m_blinkOn = true;
    m_blinkTimer->start();
}

void VideoWidget::stopAlarmBlink() {
    m_blinkTimer->stop();
    m_blinkOn = false;
    update();
}

void VideoWidget::onBlinkTimer() {
    m_blinkOn = !m_blinkOn;
    m_blinkCount++;
    if (m_blinkCount >= 10) { // 闪烁5秒后停止
        stopAlarmBlink();
        return;
    }
    update();
}

void VideoWidget::toggleFullscreen() {
    if (m_isFullscreen) {
        setWindowFlags(Qt::Widget);
        showNormal();
        m_isFullscreen = false;
    } else {
        setWindowFlags(Qt::Window);
        showFullScreen();
        m_isFullscreen = true;
    }
}

void VideoWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // 绘制视频帧
    if (!m_currentFrame.isNull() && m_state == Connected) {
        QImage scaled = m_currentFrame.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        int x = (width() - scaled.width()) / 2;
        int y = (height() - scaled.height()) / 2;
        painter.drawImage(x, y, scaled);
    } else {
        drawPlaceholder(painter);
    }

    // 报警闪烁边框
    if (m_blinkOn) {
        painter.setPen(QPen(QColor(255, 50, 50), 4));
        painter.drawRect(rect().adjusted(2, 2, -2, -2));
    }

    // 状态叠加层
    drawStatusOverlay(painter);
}

void VideoWidget::drawPlaceholder(QPainter& painter) {
    painter.fillRect(rect(), QColor(26, 26, 46));

    // 绘制图标（用文字代替）
    painter.setPen(QColor(100, 100, 140));
    QFont font = painter.font();
    font.setPointSize(48);
    painter.setFont(font);

    QString iconText;
    switch (m_state) {
        case Disconnected: iconText = "📷"; break;
        case Connecting:   iconText = "⏳"; break;
        case Connected:    iconText = ""; break;
        case Error:        iconText = "⚠"; break;
    }

    if (!iconText.isEmpty()) {
        painter.drawText(rect(), Qt::AlignCenter, iconText);
    }

    // 状态文字
    font.setPointSize(12);
    painter.setFont(font);
    painter.setPen(QColor(180, 180, 200));

    QString statusText;
    switch (m_state) {
        case Disconnected: statusText = "未连接"; break;
        case Connecting:   statusText = m_statusMessage.isEmpty() ? "连接中..." : m_statusMessage; break;
        case Connected:    statusText = ""; break;
        case Error:        statusText = m_statusMessage.isEmpty() ? "连接错误" : m_statusMessage; break;
    }

    if (!statusText.isEmpty()) {
        QRect textRect = rect().adjusted(0, height() / 2 + 40, 0, -20);
        painter.drawText(textRect, Qt::AlignCenter, statusText);
    }
}

void VideoWidget::drawStatusOverlay(QPainter& painter) {
    // 顶部信息栏：摄像头名称
    painter.fillRect(QRect(0, 0, width(), 28), QColor(0, 0, 0, 150));
    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setPointSize(10);
    painter.setFont(font);
    painter.drawText(QRect(8, 0, width() - 80, 28), Qt::AlignVCenter | Qt::AlignLeft, m_cameraName);

    // 录制指示（右上角红点）
    if (m_recording) {
        painter.setBrush(QColor(255, 50, 50));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(width() - 60, 9, 10, 10);
        painter.setPen(Qt::white);
        painter.drawText(QRect(width() - 48, 0, 40, 28), Qt::AlignVCenter | Qt::AlignLeft, "REC");
    }

    // 连接状态指示灯
    QColor indicatorColor;
    switch (m_state) {
        case Connected:    indicatorColor = QColor(50, 200, 50); break;
        case Connecting:   indicatorColor = QColor(255, 200, 50); break;
        case Disconnected: indicatorColor = QColor(120, 120, 120); break;
        case Error:        indicatorColor = QColor(255, 50, 50); break;
    }
    painter.setBrush(indicatorColor);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(width() - 20, 10, 8, 8);

    // 底部时间戳
    if (m_state == Connected && !m_currentFrame.isNull()) {
        painter.fillRect(QRect(0, height() - 24, width(), 24), QColor(0, 0, 0, 150));
        painter.setPen(QColor(200, 200, 200));
        painter.drawText(QRect(8, height() - 24, width() - 16, 24),
                         Qt::AlignVCenter | Qt::AlignRight,
                         QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
    }
}

void VideoWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emit doubleClicked(m_cameraId);
    }
    QWidget::mouseDoubleClickEvent(event);
}

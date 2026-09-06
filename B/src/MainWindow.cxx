// UI	
// 九宫格布局、控制面板、参数调节、报警响应

#include "MainWindow.h"
#include <QApplication>
#include <QMessageBox>
#include <QFileDialog>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QDebug>
#include <QCloseEvent>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_cameraManager(nullptr)
    , m_dbManager(nullptr)
    , m_activeCameraCount(0)
    , m_totalAlarmCount(0)
    , m_currentFullscreenCamera(-1)
    , m_isRecording(false) {
    setWindowTitle("多路智能视频监控系统");
    resize(1400, 900);

    // 初始化数据库
    m_dbManager = new DatabaseManager(this);
    if (!m_dbManager->init()) {
        QMessageBox::warning(this, "警告", "数据库初始化失败，报警记录将无法保存");
    }

    // 初始化摄像头管理器
    m_cameraManager = new CameraManager(this);

    setupUI();
    setupConnections();
    initCameras();

    // 状态栏定时更新
    m_statusTimer = new QTimer(this);
    m_statusTimer->setInterval(1000);
    connect(m_statusTimer, &QTimer::timeout, this, &MainWindow::updateStatusBar);
    m_statusTimer->start();
}

MainWindow::~MainWindow() {}

void MainWindow::setupUI() {
    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);

    QVBoxLayout* mainLayout = new QVBoxLayout(m_centralWidget);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // 主分割器：左侧视频区 + 右侧面板
    m_mainSplitter = new QSplitter(Qt::Horizontal, this);
    mainLayout->addWidget(m_mainSplitter);

    // 视频区域
    m_videoContainer = new QWidget();
    m_videoGrid = new QGridLayout(m_videoContainer);
    m_videoGrid->setSpacing(4);
    m_videoGrid->setContentsMargins(0, 0, 0, 0);
    m_mainSplitter->addWidget(m_videoContainer);

    // 侧边面板
    m_sidePanel = new QWidget();
    m_sidePanel->setFixedWidth(320);
    QVBoxLayout* sideLayout = new QVBoxLayout(m_sidePanel);
    sideLayout->setContentsMargins(4, 4, 4, 4);

    m_sideTabs = new QTabWidget();
    m_alarmList = new QListWidget();
    m_alarmList->setStyleSheet("QListWidget { font-size: 12px; }");
    m_recordList = new QListWidget();
    m_recordList->setStyleSheet("QListWidget { font-size: 12px; }");

    m_sideTabs->addTab(m_alarmList, "报警记录");
    m_sideTabs->addTab(m_recordList, "录制文件");
    sideLayout->addWidget(m_sideTabs);

    m_mainSplitter->addWidget(m_sidePanel);
    m_mainSplitter->setStretchFactor(0, 1);
    m_mainSplitter->setStretchFactor(1, 0);

    setupToolbar();
    setupVideoGrid();

    // 状态栏
    m_statusLabel = new QLabel("就绪");
    m_cameraCountLabel = new QLabel("在线: 0/0");
    m_alarmCountLabel = new QLabel("报警: 0");
    statusBar()->addWidget(m_statusLabel);
    statusBar()->addPermanentWidget(m_cameraCountLabel);
    statusBar()->addPermanentWidget(m_alarmCountLabel);
}

void MainWindow::setupToolbar() {
    m_toolBar = addToolBar("主工具栏");
    m_toolBar->setMovable(false);
    m_toolBar->setIconSize(QSize(20, 20));

    m_actionStartAll = m_toolBar->addAction("全部连接");
    m_actionStopAll = m_toolBar->addAction("全部断开");
    m_toolBar->addSeparator();
    m_actionRecord = m_toolBar->addAction("开始录制");
    m_toolBar->addSeparator();
    m_actionRefresh = m_toolBar->addAction("刷新记录");

    // 样式
    m_toolBar->setStyleSheet(
        "QToolBar { background-color: #1a1a2e; padding: 4px; spacing: 8px; }"
        "QToolButton { color: #e0e0e0; padding: 6px 12px; border-radius: 4px; }"
        "QToolButton:hover { background-color: #16213e; }"
    );
}

void MainWindow::setupVideoGrid() {
    // 创建3x3九宫格视频控件
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            int cameraId = row * 3 + col;
            VideoWidget* widget = new VideoWidget(cameraId, this);
            widget->setCameraName(QString("摄像头%1").arg(cameraId + 1));
            m_videoWidgets[cameraId] = widget;
            m_videoGrid->addWidget(widget, row, col);
        }
    }
}

void MainWindow::setupConnections() {
    // 摄像头管理器信号
    connect(m_cameraManager, &CameraManager::frameReady,
            this, &MainWindow::onFrameReady);
    connect(m_cameraManager, &CameraManager::motionDetected,
            this, &MainWindow::onMotionDetected);
    connect(m_cameraManager, &CameraManager::connectionStateChanged,
            this, &MainWindow::onConnectionStateChanged);
    connect(m_cameraManager, &CameraManager::recordingStarted,
            this, &MainWindow::onRecordingStarted);
    connect(m_cameraManager, &CameraManager::recordingStopped,
            this, &MainWindow::onRecordingStopped);

    // 工具栏
    connect(m_actionStartAll, &QAction::triggered, this, &MainWindow::onStartAll);
    connect(m_actionStopAll, &QAction::triggered, this, &MainWindow::onStopAll);
    connect(m_actionRecord, &QAction::triggered, this, &MainWindow::onToggleRecord);
    connect(m_actionRefresh, &QAction::triggered, this, &MainWindow::onShowAlarmRecords);

    // 视频控件双击
    for (auto it = m_videoWidgets.begin(); it != m_videoWidgets.end(); ++it) {
        connect(it.value(), &VideoWidget::doubleClicked,
                this, &MainWindow::onVideoDoubleClicked);
    }

    // 报警列表点击
    connect(m_alarmList, &QListWidget::itemClicked,
            this, &MainWindow::onAlarmItemClicked);
}

void MainWindow::initCameras() {
    // 初始化9路摄像头配置（实际使用时从配置文件或数据库读取）
    for (int i = 0; i < 9; ++i) {
        CameraConfig config;
        config.id = i;
        config.name = QString("摄像头%1").arg(i + 1);
        // 示例RTSP地址，实际使用时替换为真实地址
        config.rtspUrl = QString("rtsp://admin:password@192.168.1.%1:554/stream1").arg(100 + i);
        config.autoConnect = false;
        m_cameraManager->addCamera(config);

        // 存入数据库
        m_dbManager->upsertCamera(i, config.name, config.rtspUrl);
    }

    // 加载历史报警记录
    onShowAlarmRecords();
    onShowRecordFiles();
}

void MainWindow::onFrameReady(int cameraId, const QImage& image) {
    if (m_videoWidgets.contains(cameraId)) {
        m_videoWidgets[cameraId]->setFrame(image);
    }
}

void MainWindow::onMotionDetected(int cameraId, const QImage& snapshot,
                                    const std::vector<cv::Rect>& boxes,
                                    const std::string& timestamp) {
    Q_UNUSED(boxes);

    // 触发报警闪烁
    if (m_videoWidgets.contains(cameraId)) {
        m_videoWidgets[cameraId]->startAlarmBlink();
    }

    // 保存快照
    QString qTimestamp = QString::fromStdString(timestamp);
    saveSnapshot(cameraId, snapshot, qTimestamp);

    // 存入数据库
    QString cameraName = m_cameraManager->getCameraConfig(cameraId).name;
    QString snapshotPath = QString("snapshots/cam%1_%2.jpg")
                               .arg(cameraId)
                               .arg(qTimestamp.replace(" ", "_").replace(":", "-"));
    QString desc = QString("检测到%1个运动目标").arg(boxes.size());
    m_dbManager->insertAlarm(cameraId, cameraName,
                              QDateTime::fromString(qTimestamp, "yyyy-MM-dd HH:mm:ss"),
                              snapshotPath, "", desc);

    m_totalAlarmCount++;
    onShowAlarmRecords();

    m_statusLabel->setText(QString("报警: 摄像头%1 检测到运动目标").arg(cameraId + 1));
}

void MainWindow::onConnectionStateChanged(int cameraId, bool connected, const std::string& message) {
    if (!m_videoWidgets.contains(cameraId)) return;

    VideoWidget::ConnectionState state;
    if (connected) {
        state = VideoWidget::Connected;
        m_activeCameraCount++;
    } else {
        state = VideoWidget::Connecting;
        if (m_activeCameraCount > 0) m_activeCameraCount--;
    }

    m_videoWidgets[cameraId]->setConnectionState(state, QString::fromStdString(message));
}

void MainWindow::onRecordingStarted(int cameraId, const QString& filePath) {
    Q_UNUSED(filePath);
    if (m_videoWidgets.contains(cameraId)) {
        m_videoWidgets[cameraId]->setRecording(true);
    }
    m_statusLabel->setText(QString("摄像头%1 开始录制").arg(cameraId + 1));
}

void MainWindow::onRecordingStopped(int cameraId, const QString& filePath,
                                      const QDateTime& startTime, const QDateTime& endTime,
                                      qint64 fileSize) {
    if (m_videoWidgets.contains(cameraId)) {
        m_videoWidgets[cameraId]->setRecording(false);
    }

    // 存入数据库
    QString cameraName = m_cameraManager->getCameraConfig(cameraId).name;
    m_dbManager->insertRecord(cameraId, cameraName, startTime, endTime, filePath, fileSize);

    onShowRecordFiles();
    m_statusLabel->setText(QString("摄像头%1 录制结束").arg(cameraId + 1));
}

void MainWindow::onStartAll() {
    m_cameraManager->startAll();
    m_statusLabel->setText("正在连接所有摄像头...");
}

void MainWindow::onStopAll() {
    m_cameraManager->stopAll();
    for (auto it = m_videoWidgets.begin(); it != m_videoWidgets.end(); ++it) {
        it.value()->setConnectionState(VideoWidget::Disconnected, "已断开");
    }
    m_activeCameraCount = 0;
    m_statusLabel->setText("已断开所有摄像头");
}

void MainWindow::onToggleRecord() {
    if (!m_isRecording) {
        // 对所有在线摄像头开始录制
        bool started = false;
        for (int id : m_cameraManager->getAllCameraIds()) {
            if (m_videoWidgets.contains(id) &&
                m_videoWidgets[id]->connectionState() == VideoWidget::Connected) {
                m_cameraManager->startRecording(id, 1280, 720, 25.0);
                started = true;
            }
        }
        if (started) {
            m_isRecording = true;
            m_actionRecord->setText("停止录制");
        }
    } else {
        for (int id : m_cameraManager->getAllCameraIds()) {
            m_cameraManager->stopRecording(id);
        }
        m_isRecording = false;
        m_actionRecord->setText("开始录制");
    }
}

void MainWindow::onShowAlarmRecords() {
    m_alarmList->clear();
    auto alarms = m_dbManager->queryAlarms(-1, QDateTime(), QDateTime(), 50);
    for (const auto& alarm : alarms) {
        QString text = QString("[%1] %2 - %3")
                           .arg(alarm.alarmTime.toString("MM-dd HH:mm:ss"))
                           .arg(alarm.cameraName)
                           .arg(alarm.description);
        QListWidgetItem* item = new QListWidgetItem(text);
        item->setData(Qt::UserRole, alarm.id);
        item->setData(Qt::UserRole + 1, alarm.snapshotPath);
        if (!alarm.description.isEmpty()) {
            item->setForeground(QColor(255, 100, 100));
        }
        m_alarmList->addItem(item);
    }
}

void MainWindow::onShowRecordFiles() {
    m_recordList->clear();
    auto records = m_dbManager->queryRecords(-1, 50);
    for (const auto& rec : records) {
        QString sizeStr;
        if (rec.fileSize > 1024 * 1024) {
            sizeStr = QString("%1MB").arg(rec.fileSize / (1024 * 1024));
        } else {
            sizeStr = QString("%1KB").arg(rec.fileSize / 1024);
        }
        QString text = QString("[%1] %2 (%3)")
                           .arg(rec.startTime.toString("MM-dd HH:mm"))
                           .arg(rec.cameraName)
                           .arg(sizeStr);
        QListWidgetItem* item = new QListWidgetItem(text);
        item->setData(Qt::UserRole, rec.id);
        item->setData(Qt::UserRole + 1, rec.filePath);
        m_recordList->addItem(item);
    }
}

void MainWindow::onVideoDoubleClicked(int cameraId) {
    if (m_currentFullscreenCamera == cameraId) {
        // 退出全屏：恢复所有控件显示
        for (auto it = m_videoWidgets.begin(); it != m_videoWidgets.end(); ++it) {
            it.value()->show();
        }
        m_currentFullscreenCamera = -1;
    } else {
        // 进入全屏：隐藏其他控件
        for (auto it = m_videoWidgets.begin(); it != m_videoWidgets.end(); ++it) {
            if (it.key() != cameraId) {
                it.value()->hide();
            }
        }
        m_currentFullscreenCamera = cameraId;
    }
}

void MainWindow::onAlarmItemClicked(QListWidgetItem* item) {
    QString snapshotPath = item->data(Qt::UserRole + 1).toString();
    if (QFile::exists(snapshotPath)) {
        QImage img(snapshotPath);
        if (!img.isNull()) {
            // 可以弹出预览窗口，这里简单显示在状态栏
            m_statusLabel->setText(QString("快照: %1").arg(snapshotPath));
        }
    }
}

void MainWindow::saveSnapshot(int cameraId, const QImage& image, const QString& timestamp) {
    QDir dir("snapshots");
    if (!dir.exists()) dir.mkpath(".");

    QString fileName = QString("snapshots/cam%1_%2.jpg")
                           .arg(cameraId)
                           .arg(timestamp).replace(" ", "_").replace(":", "-");
    image.save(fileName, "JPG", 85);
}

void MainWindow::updateStatusBar() {
    int total = m_cameraManager->cameraCount();
    m_cameraCountLabel->setText(QString("在线: %1/%2").arg(m_activeCameraCount).arg(total));
    m_alarmCountLabel->setText(QString("报警: %1").arg(m_totalAlarmCount));
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_isRecording) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, "确认退出", "正在录制中，确定要退出吗？",
            QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::No) {
            event->ignore();
            return;
        }
    }
    m_cameraManager->stopAll();
    event->accept();
}

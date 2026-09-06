#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>
#include <QGridLayout>
#include <QMap>
#include <QListWidget>
#include <QSplitter>
#include <QToolBar>
#include <QStatusBar>
#include <QLabel>
#include <QTimer>
#include <QTabWidget>
#include "VideoWidget.h"
#include "CameraManager.h"
#include "DatabaseManager.h"

// 主窗口：3x3九宫格监控界面（成员B负责）
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    // 视频帧处理
    void onFrameReady(int cameraId, const QImage& image);
    void onMotionDetected(int cameraId, const QImage& snapshot,
                           const std::vector<cv::Rect>& boxes, const std::string& timestamp);
    void onConnectionStateChanged(int cameraId, bool connected, const std::string& message);

    // 录制相关
    void onRecordingStarted(int cameraId, const QString& filePath);
    void onRecordingStopped(int cameraId, const QString& filePath,
                             const QDateTime& startTime, const QDateTime& endTime, qint64 fileSize);

    // UI操作
    void onStartAll();
    void onStopAll();
    void onToggleRecord();
    void onShowAlarmRecords();
    void onShowRecordFiles();
    void onVideoDoubleClicked(int cameraId);
    void onAlarmItemClicked(QListWidgetItem* item);

    // 状态栏更新
    void updateStatusBar();

private:
    void setupUI();
    void setupToolbar();
    void setupVideoGrid();
    void setupSidePanel();
    void setupConnections();
    void initCameras();
    void saveSnapshot(int cameraId, const QImage& image, const QString& timestamp);

    // 核心模块
    CameraManager* m_cameraManager;
    DatabaseManager* m_dbManager;

    // UI组件
    QWidget* m_centralWidget;
    QSplitter* m_mainSplitter;
    QWidget* m_videoContainer;
    QGridLayout* m_videoGrid;
    QMap<int, VideoWidget*> m_videoWidgets;

    // 侧边面板
    QWidget* m_sidePanel;
    QTabWidget* m_sideTabs;
    QListWidget* m_alarmList;
    QListWidget* m_recordList;

    // 工具栏
    QToolBar* m_toolBar;
    QAction* m_actionStartAll;
    QAction* m_actionStopAll;
    QAction* m_actionRecord;
    QAction* m_actionRefresh;

    // 状态栏
    QLabel* m_statusLabel;
    QLabel* m_cameraCountLabel;
    QLabel* m_alarmCountLabel;
    QTimer* m_statusTimer;

    // 状态
    int m_activeCameraCount;
    int m_totalAlarmCount;
    int m_currentFullscreenCamera;
    bool m_isRecording;
};

#endif // MAIN_WINDOW_H

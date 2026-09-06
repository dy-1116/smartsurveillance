#ifndef DATABASE_MANAGER_H
#define DATABASE_MANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QDateTime>
#include <vector>

// 报警事件记录
struct AlarmRecord {
    int id;
    int cameraId;
    QString cameraName;
    QDateTime alarmTime;
    QString snapshotPath;
    QString videoPath;
    QString description;
};

// 录制记录
struct RecordInfo {
    int id;
    int cameraId;
    QString cameraName;
    QDateTime startTime;
    QDateTime endTime;
    QString filePath;
    qint64 fileSize;
};

// SQLite数据库管理（成员B负责）
class DatabaseManager : public QObject {
    Q_OBJECT

public:
    explicit DatabaseManager(QObject* parent = nullptr);
    ~DatabaseManager();

    // 初始化数据库，创建表
    bool init(const QString& dbPath = "monitor.db");

    // 报警事件操作
    bool insertAlarm(int cameraId, const QString& cameraName,
                     const QDateTime& time, const QString& snapshotPath,
                     const QString& videoPath = "", const QString& desc = "");
    std::vector<AlarmRecord> queryAlarms(int cameraId = -1,
                                          const QDateTime& start = QDateTime(),
                                          const QDateTime& end = QDateTime(),
                                          int limit = 100);
    bool deleteAlarm(int id);

    // 录制记录操作
    bool insertRecord(int cameraId, const QString& cameraName,
                      const QDateTime& startTime, const QDateTime& endTime,
                      const QString& filePath, qint64 fileSize);
    std::vector<RecordInfo> queryRecords(int cameraId = -1, int limit = 100);
    bool deleteRecord(int id);

    // 摄像头信息操作
    bool upsertCamera(int cameraId, const QString& name, const QString& rtspUrl);
    QString getCameraName(int cameraId);
    QString getCameraRtsp(int cameraId);
    std::vector<std::pair<int, QString>> getAllCameras();

private:
    bool createTables();
    QSqlDatabase m_db;
    bool m_initialized;
};

#endif // DATABASE_MANAGER_H

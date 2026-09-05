// 数据存储	
// 建表、录制记录、运动事件写入



#include "DatabaseManager.h"
#include <QSqlError>
#include <QVariant>
#include <QDir>
#include <QDebug>

DatabaseManager::DatabaseManager(QObject* parent)
    : QObject(parent), m_initialized(false) {}

DatabaseManager::~DatabaseManager() {
    if (m_db.isOpen()) {
        m_db.close();
    }
}

bool DatabaseManager::init(const QString& dbPath) {
    if (m_initialized) return true;

    m_db = QSqlDatabase::addDatabase("QSQLITE");
    m_db.setDatabaseName(dbPath);

    if (!m_db.open()) {
        qDebug() << "数据库打开失败:" << m_db.lastError().text();
        return false;
    }

    // 开启WAL模式提升并发性能
    QSqlQuery query(m_db);
    query.exec("PRAGMA journal_mode=WAL;");
    query.exec("PRAGMA synchronous=NORMAL;");

    if (!createTables()) {
        return false;
    }

    m_initialized = true;
    return true;
}

bool DatabaseManager::createTables() {
    QSqlQuery query(m_db);

    // 摄像头信息表
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS cameras ("
        "id INTEGER PRIMARY KEY,"
        "name TEXT NOT NULL,"
        "rtsp_url TEXT NOT NULL,"
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");")) {
        qDebug() << "创建cameras表失败:" << query.lastError().text();
        return false;
    }

    // 报警事件表
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS alarms ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "camera_id INTEGER NOT NULL,"
        "camera_name TEXT,"
        "alarm_time DATETIME NOT NULL,"
        "snapshot_path TEXT,"
        "video_path TEXT,"
        "description TEXT,"
        "FOREIGN KEY (camera_id) REFERENCES cameras(id)"
        ");")) {
        qDebug() << "创建alarms表失败:" << query.lastError().text();
        return false;
    }

    // 报警时间索引
    query.exec("CREATE INDEX IF NOT EXISTS idx_alarms_time ON alarms(alarm_time);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_alarms_camera ON alarms(camera_id);");

    // 录制记录表
    if (!query.exec(
        "CREATE TABLE IF NOT EXISTS records ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "camera_id INTEGER NOT NULL,"
        "camera_name TEXT,"
        "start_time DATETIME NOT NULL,"
        "end_time DATETIME NOT NULL,"
        "file_path TEXT NOT NULL,"
        "file_size INTEGER DEFAULT 0,"
        "FOREIGN KEY (camera_id) REFERENCES cameras(id)"
        ");")) {
        qDebug() << "创建records表失败:" << query.lastError().text();
        return false;
    }

    query.exec("CREATE INDEX IF NOT EXISTS idx_records_time ON records(start_time);");

    return true;
}

bool DatabaseManager::insertAlarm(int cameraId, const QString& cameraName,
                                    const QDateTime& time, const QString& snapshotPath,
                                    const QString& videoPath, const QString& desc) {
    if (!m_initialized) return false;
    QSqlQuery query(m_db);
    query.prepare(
        "INSERT INTO alarms (camera_id, camera_name, alarm_time, snapshot_path, video_path, description) "
        "VALUES (?, ?, ?, ?, ?, ?);");
    query.addBindValue(cameraId);
    query.addBindValue(cameraName);
    query.addBindValue(time);
    query.addBindValue(snapshotPath);
    query.addBindValue(videoPath);
    query.addBindValue(desc);
    return query.exec();
}

std::vector<AlarmRecord> DatabaseManager::queryAlarms(int cameraId,
                                                         const QDateTime& start,
                                                         const QDateTime& end,
                                                         int limit) {
    std::vector<AlarmRecord> results;
    if (!m_initialized) return results;

    QString sql = "SELECT id, camera_id, camera_name, alarm_time, snapshot_path, video_path, description "
                  "FROM alarms WHERE 1=1";
    QVariantList params;

    if (cameraId >= 0) {
        sql += " AND camera_id = ?";
        params << cameraId;
    }
    if (start.isValid()) {
        sql += " AND alarm_time >= ?";
        params << start;
    }
    if (end.isValid()) {
        sql += " AND alarm_time <= ?";
        params << end;
    }
    sql += " ORDER BY alarm_time DESC LIMIT ?";
    params << limit;

    QSqlQuery query(m_db);
    query.prepare(sql);
    for (const auto& p : params) query.addBindValue(p);

    if (!query.exec()) return results;

    while (query.next()) {
        AlarmRecord rec;
        rec.id = query.value(0).toInt();
        rec.cameraId = query.value(1).toInt();
        rec.cameraName = query.value(2).toString();
        rec.alarmTime = query.value(3).toDateTime();
        rec.snapshotPath = query.value(4).toString();
        rec.videoPath = query.value(5).toString();
        rec.description = query.value(6).toString();
        results.push_back(rec);
    }
    return results;
}

bool DatabaseManager::deleteAlarm(int id) {
    if (!m_initialized) return false;
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM alarms WHERE id = ?;");
    query.addBindValue(id);
    return query.exec();
}

bool DatabaseManager::insertRecord(int cameraId, const QString& cameraName,
                                     const QDateTime& startTime, const QDateTime& endTime,
                                     const QString& filePath, qint64 fileSize) {
    if (!m_initialized) return false;
    QSqlQuery query(m_db);
    query.prepare(
        "INSERT INTO records (camera_id, camera_name, start_time, end_time, file_path, file_size) "
        "VALUES (?, ?, ?, ?, ?, ?);");
    query.addBindValue(cameraId);
    query.addBindValue(cameraName);
    query.addBindValue(startTime);
    query.addBindValue(endTime);
    query.addBindValue(filePath);
    query.addBindValue(fileSize);
    return query.exec();
}

std::vector<RecordInfo> DatabaseManager::queryRecords(int cameraId, int limit) {
    std::vector<RecordInfo> results;
    if (!m_initialized) return results;

    QString sql = "SELECT id, camera_id, camera_name, start_time, end_time, file_path, file_size "
                  "FROM records WHERE 1=1";
    QVariantList params;
    if (cameraId >= 0) {
        sql += " AND camera_id = ?";
        params << cameraId;
    }
    sql += " ORDER BY start_time DESC LIMIT ?";
    params << limit;

    QSqlQuery query(m_db);
    query.prepare(sql);
    for (const auto& p : params) query.addBindValue(p);

    if (!query.exec()) return results;

    while (query.next()) {
        RecordInfo info;
        info.id = query.value(0).toInt();
        info.cameraId = query.value(1).toInt();
        info.cameraName = query.value(2).toString();
        info.startTime = query.value(3).toDateTime();
        info.endTime = query.value(4).toDateTime();
        info.filePath = query.value(5).toString();
        info.fileSize = query.value(6).toLongLong();
        results.push_back(info);
    }
    return results;
}

bool DatabaseManager::deleteRecord(int id) {
    if (!m_initialized) return false;
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM records WHERE id = ?;");
    query.addBindValue(id);
    return query.exec();
}

bool DatabaseManager::upsertCamera(int cameraId, const QString& name, const QString& rtspUrl) {
    if (!m_initialized) return false;
    QSqlQuery query(m_db);
    query.prepare(
        "INSERT INTO cameras (id, name, rtsp_url) VALUES (?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET name=excluded.name, rtsp_url=excluded.rtsp_url;");
    query.addBindValue(cameraId);
    query.addBindValue(name);
    query.addBindValue(rtspUrl);
    return query.exec();
}

QString DatabaseManager::getCameraName(int cameraId) {
    if (!m_initialized) return QString();
    QSqlQuery query(m_db);
    query.prepare("SELECT name FROM cameras WHERE id = ?;");
    query.addBindValue(cameraId);
    if (query.exec() && query.next()) {
        return query.value(0).toString();
    }
    return QString("摄像头%1").arg(cameraId);
}

QString DatabaseManager::getCameraRtsp(int cameraId) {
    if (!m_initialized) return QString();
    QSqlQuery query(m_db);
    query.prepare("SELECT rtsp_url FROM cameras WHERE id = ?;");
    query.addBindValue(cameraId);
    if (query.exec() && query.next()) {
        return query.value(0).toString();
    }
    return QString();
}

std::vector<std::pair<int, QString>> DatabaseManager::getAllCameras() {
    std::vector<std::pair<int, QString>> cameras;
    if (!m_initialized) return cameras;
    QSqlQuery query(m_db);
    query.exec("SELECT id, name FROM cameras ORDER BY id;");
    while (query.next()) {
        cameras.push_back({query.value(0).toInt(), query.value(1).toString()});
    }
    return cameras;
}

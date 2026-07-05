#include "DatabaseManager.h"
#include <QSqlError>
#include <QSqlQuery>
#include <QDir>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>

DatabaseManager::DatabaseManager(QObject *parent) : QObject(parent)
{
}

DatabaseManager::~DatabaseManager()
{
    if (db.isOpen()) {
        db.close();
    }
}

bool DatabaseManager::initDb()
{
    // Ensure the data directory exists (up one directory from qt_client executable)
    QString dataDirPath = QCoreApplication::applicationDirPath() + "/../data/study_data";
    QDir dir(dataDirPath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QString dbPath = dataDirPath + "/stats.db";
    
    if (QSqlDatabase::contains("qt_sql_default_connection")) {
        db = QSqlDatabase::database("qt_sql_default_connection");
    } else {
        db = QSqlDatabase::addDatabase("QSQLITE");
    }
    
    db.setDatabaseName(dbPath);

    if (!db.open()) {
        qWarning() << "Failed to open database at" << dbPath << ":" << db.lastError().text();
        return false;
    }

    qDebug() << "Successfully opened database at" << dbPath;

    // Create table if it doesn't exist
    QSqlQuery query;
    bool success = query.exec(
        "CREATE TABLE IF NOT EXISTS daily_stats ("
        "date TEXT PRIMARY KEY, "
        "effective_seconds INTEGER, "
        "absent_count INTEGER, "
        "distracted_count INTEGER, "
        "distracted_seconds INTEGER"
        ")"
    );

    if (!success) {
        qWarning() << "Failed to create table daily_stats:" << query.lastError().text();
        return false;
    }

    success = query.exec(
        "CREATE TABLE IF NOT EXISTS hourly_stats ("
        "date TEXT, "
        "hour INTEGER, "
        "focus_seconds INTEGER, "
        "distracted_seconds INTEGER, "
        "absent_seconds INTEGER, "
        "PRIMARY KEY (date, hour)"
        ")"
    );

    if (!success) {
        qWarning() << "Failed to create table hourly_stats:" << query.lastError().text();
        return false;
    }

    return true;
}

void DatabaseManager::loadTodayStats(int &effective, int &absent, int &distracted_cnt, int &distracted_sec)
{
    if (!db.isOpen()) return;

    QString today = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    QSqlQuery query;
    query.prepare("SELECT effective_seconds, absent_count, distracted_count, distracted_seconds "
                  "FROM daily_stats WHERE date = :date");
    query.bindValue(":date", today);

    if (query.exec() && query.next()) {
        effective = query.value(0).toInt();
        absent = query.value(1).toInt();
        distracted_cnt = query.value(2).toInt();
        distracted_sec = query.value(3).toInt();
        qDebug() << "Loaded today's stats from DB:" << effective << absent << distracted_cnt << distracted_sec;
    } else {
        // No record for today yet
        effective = 0;
        absent = 0;
        distracted_cnt = 0;
        distracted_sec = 0;
    }
}

void DatabaseManager::saveTodayStats(int effective, int absent, int distracted_cnt, int distracted_sec)
{
    if (!db.isOpen()) return;

    QString today = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    QSqlQuery query;
    // Uses REPLACE INTO (SQLite specific) which acts as an UPSERT since date is PRIMARY KEY
    query.prepare("REPLACE INTO daily_stats (date, effective_seconds, absent_count, distracted_count, distracted_seconds) "
                  "VALUES (:date, :eff, :abs, :dcnt, :dsec)");
    query.bindValue(":date", today);
    query.bindValue(":eff", effective);
    query.bindValue(":abs", absent);
    query.bindValue(":dcnt", distracted_cnt);
    query.bindValue(":dsec", distracted_sec);

    if (!query.exec()) {
        qWarning() << "Failed to save today's stats:" << query.lastError().text();
    } else {
        qDebug() << "Saved today's stats to DB:" << effective << absent << distracted_cnt << distracted_sec;
    }
}

void DatabaseManager::loadHourlyStats(QVector<int> &focus, QVector<int> &distracted, QVector<int> &absent)
{
    if (!db.isOpen()) return;

    QString today = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    QSqlQuery query;
    query.prepare("SELECT hour, focus_seconds, distracted_seconds, absent_seconds FROM hourly_stats WHERE date = :date");
    query.bindValue(":date", today);

    if (query.exec()) {
        while (query.next()) {
            int hour = query.value(0).toInt();
            int focus_sec = query.value(1).toInt();
            int distracted_sec = query.value(2).toInt();
            int absent_sec = query.value(3).toInt();

            if (hour >= 0 && hour < 24) {
                if (focus.size() > hour) focus[hour] = focus_sec;
                if (distracted.size() > hour) distracted[hour] = distracted_sec;
                if (absent.size() > hour) absent[hour] = absent_sec;
            }
        }
        qDebug() << "Successfully loaded hourly stats for" << today;
    } else {
        qWarning() << "Failed to load hourly stats:" << query.lastError().text();
    }
}

void DatabaseManager::saveHourlyStats(const QVector<int> &focus, const QVector<int> &distracted, const QVector<int> &absent)
{
    if (!db.isOpen()) return;

    QString today = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    
    db.transaction();
    
    QSqlQuery query;
    query.prepare("REPLACE INTO hourly_stats (date, hour, focus_seconds, distracted_seconds, absent_seconds) "
                  "VALUES (:date, :hour, :focus, :distracted, :absent)");

    for (int hour = 0; hour < 24; ++hour) {
        int f = focus.size() > hour ? focus.at(hour) : 0;
        int d = distracted.size() > hour ? distracted.at(hour) : 0;
        int a = absent.size() > hour ? absent.at(hour) : 0;

        // Only save if there's actual data for this hour to save space
        if (f > 0 || d > 0 || a > 0) {
            query.bindValue(":date", today);
            query.bindValue(":hour", hour);
            query.bindValue(":focus", f);
            query.bindValue(":distracted", d);
            query.bindValue(":absent", a);
            query.exec();
        }
    }
    
    db.commit();
}

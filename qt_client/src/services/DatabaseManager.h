#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <QVector>

class DatabaseManager : public QObject
{
    Q_OBJECT
public:
    static DatabaseManager& instance() {
        static DatabaseManager inst;
        return inst;
    }

    bool initDb();
    
    // 加载当天的数据
    void loadTodayStats(int &effective, int &absent, int &distracted_cnt, int &distracted_sec);
    
    // 保存/更新当天的数据
    void saveTodayStats(int effective, int absent, int distracted_cnt, int distracted_sec);

    // 加载当天的24小时分布数据
    void loadHourlyStats(QVector<int> &focus, QVector<int> &distracted, QVector<int> &absent);
    
    // 保存/更新当天的24小时分布数据
    void saveHourlyStats(const QVector<int> &focus, const QVector<int> &distracted, const QVector<int> &absent);

private:
    explicit DatabaseManager(QObject *parent = nullptr);
    ~DatabaseManager();

    QSqlDatabase db;
};

#endif // DATABASEMANAGER_H

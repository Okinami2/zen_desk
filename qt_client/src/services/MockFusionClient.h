#ifndef MOCKFUSIONCLIENT_H
#define MOCKFUSIONCLIENT_H

#include <QObject>
#include <QTimer>
#include <QString>

class MockFusionClient : public QObject
{
    Q_OBJECT
public:
    explicit MockFusionClient(QObject *parent = nullptr);
    void start(int intervalMs = 2000);

signals:
    void stateUpdated(const QString &stateText, double score);

private slots:
    void onTimeout();

private:
    QTimer m_timer;
    int m_index;
};

#endif

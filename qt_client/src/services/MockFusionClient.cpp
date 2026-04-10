#include "MockFusionClient.h"

MockFusionClient::MockFusionClient(QObject *parent)
    : QObject(parent), m_index(0)
{
    connect(&m_timer, &QTimer::timeout, this, &MockFusionClient::onTimeout);
}

void MockFusionClient::start(int intervalMs)
{
    m_timer.start(intervalMs);
    onTimeout();
}

void MockFusionClient::onTimeout()
{
    static const char *states[] = {
        "专注",
        "走神",
        "疲劳",
        "离座"
    };

    static const double scores[] = {
        0.92,
        0.68,
        0.81,
        0.30
    };

    emit stateUpdated(QString::fromUtf8(states[m_index]), scores[m_index]);
    m_index = (m_index + 1) % 4;
}

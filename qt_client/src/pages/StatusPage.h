#ifndef STATUSPAGE_H
#define STATUSPAGE_H

#include <QWidget>
#include <QLabel>

#include "protocol.h"

class StatusPage : public QWidget
{
    Q_OBJECT
public:
    explicit StatusPage(QWidget *parent = nullptr);

    void setSeatPresent(bool present, float distanceMeters, float confidence);
    void setFacePresent(bool present, float score);
    void setAttention(const QString &attention, float yaw, float pitch, float roll);
    void setFusionState(LearningState state, float score);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QLabel *seatValue;
    QLabel *seatDetail;
    QLabel *faceValue;
    QLabel *faceDetail;
    QLabel *attentionValue;
    QLabel *attentionDetail;
    QLabel *fusionValue;
    QLabel *fusionDetail;

    QWidget *makeStatusCard(const QString &title, QLabel **value, QLabel **detail,
        const QColor &accent);
    void setValueStyle(QLabel *label, const QColor &color);
    QString attentionText(const QString &attention) const;
    QString fusionText(LearningState state) const;
};

#endif

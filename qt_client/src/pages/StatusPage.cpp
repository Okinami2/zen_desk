#include "StatusPage.h"

#include <QDateTime>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QtGlobal>
#include <QVBoxLayout>

StatusPage::StatusPage(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, false);

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(40, 34, 40, 36);
    root->setSpacing(22);

    QHBoxLayout *header = new QHBoxLayout();
    QLabel *title = new QLabel("实时学习状态");
    title->setStyleSheet("font-size: 30px; font-weight: 800; color: #111827; background: transparent;");
    QLabel *hint = new QLabel("live");
    hint->setAlignment(Qt::AlignCenter);
    hint->setFixedSize(64, 32);
    hint->setStyleSheet("background: #DCFCE7; color: #15803D; border-radius: 8px; font-size: 14px; font-weight: 700;");
    header->addWidget(title);
    header->addStretch();
    header->addWidget(hint);
    root->addLayout(header);

    QGridLayout *grid = new QGridLayout();
    grid->setHorizontalSpacing(20);
    grid->setVerticalSpacing(20);
    grid->addWidget(makeStatusCard("在座状态", &seatValue, &seatDetail, QColor(0x10, 0xB9, 0x81)), 0, 0);
    grid->addWidget(makeStatusCard("人脸检测", &faceValue, &faceDetail, QColor(0x4F, 0x46, 0xE5)), 0, 1);
    grid->addWidget(makeStatusCard("视线方向", &attentionValue, &attentionDetail, QColor(0xF5, 0x9E, 0x0B)), 1, 0);
    grid->addWidget(makeStatusCard("融合判断", &fusionValue, &fusionDetail, QColor(0x06, 0xB6, 0xD4)), 1, 1);
    root->addLayout(grid, 1);

    setSeatPresent(false, 0.0f, 0.0f);
    setFacePresent(false, 0.0f);
    setAttention("waiting", 0.0f, 0.0f, 0.0f);
    setFusionState(STATE_ABSENT, 0.0f);
    fusionValue->setText("等待融合");
    fusionDetail->setText("尚未收到 fusion_service 状态");
}

QWidget *StatusPage::makeStatusCard(const QString &title, QLabel **value, QLabel **detail,
    const QColor &accent)
{
    QWidget *card = new QWidget(this);
    card->setStyleSheet("background: white; border-radius: 18px;");
    QVBoxLayout *layout = new QVBoxLayout(card);
    layout->setContentsMargins(24, 22, 24, 22);
    layout->setSpacing(12);

    QHBoxLayout *top = new QHBoxLayout();
    QLabel *dot = new QLabel();
    dot->setFixedSize(12, 12);
    dot->setStyleSheet(QString("background: %1; border-radius: 6px;").arg(accent.name()));
    QLabel *titleLabel = new QLabel(title);
    titleLabel->setStyleSheet("font-size: 16px; font-weight: 700; color: #6B7280; background: transparent;");
    top->addWidget(dot);
    top->addSpacing(8);
    top->addWidget(titleLabel);
    top->addStretch();
    layout->addLayout(top);

    *value = new QLabel("等待");
    (*value)->setMinimumHeight(68);
    (*value)->setStyleSheet("font-size: 38px; font-weight: 900; color: #111827; background: transparent;");
    layout->addWidget(*value);

    *detail = new QLabel("等待数据");
    (*detail)->setWordWrap(true);
    (*detail)->setStyleSheet("font-size: 15px; color: #6B7280; background: transparent;");
    layout->addWidget(*detail);
    layout->addStretch();
    return card;
}

void StatusPage::setValueStyle(QLabel *label, const QColor &color)
{
    label->setStyleSheet(QString("font-size: 38px; font-weight: 900; color: %1; background: transparent;")
        .arg(color.name()));
}

void StatusPage::setSeatPresent(bool present, float distanceMeters, float confidence)
{
    seatValue->setText(present ? "在座" : "离座");
    setValueStyle(seatValue, present ? QColor(0x10, 0xB9, 0x81) : QColor(0xEF, 0x44, 0x44));
    if (distanceMeters > 0.01f) {
        seatDetail->setText(QString("距离 %1 m · 置信度 %2%")
            .arg(distanceMeters, 0, 'f', 2)
            .arg(qRound(confidence * 100.0f)));
    } else {
        seatDetail->setText(present ? "融合状态显示用户在座" : "融合状态显示用户离座");
    }
}

void StatusPage::setFacePresent(bool present, float score)
{
    faceValue->setText(present ? "有人脸" : "未检测");
    setValueStyle(faceValue, present ? QColor(0x4F, 0x46, 0xE5) : QColor(0x9C, 0xA3, 0xAF));
    faceDetail->setText(present
        ? QString("检测置信度 %1%").arg(qRound(score * 100.0f))
        : "vision_service 暂未检测到有效人脸");
}

void StatusPage::setAttention(const QString &attention, float yaw, float pitch, float roll)
{
    attentionValue->setText(attentionText(attention));
    QColor color = (attention == "front") ? QColor(0x10, 0xB9, 0x81) : QColor(0xF5, 0x9E, 0x0B);
    if (attention == "no_face" || attention == "waiting") {
        color = QColor(0x9C, 0xA3, 0xAF);
    } else if (attention == "error") {
        color = QColor(0xEF, 0x44, 0x44);
    }
    setValueStyle(attentionValue, color);
    attentionDetail->setText(QString("yaw %1° · pitch %2° · roll %3°")
        .arg(yaw, 0, 'f', 1)
        .arg(pitch, 0, 'f', 1)
        .arg(roll, 0, 'f', 1));
}

void StatusPage::setFusionState(LearningState state, float score)
{
    const bool focused = (state == STATE_FOCUSED);
    fusionValue->setText(focused ? "专注" : fusionText(state));
    setValueStyle(fusionValue, focused ? QColor(0x10, 0xB9, 0x81) : QColor(0xF5, 0x9E, 0x0B));
    fusionDetail->setText(QString("融合状态 %1 · 置信度 %2%")
        .arg(static_cast<int>(state))
        .arg(qRound(score * 100.0f)));
}

QString StatusPage::attentionText(const QString &attention) const
{
    if (attention == "front") return "桌面学习区";
    if (attention == "left") return "左偏";
    if (attention == "right") return "右偏";
    if (attention == "up") return "抬头";
    if (attention == "down") return "低头";
    if (attention == "eyes_closed") return "闭眼";
    if (attention == "no_face") return "无人脸";
    if (attention == "error") return "异常";
    return "等待";
}

QString StatusPage::fusionText(LearningState state) const
{
    switch (state) {
        case STATE_SEATED_IDLE: return "在座空闲";
        case STATE_FOCUSED: return "专注";
        case STATE_DISTRACTED: return "走神";
        case STATE_TIRED: return "疲劳";
        case STATE_ABSENT: return "离座";
    }
    return "未知";
}

void StatusPage::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0xF4, 0xF5, 0xF7));
}

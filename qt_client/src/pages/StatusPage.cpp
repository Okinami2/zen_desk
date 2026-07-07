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
    root->setContentsMargins(40, 26, 40, 26);
    root->setSpacing(16);

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
    grid->setVerticalSpacing(16);
    // 前两行是双元素卡片，底部坐姿卡片有 4 个文本行，需要更多高度
    grid->setRowStretch(0, 3);
    grid->setRowStretch(1, 3);
    grid->setRowStretch(2, 4);
    grid->addWidget(makeStatusCard("在座状态", &seatValue, &seatDetail, QColor(0x10, 0xB9, 0x81)), 0, 0);
    grid->addWidget(makeStatusCard("人脸检测", &faceValue, &faceDetail, QColor(0x4F, 0x46, 0xE5)), 0, 1);
    grid->addWidget(makeStatusCard("视线方向", &attentionValue, &attentionDetail, QColor(0xF5, 0x9E, 0x0B)), 1, 0);
    grid->addWidget(makeStatusCard("融合判断", &fusionValue, &fusionDetail, QColor(0x06, 0xB6, 0xD4)), 1, 1);

    // 坐姿监测卡片，跨两列铺满底部一行
    QWidget *postureCard = makeStatusCard("坐姿监测", &postureValue, &postureDetail,
        QColor(0xEC, 0x48, 0x99));
    postureMetric = new QLabel("肩斜 0.00 · 侧倾 0.00 · 扶头 0.00");
    postureMetric->setWordWrap(true);
    postureMetric->setStyleSheet("font-size: 13px; color: #94A3B8; background: transparent;");
    if (QVBoxLayout *cardLay = qobject_cast<QVBoxLayout *>(postureCard->layout())) {
        // 插到卡片底部 stretch 之前
        cardLay->insertWidget(cardLay->count() - 1, postureMetric);
    }
    grid->addWidget(postureCard, 2, 0, 1, 2);
    root->addLayout(grid, 1);

    setSeatPresent(false, 0.0f, 0.0f);
    setFacePresent(false, 0.0f);
    setAttention("waiting", 0.0f, 0.0f, 0.0f);
    setFusionState(STATE_ABSENT, 0.0f);
    fusionValue->setText("等待融合");
    fusionDetail->setText("尚未收到 fusion_service 状态");
    updatePostureStatus("waiting", false, false, 0.0, 0.0, 0.0, false,
        0.0, 0.0, 0.0);
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

void StatusPage::updatePostureStatus(const QString &label, bool ok, bool present,
                                     double poseScore, double detectionScore,
                                     double ageMs, bool updated,
                                     double shoulderTilt, double bodyLean,
                                     double handSupportScore)
{
    Q_UNUSED(ageMs);
    Q_UNUSED(updated);
    Q_UNUSED(detectionScore);

    QString title = "等待数据";
    QString detail = "pose 模型低频运行，约 2 秒刷新一次。";
    QColor color(0x9C, 0xA3, 0xAF);

    if (!present) {
        title = "未检测";
        detail = "请确认上半身进入摄像头画面。";
        color = QColor(0x9C, 0xA3, 0xAF);
    } else if (ok || label == "normal") {
        title = "坐姿规范";
        detail = "肩部基本水平，头部和躯干位置正常。";
        color = QColor(0x10, 0xB9, 0x81);
    } else if (label == "hunchback") {
        title = "疑似驼背";
        detail = "头部相对肩线过低，建议挺直背部并抬高视线。";
        color = QColor(0xEF, 0x44, 0x44);
    } else if (label == "shoulder_tilt") {
        title = "斜肩";
        detail = "左右肩高度差偏大，建议放松肩膀并调整坐姿。";
        color = QColor(0xF5, 0x9E, 0x0B);
    } else if (label == "hand_support_head") {
        title = "疑似单手撑头";
        detail = "手腕靠近头部，可能正在撑头，建议双手离开脸部。";
        color = QColor(0xEF, 0x44, 0x44);
    } else if (label == "lean_left") {
        title = "身体左倾";
        detail = "肩部中心相对髋部偏左，建议回到椅子中央。";
        color = QColor(0xF5, 0x9E, 0x0B);
    } else if (label == "lean_right") {
        title = "身体右倾";
        detail = "肩部中心相对髋部偏右，建议回到椅子中央。";
        color = QColor(0xF5, 0x9E, 0x0B);
    } else if (label == "head_offset") {
        title = "头部偏移";
        detail = "头部相对肩部中心偏移明显，建议回正头部。";
        color = QColor(0xF5, 0x9E, 0x0B);
    } else if (label == "unknown") {
        title = "关键点不足";
        detail = "肩部或头部关键点置信度不足，请检查画面遮挡和光照。";
        color = QColor(0x9C, 0xA3, 0xAF);
    } else if (label == "waiting") {
        title = "等待";
        detail = "pose 模型低频运行，约 2 秒刷新一次。";
        color = QColor(0x9C, 0xA3, 0xAF);
    } else {
        title = label.isEmpty() ? "等待数据" : label;
        detail = "收到未知坐姿标签。";
        color = QColor(0x9C, 0xA3, 0xAF);
    }

    if (postureValue) {
        postureValue->setText(title);
        setValueStyle(postureValue, color);
    }
    if (postureDetail) {
        postureDetail->setText(detail);
    }
    if (postureMetric) {
        postureMetric->setText(QString("肩斜 %1 · 侧倾 %2 · 扶头 %3 · pose %4")
            .arg(shoulderTilt, 0, 'f', 2)
            .arg(bodyLean, 0, 'f', 2)
            .arg(handSupportScore, 0, 'f', 2)
            .arg(poseScore, 0, 'f', 2));
    }
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

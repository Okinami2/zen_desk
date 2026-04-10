#include "StudyPage.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QConicalGradient>
#include <QRadialGradient>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTime>
#include <QFontDatabase>
#include <QPushButton>
#include <QGraphicsOpacityEffect>
#include <QtMath>

// ════════════════════════════════════════════════════════════
//  RingProgressWidget
// ════════════════════════════════════════════════════════════
RingProgressWidget::RingProgressWidget(QWidget *parent) : QWidget(parent)
{
    setFixedSize(340, 340);
}

void RingProgressWidget::setProgress(double v)
{
    m_progress = qBound(0.0, v, 1.0);
    update();
}

void RingProgressWidget::setCountdown(bool b) { m_countdown = b; update(); }
void RingProgressWidget::setRingColor(const QColor &c) { m_ringColor = c; update(); }

void RingProgressWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int W = width(), H = height();
    const int ringW = 18;
    QRectF outerRect(ringW/2+2, ringW/2+2, W-ringW-4, H-ringW-4);

    // ── 轨道（暗色底环） ──────────────────────────────────────────────────
    p.setPen(QPen(QColor(255,255,255,18), ringW, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(outerRect);

    // ── 进度弧 ────────────────────────────────────────────────────────────
    double span = m_progress * 360.0;
    // Qt drawArc: 从 90° 开始（顶部），顺时针为负，单位 1/16 度
    int startAngle = 90 * 16;
    int spanAngle  = -(int)(span * 16);

    // 渐变色弧：用 QConicalGradient
    QConicalGradient cg(W/2.0, H/2.0, 90.0);
    cg.setColorAt(0.0, m_ringColor);
    cg.setColorAt(0.5, m_ringColor.lighter(140));
    cg.setColorAt(1.0, m_ringColor);

    QPen arcPen(QBrush(cg), ringW, Qt::SolidLine, Qt::RoundCap);
    p.setPen(arcPen);
    p.drawArc(outerRect, startAngle, spanAngle);

    // ── 弧头部发光点 ─────────────────────────────────────────────────────
    if (m_progress > 0.02) {
        double headAngle = (90.0 - span) * M_PI / 180.0;
        double r = outerRect.width() / 2.0;
        double cx = W/2.0 + r * qCos(headAngle);
        double cy = H/2.0 - r * qSin(headAngle);

        QRadialGradient glow(cx, cy, ringW);
        glow.setColorAt(0, QColor(255,255,255,200));
        glow.setColorAt(0.4, m_ringColor.lighter(160));
        glow.setColorAt(1, QColor(0,0,0,0));
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(QPointF(cx,cy), ringW, ringW);
    }
}

// ════════════════════════════════════════════════════════════
//  StudyPage
// ════════════════════════════════════════════════════════════
StudyPage::StudyPage(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, false);

    int fid = QFontDatabase::addApplicationFont(":/fonts/Inter_28pt-Bold.ttf");
    QString numFamily = QFontDatabase::applicationFontFamilies(fid).value(0, "Inter");

    // ── 根布局 ────────────────────────────────────────────────────────────
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->setAlignment(Qt::AlignCenter);

    root->addStretch(1);

    // ── 圆环 + 时钟 叠加（使用 QWidget 叠层）────────────────────────────
    QWidget *ringArea = new QWidget();
    ringArea->setFixedSize(340, 340);
    ringArea->setAttribute(Qt::WA_TranslucentBackground);

    ring = new RingProgressWidget(ringArea);
    ring->move(0, 0);

    // 时钟文字叠在圆环正中央
    timeDisplay = new QLabel("00:00", ringArea);
    timeDisplay->setAlignment(Qt::AlignCenter);
    timeDisplay->setGeometry(0, 100, 340, 120);
    timeDisplay->setStyleSheet(QString(
        "font-family: '%1';"
        "font-size: 80px;"
        "font-weight: 700;"
        "color: #ffffff;"
        "background: transparent;"
        "letter-spacing: -2px;"
    ).arg(numFamily));

    statusLabel = new QLabel("专注中 …", ringArea);
    statusLabel->setAlignment(Qt::AlignCenter);
    statusLabel->setGeometry(0, 218, 340, 36);
    statusLabel->setStyleSheet(
        "font-size: 18px;"
        "color: rgba(255,255,255,0.60);"
        "background: transparent;"
        "letter-spacing: 2px;"
    );

    root->addWidget(ringArea, 0, Qt::AlignHCenter);

    // ── 番茄计数 ──────────────────────────────────────────────────────────
    pomodoroLabel = new QLabel("番茄钟  1 / 4");
    pomodoroLabel->setAlignment(Qt::AlignCenter);
    pomodoroLabel->setStyleSheet(
        "font-size: 16px;"
        "color: rgba(255,255,255,0.40);"
        "letter-spacing: 3px;"
        "margin-top: 16px;"
    );
    root->addWidget(pomodoroLabel, 0, Qt::AlignHCenter);

    // ── 提示浮层 ──────────────────────────────────────────────────────────
    tipWidget = new QLabel("💧  喝口水吧");
    tipWidget->setAlignment(Qt::AlignCenter);
    tipWidget->setFixedHeight(52);
    tipWidget->setStyleSheet(
        "font-size: 20px;"
        "color: #93C5FD;"
        "background: rgba(37,99,235,0.22);"
        "border: 1px solid rgba(147,197,253,0.25);"
        "border-radius: 26px;"
        "padding: 0 32px;"
        "margin-top: 20px;"
    );
    tipWidget->setVisible(false);

    QGraphicsOpacityEffect *tipOpacity = new QGraphicsOpacityEffect(tipWidget);
    tipWidget->setGraphicsEffect(tipOpacity);
    tipAnim = new QPropertyAnimation(tipOpacity, "opacity", this);
    tipAnim->setDuration(600);

    root->addWidget(tipWidget, 0, Qt::AlignHCenter);
    root->addStretch(1);

    // ── 底部操作按钮 ──────────────────────────────────────────────────────
    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->setContentsMargins(0, 0, 0, 40);
    btnRow->setSpacing(24);
    btnRow->setAlignment(Qt::AlignHCenter);

    auto makeCtrlBtn = [](const QString &text, const QString &bg) -> QPushButton* {
        QPushButton *b = new QPushButton(text);
        b->setFixedSize(140, 52);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(QString(
            "QPushButton {"
            "  background: %1;"
            "  color: rgba(255,255,255,0.85);"
            "  font-size: 16px;"
            "  font-weight: 600;"
            "  border: 1px solid rgba(255,255,255,0.12);"
            "  border-radius: 26px;"
            "  letter-spacing: 1px;"
            "}"
            "QPushButton:hover { background: rgba(255,255,255,0.18); }"
            "QPushButton:pressed { background: rgba(255,255,255,0.08); }"
        ).arg(bg));
        return b;
    };

    QPushButton *pauseBtn = makeCtrlBtn("⏸  暂停", "rgba(255,255,255,0.10)");
    QPushButton *stopBtn  = makeCtrlBtn("⏹  结束", "rgba(239,68,68,0.25)");

    connect(pauseBtn, &QPushButton::clicked, this, [this, pauseBtn](){
        if (timer->isActive()) {
            timer->stop();
            pauseBtn->setText("▶  继续");
        } else {
            timer->start(1000);
            pauseBtn->setText("⏸  暂停");
        }
    });
    connect(stopBtn, &QPushButton::clicked, this, &StudyPage::studyFinished);

    btnRow->addWidget(pauseBtn);
    btnRow->addWidget(stopBtn);
    root->addLayout(btnRow);

    // ── 定时器 ────────────────────────────────────────────────────────────
    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &StudyPage::tick);

    tipHideTimer = new QTimer(this);
    tipHideTimer->setSingleShot(true);
    connect(tipHideTimer, &QTimer::timeout, this, [this](){
        tipAnim->setStartValue(1.0);
        tipAnim->setEndValue(0.0);
        connect(tipAnim, &QPropertyAnimation::finished, this, [this](){
            tipWidget->setVisible(false);
            disconnect(tipAnim, &QPropertyAnimation::finished, nullptr, nullptr);
        });
        tipAnim->start();
    });
}

void StudyPage::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 深空渐变背景
    QLinearGradient bg(0, 0, 0, height());
    bg.setColorAt(0.0, QColor(0x06, 0x09, 0x1A));
    bg.setColorAt(0.5, QColor(0x0D, 0x0D, 0x2A));
    bg.setColorAt(1.0, QColor(0x04, 0x07, 0x14));
    p.fillRect(rect(), bg);

    // 圆形光晕（圆环正下方）
    QRadialGradient halo(width()/2.0, height()/2.0 - 30, 200);
    halo.setColorAt(0, QColor(0x4F, 0x46, 0xE5, 35));
    halo.setColorAt(1, QColor(0,0,0,0));
    p.fillRect(rect(), halo);
}

void StudyPage::startTimer(int minutes)
{
    if (minutes < 0) {
        isCountdown = false;
        seconds   = 0;
        totalSecs = 0;
        ring->setCountdown(false);
        ring->setRingColor(QColor(0x10, 0xB9, 0x81));
        ring->setProgress(0.0);
        statusLabel->setText("正计时 · 自由专注");
        pomodoroLabel->setText("自由模式");
    } else {
        isCountdown = true;
        seconds   = minutes * 60;
        totalSecs = seconds;
        ring->setCountdown(true);
        ring->setRingColor(QColor(0x4F, 0x46, 0xE5));
        ring->setProgress(1.0);
        statusLabel->setText("专注中 …");
        pomodoroLabel->setText(QString("番茄钟  %1 / 4").arg(pomodoroCount));
    }
    tipWidget->setVisible(false);
    tick();
    timer->start(1000);
}

void StudyPage::stopTimer()
{
    timer->stop();
    timeDisplay->setText("00:00");
    ring->setProgress(0.0);
    tipWidget->setVisible(false);
}

void StudyPage::tick()
{
    if (isCountdown) {
        if (seconds <= 0) {
            timer->stop();
            timeDisplay->setText("00:00");
            ring->setProgress(0.0);
            statusLabel->setText("休息一下吧 ☕");
            showTip("☕", "休息 5 分钟");
            emit studyFinished();
            return;
        }
        seconds--;
        double prog = totalSecs > 0 ? (double)seconds / totalSecs : 0.0;
        ring->setProgress(prog);

        // 最后 5 分钟提醒
        if (seconds == 300)
            showTip("⏰", "还有 5 分钟");
        // 每 25 分钟喝水提醒（正计时）
    } else {
        seconds++;
        // 正计时：圆环缓慢充满，以 25 分钟为一圈
        double prog = fmod(seconds, 1500) / 1500.0;
        ring->setProgress(prog);

        if (seconds > 0 && seconds % 1800 == 0)
            showTip("💧", "喝口水吧");
    }

    // 格式化时间
    QTime t(0, 0, 0);
    t = t.addSecs(isCountdown ? seconds : seconds);
    if (seconds < 3600)
        timeDisplay->setText(t.toString("mm:ss"));
    else
        timeDisplay->setText(t.toString("h:mm:ss"));

    // 更新状态文字
    if (isCountdown && seconds > 0) {
        if (seconds <= 300)
            statusLabel->setText("即将完成 · 坚持住！");
        else
            statusLabel->setText("专注中 …");
    }
}

void StudyPage::showTip(const QString &icon, const QString &text)
{
    tipWidget->setText(icon + "  " + text);
    tipWidget->setVisible(true);
    tipAnim->stop();
    // 淡入
    auto *eff = qobject_cast<QGraphicsOpacityEffect*>(tipWidget->graphicsEffect());
    if (eff) eff->setOpacity(0.0);
    tipAnim->setStartValue(0.0);
    tipAnim->setEndValue(1.0);
    tipAnim->start();
    // 8 秒后淡出
    tipHideTimer->start(8000);
}

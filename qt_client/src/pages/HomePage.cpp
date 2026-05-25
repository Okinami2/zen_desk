#include "HomePage.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDateTime>
#include <QPushButton>
#include <QFontDatabase>
#include <QApplication>
#include <QLabel>
#include <QTimer>
#include <QPen>

// ── 毛玻璃卡片容器（无真实模糊，用半透明+圆角模拟） ──────────────────────
class GlassCard : public QWidget {
public:
    explicit GlassCard(QWidget *parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_TranslucentBackground);
        setAutoFillBackground(false);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);

        QRectF r = rect().adjusted(1, 1, -1, -1);

        // 底层填充：半透明白
        p.setBrush(QColor(255, 255, 255, 45));
        p.drawRoundedRect(r, 20, 20);

        // 边框：半透明白边
        p.setPen(QPen(QColor(255, 255, 255, 70), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r, 20, 20);

        // 顶部光晕条
        QLinearGradient shine(0, 0, 0, r.height() * 0.4);
        shine.setColorAt(0, QColor(255, 255, 255, 50));
        shine.setColorAt(1, QColor(255, 255, 255, 0));

        p.setPen(Qt::NoPen);
        p.setBrush(shine);

        QPainterPath topHalf;
        topHalf.addRoundedRect(r, 20, 20);
        p.setClipPath(topHalf);
        p.drawRect(QRectF(0, 0, width(), height() * 0.4));
    }
};

HomePage::HomePage(QWidget *parent) : QWidget(parent)
{
    // 关键：父控件不自动刷底色，背景完全由 paintEvent 自己画
    setAttribute(Qt::WA_StyledBackground, false);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setAutoFillBackground(false);

    // ── 加载字体 ──────────────────────────────────────────────────────────
    int fid = QFontDatabase::addApplicationFont(":/fonts/Inter_28pt-Bold.ttf");
    QString numFamily = QFontDatabase::applicationFontFamilies(fid).value(0, "Inter");

    int fontIdRegular = QFontDatabase::addApplicationFont(":/fonts/SourceHanSansCN-Medium.otf");

    QString zhRegularFamily;

    if (fontIdRegular != -1) {
        zhRegularFamily = QFontDatabase::applicationFontFamilies(fontIdRegular).value(0);
    }

    // ── 根布局 ────────────────────────────────────────────────────────────
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── 顶部区域：天气占位（右上） ───────────────────────────────────────
    QHBoxLayout *topBar = new QHBoxLayout();
    topBar->setContentsMargins(0, 28, 36, 0);
    topBar->addStretch();

    QLabel *weatherLabel = new QLabel("Temp  22°C", this);
    weatherLabel->setAttribute(Qt::WA_TranslucentBackground);
    weatherLabel->setAutoFillBackground(false);
    weatherLabel->setStyleSheet(
        "QLabel {"
        "  color: rgba(255,255,255,0.80);"
        "  font-size: 22px;"
        "  font-family: 'Inter';"
        "  letter-spacing: 1px;"
        "  background: transparent;"
        "  border: none;"
        "}"
    );
    topBar->addWidget(weatherLabel);
    root->addLayout(topBar);

    // ── 中部：时钟 + 日期 ─────────────────────────────────────────────────
    root->addStretch(2);

    timeLabel = new QLabel("00:00", this);
    timeLabel->setAlignment(Qt::AlignCenter);
    timeLabel->setAttribute(Qt::WA_TranslucentBackground);
    timeLabel->setAutoFillBackground(false);
    timeLabel->setStyleSheet(QString(
        "QLabel {"
        "  font-family: '%1';"
        "  font-size: 168px;"
        "  font-weight: 700;"
        "  color: #FFFFFF;"
        "  letter-spacing: -4px;"
        "  background: transparent;"
        "  border: none;"
        "}"
    ).arg(numFamily));
    root->addWidget(timeLabel, 0, Qt::AlignHCenter);

    dateLabel = new QLabel(this);
    dateLabel->setAlignment(Qt::AlignCenter);
    dateLabel->setAttribute(Qt::WA_TranslucentBackground);
    dateLabel->setAutoFillBackground(false);
    dateLabel->setStyleSheet(
        "QLabel {"
        "  font-size: 26px;"
        "  color: rgba(255,255,255,0.72);"
        "  letter-spacing: 2px;"
        "  margin-top: -8px;"
        "  background: transparent;"
        "  border: none;"
        "}"
    );
    root->addWidget(dateLabel, 0, Qt::AlignHCenter);

    root->addStretch(3);

    // ── 底部：毛玻璃卡片（右侧） ─────────────────────────────────────────
    QHBoxLayout *bottomRow = new QHBoxLayout();
    bottomRow->setContentsMargins(0, 0, 52, 48);
    bottomRow->addStretch();

    glassCard = new GlassCard(this);
    glassCard->setFixedSize(360, 170);

    QVBoxLayout *cardLay = new QVBoxLayout(glassCard);
    cardLay->setContentsMargins(28, 20, 28, 20);
    cardLay->setSpacing(14);

    quoteLabel = new QLabel("心如止水，专注当下", glassCard);
    quoteLabel->setAlignment(Qt::AlignCenter);
    quoteLabel->setAttribute(Qt::WA_TranslucentBackground);
    quoteLabel->setAutoFillBackground(false);
    quoteLabel->setStyleSheet(
        "QLabel {"
        "  font-size: 20px;"
        "  color: rgba(255,255,255,0.90);"
        "  letter-spacing: 3px;"
        "  background: transparent;"
        "  border: none;"
        "}"
    );
    cardLay->addWidget(quoteLabel);

    //QPushButton *enterBtn = new QPushButton("▶  进入专注", glassCard);
    enterBtn = new QPushButton("▶  进入专注", glassCard); //  移除局部声明，使用成员变量
    enterBtn->setFixedHeight(52);
    enterBtn->setCursor(Qt::PointingHandCursor);
    enterBtn->setStyleSheet(
        "QPushButton {"
        "  background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "              stop:0 #4F46E5, stop:1 #6D28D9);"
        "  color: white;"
        "  font-size: 18px;"
        "  font-weight: 600;"
        "  border: none;"
        "  border-radius: 14px;"
        "  letter-spacing: 1px;"
        "}"
        "QPushButton:hover {"
        "  background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "              stop:0 #4338CA, stop:1 #5B21B6);"
        "}"
        "QPushButton:pressed {"
        "  background: #3730A3;"
        "}"
        /* 新增高亮边框样式 */
        "QPushButton[zenFocus=\"true\"] {"
        "  border: 3px solid #10B981;"
        "}"
    );
    connect(enterBtn, &QPushButton::clicked, this, &HomePage::enterStudyRequested);
    cardLay->addWidget(enterBtn);

    bottomRow->addWidget(glassCard);
    root->addLayout(bottomRow);

    // ── 时钟定时器 ────────────────────────────────────────────────────────
    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &HomePage::updateTime);
    timer->start(1000);
    updateTime();
}

// ── 背景：深邃渐变 ────────────────────────────────────────────────────────
void HomePage::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // 主渐变：深蓝→深紫→深墨
    QLinearGradient bg(0, 0, width(), height());
    bg.setColorAt(0.00, QColor(0x05, 0x06, 0x18));
    bg.setColorAt(0.35, QColor(0x0D, 0x11, 0x35));
    bg.setColorAt(0.70, QColor(0x1A, 0x0D, 0x2E));
    bg.setColorAt(1.00, QColor(0x07, 0x10, 0x1E));
    p.fillRect(rect(), bg);

    // 左上角星云光晕（靛蓝）
    QRadialGradient nebula1(width() * 0.18, height() * 0.22, 260);
    nebula1.setColorAt(0, QColor(0x4F, 0x46, 0xE5, 55));
    nebula1.setColorAt(1, QColor(0, 0, 0, 0));
    p.fillRect(rect(), nebula1);

    // 右下角光晕（深紫）
    QRadialGradient nebula2(width() * 0.82, height() * 0.80, 220);
    nebula2.setColorAt(0, QColor(0x7C, 0x3A, 0xED, 45));
    nebula2.setColorAt(1, QColor(0, 0, 0, 0));
    p.fillRect(rect(), nebula2);

    // 底部暗影渐变（增强卡片区域深度感）
    QLinearGradient vignette(0, height() * 0.55, 0, height());
    vignette.setColorAt(0, QColor(0, 0, 0, 0));
    vignette.setColorAt(1, QColor(0, 0, 0, 80));
    p.fillRect(rect(), vignette);
}

void HomePage::updateTime()
{
    const QDateTime now = QDateTime::currentDateTime();
    timeLabel->setText(now.toString("hh:mm"));
    dateLabel->setText(now.toString("yyyy年MM月dd日  dddd"));
}
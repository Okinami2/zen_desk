#include "StatsPage.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QWidget>
#include <QtMath>
#include <QFrame>

// ════════════════════════════════════════════════════════════
//  LineChartWidget
// ════════════════════════════════════════════════════════════
LineChartWidget::LineChartWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(0, 160);
}

void LineChartWidget::setData(const QVector<double> &d, const QStringList &labels)
{
    m_data = d;
    m_labels = labels;
    update();
}

void LineChartWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    if (m_data.isEmpty()) {
        p.setPen(QColor(0x9C, 0xA3, 0xAF));
        p.setFont(QFont("Inter", 12));
        p.drawText(rect(), Qt::AlignCenter, "暂无统计数据");
        return;
    }

    const int pad = 12;
    QRectF area(pad, pad, width() - pad*2, height() - pad*2 - 20);

    int n = m_data.size();
    auto ptX = [&](int i) {
        return n == 1 ? area.center().x() : area.left() + area.width() * i / (n - 1);
    };
    auto ptY = [&](double v) { return area.bottom() - area.height() * qBound(0.0, v, 1.0); };

    // 构造平滑贝塞尔路径
    QPainterPath linePath;
    linePath.moveTo(ptX(0), ptY(m_data[0]));
    if (n > 1) {
        for (int i = 1; i < n; ++i) {
            double cpx = (ptX(i) + ptX(i-1)) / 2.0;
            linePath.cubicTo(
                cpx, ptY(m_data[i-1]),
                cpx, ptY(m_data[i]),
                ptX(i), ptY(m_data[i])
            );
        }

        // 填充区域路径
        QPainterPath fillPath = linePath;
        fillPath.lineTo(ptX(n-1), area.bottom());
        fillPath.lineTo(ptX(0),   area.bottom());
        fillPath.closeSubpath();

        // 渐变填充
        QLinearGradient grad(0, area.top(), 0, area.bottom());
        grad.setColorAt(0, QColor(0x4F, 0x46, 0xE5, 100));
        grad.setColorAt(1, QColor(0x4F, 0x46, 0xE5, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(grad);
        p.drawPath(fillPath);
    }

    // 折线本身
    p.setPen(QPen(QColor(0x4F, 0x46, 0xE5), 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    if (n > 1) {
        p.drawPath(linePath);
    }

    // 数据点小圆
    for (int i = 0; i < n; ++i) {
        QPointF pt(ptX(i), ptY(m_data[i]));
        p.setPen(QPen(QColor(0x4F, 0x46, 0xE5), 2));
        p.setBrush(Qt::white);
        p.drawEllipse(pt, 4, 4);
    }

    p.setPen(QColor(0x6B, 0x72, 0x80));
    p.setFont(QFont("Inter", 10));
    for (int i = 0; i < n && i < m_labels.size(); ++i) {
        p.drawText(QRectF(ptX(i)-20, area.bottom()+4, 40, 18),
                   Qt::AlignCenter, m_labels[i]+"时");
    }
}

// ════════════════════════════════════════════════════════════
//  StackedBarWidget
// ════════════════════════════════════════════════════════════
StackedBarWidget::StackedBarWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(0, 100);
}

void StackedBarWidget::setData(const QVector<QVector<int>> &segs, const QStringList &labels)
{
    m_segs = segs; m_labels = labels; update();
}

void StackedBarWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    if (m_segs.isEmpty()) {
        p.setPen(QColor(0x9C, 0xA3, 0xAF));
        p.setFont(QFont("Inter", 12));
        p.drawText(rect(), Qt::AlignCenter, "暂无统计数据");
        return;
    }

    const int n = m_segs.size();
    const int padX = 12, padTop = 8, padBot = 24;
    const int barW = qMax(8, (width() - padX*2) / n - 6);
    const double totalH = height() - padTop - padBot;

    // 每列最大总秒数
    int maxTotal = 1;
    for (auto &seg : m_segs) {
        int s = 0; for (int v : seg) s += v;
        maxTotal = qMax(maxTotal, s);
    }

    const QColor colors[3] = {
        QColor(0x10, 0xB9, 0x81),  // 专注：绿
        QColor(0xF5, 0x9E, 0x0B),  // 走神：橙
        QColor(0xCB, 0xD5, 0xE1),  // 离座：灰蓝
    };

    for (int i = 0; i < n; ++i) {
        double cx = padX + (width() - padX*2) * (i + 0.5) / n;
        double x0 = cx - barW / 2.0;
        double y = height() - padBot;

        for (int layer = 0; layer < (int)m_segs[i].size() && layer < 3; ++layer) {
            double h = totalH * m_segs[i][layer] / maxTotal;
            if (h < 1) continue;
            QRectF r(x0, y - h, barW, h);
            // 顶层圆角
            QPainterPath path;
            if (layer == (int)m_segs[i].size() - 1
                || (layer+1 < (int)m_segs[i].size() && m_segs[i][layer+1] == 0)) {
                path.addRoundedRect(r, 4, 4);
                // 底部直角（只对最顶层圆角顶部）
                QPainterPath square;
                square.addRect(r.adjusted(0, 6, 0, 0));
                path = path.united(square);
            } else {
                path.addRect(r);
            }
            p.setPen(Qt::NoPen);
            p.setBrush(colors[layer]);
            p.drawPath(path);
            y -= h;
        }

        // X 标签
        if (i < m_labels.size()) {
            p.setPen(QColor(0x6B, 0x72, 0x80));
            p.setFont(QFont("Inter", 10));
            p.drawText(QRectF(x0, height()-padBot+4, barW, 18),
                       Qt::AlignCenter, m_labels[i]);
        }
    }
}


// ════════════════════════════════════════════════════════════
//  StatsPage
// ════════════════════════════════════════════════════════════
StatsPage::StatsPage(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, false);

    QHBoxLayout *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── 左侧主内容 ────────────────────────────────────────────────────────
    QWidget *leftPane = new QWidget();
    leftPane->setStyleSheet("background: transparent;");
    QVBoxLayout *leftLay = new QVBoxLayout(leftPane);
    leftLay->setContentsMargins(40, 36, 24, 36);
    leftLay->setSpacing(20);

    // 标题行
    QHBoxLayout *headerRow = new QHBoxLayout();
    QLabel *pageTitle = new QLabel("今日专注表现");
    pageTitle->setStyleSheet("font-size: 30px; font-weight: 700; color: #1F2937; background: transparent;");

    QWidget *totalCard = new QWidget();
    totalCard->setFixedHeight(48);
    totalCard->setStyleSheet("background: #EEF2FF; border-radius: 12px;");
    QHBoxLayout *tcLay = new QHBoxLayout(totalCard);
    tcLay->setContentsMargins(16, 0, 16, 0);
    QLabel *tcLabel = new QLabel("有效学习");
    tcLabel->setStyleSheet("font-size: 14px; color: #6366F1; background: transparent;");
    tcVal = new QLabel("0 小时 0 分");
    tcVal->setStyleSheet("font-size: 20px; font-weight: 700; color: #4F46E5; background: transparent;");
    tcLay->addWidget(tcLabel);
    tcLay->addSpacing(8);
    tcLay->addWidget(tcVal);

    headerRow->addWidget(pageTitle);
    headerRow->addStretch();
    headerRow->addWidget(totalCard);
    leftLay->addLayout(headerRow);

    // 折线图区域卡片
    QWidget *lineCard = new QWidget();
    lineCard->setStyleSheet("background: white; border-radius: 18px;");
    QVBoxLayout *lineLay = new QVBoxLayout(lineCard);
    lineLay->setContentsMargins(20, 16, 20, 10);
    lineLay->setSpacing(4);
    QLabel *lineTitle = new QLabel("专注度变化");
    lineTitle->setStyleSheet("font-size: 15px; color: #6B7280; background: transparent;");
    lineLay->addWidget(lineTitle);
    lineChart = new LineChartWidget(lineCard);
    lineLay->addWidget(lineChart, 1);
    leftLay->addWidget(lineCard, 3);

    // 柱状图区域卡片
    QWidget *barCard = new QWidget();
    barCard->setStyleSheet("background: white; border-radius: 18px;");
    QVBoxLayout *barLay = new QVBoxLayout(barCard);
    barLay->setContentsMargins(20, 14, 20, 8);
    barLay->setSpacing(4);

    QHBoxLayout *barHeader = new QHBoxLayout();
    QLabel *barTitle = new QLabel("时段分布");
    barTitle->setStyleSheet("font-size: 15px; color: #6B7280; background: transparent;");
    barHeader->addWidget(barTitle);
    barHeader->addStretch();
    // 图例
    auto makeLegend = [](const QString &text, const QColor &c) -> QWidget* {
        QWidget *w = new QWidget();
        QHBoxLayout *l = new QHBoxLayout(w);
        l->setContentsMargins(0,0,0,0);
        l->setSpacing(4);
        QLabel *dot = new QLabel();
        dot->setFixedSize(10, 10);
        dot->setStyleSheet(QString("background: %1; border-radius: 5px;").arg(c.name()));
        QLabel *txt = new QLabel(text);
        txt->setStyleSheet("font-size: 12px; color: #9CA3AF; background: transparent;");
        l->addWidget(dot);
        l->addWidget(txt);
        return w;
    };
    barHeader->addWidget(makeLegend("专注", QColor(0x10,0xB9,0x81)));
    barHeader->addSpacing(8);
    barHeader->addWidget(makeLegend("走神", QColor(0xF5,0x9E,0x0B)));
    barHeader->addSpacing(8);
    barHeader->addWidget(makeLegend("离座", QColor(0xCB,0xD5,0xE1)));
    barLay->addLayout(barHeader);

    barChart = new StackedBarWidget(barCard);
    barLay->addWidget(barChart, 1);
    leftLay->addWidget(barCard, 2);

    root->addWidget(leftPane, 62);

    // ── 分割线 ────────────────────────────────────────────────────────────
    QWidget *divider = new QWidget();
    divider->setFixedWidth(1);
    divider->setStyleSheet("background: #E5E7EB;");
    root->addWidget(divider);

    // ── 右侧信息列 ────────────────────────────────────────────────────────
    QWidget *rightPane = new QWidget();
    rightPane->setStyleSheet("background: transparent;");
    QVBoxLayout *rightLay = new QVBoxLayout(rightPane);
    rightLay->setContentsMargins(20, 36, 24, 36);
    rightLay->setSpacing(16);

    // 状态评级卡片
    QWidget *gradeCard = new QWidget();
    gradeCard->setStyleSheet("background: white; border-radius: 18px;");
    QVBoxLayout *gradeLay = new QVBoxLayout(gradeCard);
    gradeLay->setContentsMargins(20, 18, 20, 20);
    gradeLay->setSpacing(8);

    QLabel *gradeTitle = new QLabel("学习状态评估");
    gradeTitle->setStyleSheet("font-size: 14px; color: #6B7280; background: transparent;");

    gradeVal = new QLabel("A");
    gradeVal->setAlignment(Qt::AlignCenter);
    gradeVal->setFixedHeight(80);
    gradeVal->setStyleSheet(
        "font-size: 72px; font-weight: 900;"
        "color: #4F46E5;"
        "background: #EEF2FF;"
        "border-radius: 16px;"
        "letter-spacing: -2px;"
    );

    gradeDesc = new QLabel("状态良好，保持专注！");
    gradeDesc->setAlignment(Qt::AlignCenter);
    gradeDesc->setStyleSheet("font-size: 14px; color: #10B981; background: transparent;");

    gradeLay->addWidget(gradeTitle);
    gradeLay->addWidget(gradeVal);
    gradeLay->addWidget(gradeDesc);
    rightLay->addWidget(gradeCard);


    QWidget *postureCard = new QWidget();
    postureCard->setStyleSheet("background: white; border-radius: 18px;");
    QVBoxLayout *postureLay = new QVBoxLayout(postureCard);
    postureLay->setContentsMargins(20, 18, 20, 20);
    postureLay->setSpacing(10);

    QLabel *postureTitle = new QLabel("坐姿监测");
    postureTitle->setStyleSheet("font-size: 14px; color: #6B7280; background: transparent;");
    postureStateVal = new QLabel("等待数据");
    postureStateVal->setAlignment(Qt::AlignCenter);
    postureStateVal->setFixedHeight(44);
    postureStateVal->setStyleSheet(
        "font-size: 18px; font-weight: 800;"
        "color: #64748B;"
        "background: #F1F5F9;"
        "border-radius: 12px;"
    );
    postureDetailVal = new QLabel("pose 模型低频运行，约 2 秒刷新一次。");
    postureDetailVal->setWordWrap(true);
    postureDetailVal->setStyleSheet("font-size: 13px; color: #64748B; background: transparent;");
    postureMetricVal = new QLabel("肩斜 0.00 · 侧倾 0.00 · 低头 0.00 · 扶头 0.00");
    postureMetricVal->setWordWrap(true);
    postureMetricVal->setStyleSheet("font-size: 12px; color: #94A3B8; background: transparent;");
    postureAgeVal = new QLabel("未更新");
    postureAgeVal->setStyleSheet("font-size: 12px; color: #94A3B8; background: transparent;");

    postureLay->addWidget(postureTitle);
    postureLay->addWidget(postureStateVal);
    postureLay->addWidget(postureDetailVal);
    postureLay->addWidget(postureMetricVal);
    postureLay->addWidget(postureAgeVal);
    rightLay->addWidget(postureCard);

    // 干扰记录卡片
    QWidget *distCard = new QWidget();
    distCard->setStyleSheet("background: white; border-radius: 18px;");
    QVBoxLayout *distLay = new QVBoxLayout(distCard);
    distLay->setContentsMargins(20, 18, 20, 20);
    distLay->setSpacing(12);

    QLabel *distTitle = new QLabel("干扰记录");
    distTitle->setStyleSheet("font-size: 14px; color: #6B7280; background: transparent;");
    distLay->addWidget(distTitle);

    auto makeDistRow = [](const QString &icon, const QString &label, const QString &val, const QColor &vc, QLabel*& outLabel) -> QWidget* {
        QWidget *row = new QWidget();
        row->setStyleSheet("background: transparent;");
        QHBoxLayout *rl = new QHBoxLayout(row);
        rl->setContentsMargins(0,0,0,0);
        rl->setSpacing(8);
        QLabel *ico = new QLabel(icon);
        ico->setFixedSize(32,32);
        ico->setAlignment(Qt::AlignCenter);
        ico->setStyleSheet("font-size: 18px; background: #FEF3C7; border-radius: 8px;");
        QLabel *lbl = new QLabel(label);
        lbl->setStyleSheet("font-size: 14px; color: #6B7280; background: transparent;");
        outLabel = new QLabel(val);
        outLabel->setStyleSheet(QString("font-size: 20px; font-weight: 700; color: %1; background: transparent;").arg(vc.name()));
        rl->addWidget(ico);
        rl->addWidget(lbl);
        rl->addStretch();
        rl->addWidget(outLabel);
        return row;
    };

    distLay->addWidget(makeDistRow("😶", "走神次数", "0 次", QColor(0xF5,0x9E,0x0B), distractedCountVal));
    distLay->addWidget(makeDistRow("⏱", "走神时长", "0 分钟", QColor(0xEF,0x44,0x44), distractedTimeVal));
    distLay->addWidget(makeDistRow("🚶", "离座次数", "0 次", QColor(0x6B,0x72,0x80), absentCountVal));
    rightLay->addWidget(distCard);

    rightLay->addStretch();
    root->addWidget(rightPane, 38);
}

void StatsPage::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0xF4, 0xF5, 0xF7));
}

void StatsPage::updateStatsData(int totalSeconds, int absentCount, int distractedCount, int distractedSeconds)
{
    int effectiveSeconds = totalSeconds - distractedSeconds;
    if (effectiveSeconds < 0) effectiveSeconds = 0;

    int hours = effectiveSeconds / 3600;
    int minutes = (effectiveSeconds % 3600) / 60;
    
    tcVal->setText(QString("%1 小时 %2 分").arg(hours).arg(minutes));
    absentCountVal->setText(QString("%1 次").arg(absentCount));
    distractedCountVal->setText(QString("%1 次").arg(distractedCount));
    distractedTimeVal->setText(QString("%1 分钟").arg(distractedSeconds / 60));

    // 学习状态评估算法
    double focusRate = 100.0;
    if (totalSeconds > 0) {
        focusRate = (effectiveSeconds * 100.0) / totalSeconds;
    }
    double disturbanceScore = 100.0 - (distractedCount + absentCount) * 5.0;
    if (disturbanceScore < 0) disturbanceScore = 0;
    
    double finalScore = focusRate * 0.7 + disturbanceScore * 0.3;

    QString grade = "C";
    QString desc = "频繁分心，建议稍作休息调整状态。";
    QString color = "#EF4444"; // 红色
    QString bgColor = "#FEE2E2";

    if (finalScore >= 90 && absentCount == 0) {
        grade = "S";
        desc = "心如止水，极致专注！";
        color = "#8B5CF6"; // 紫色
        bgColor = "#EDE9FE";
    } else if (finalScore >= 80) {
        grade = "A";
        desc = "状态良好，继续保持！";
        color = "#10B981"; // 绿色
        bgColor = "#D1FAE5";
    } else if (finalScore >= 60) {
        grade = "B";
        desc = "表现及格，但还有提升空间。";
        color = "#F59E0B"; // 橙色
        bgColor = "#FEF3C7";
    }

    if (gradeVal && gradeDesc) {
        gradeVal->setText(grade);
        gradeVal->setStyleSheet(QString(
            "font-size: 72px; font-weight: 900;"
            "color: %1;"
            "background: %2;"
            "border-radius: 16px;"
            "letter-spacing: -2px;"
        ).arg(color).arg(bgColor));

        gradeDesc->setText(desc);
        gradeDesc->setStyleSheet(QString("font-size: 14px; color: %1; background: transparent;").arg(color));
    }
}

void StatsPage::updateTimelineData(const QVector<QVector<int>> &segments,
                                   const QStringList &labels,
                                   const QVector<double> &focusScores)
{
    if (lineChart) {
        lineChart->setData(focusScores, labels);
    }
    if (barChart) {
        barChart->setData(segments, labels);
    }
}


void StatsPage::updatePostureStatus(const QString &label, bool ok, bool present,
                                    double poseScore, double detectionScore,
                                    double ageMs, bool updated,
                                    double shoulderTilt, double bodyLean,
                                    double headDrop, double handSupportScore)
{
    QString title = "等待数据";
    QString detail = "pose 模型低频运行，约 2 秒刷新一次。";
    QString color = "#64748B";
    QString bgColor = "#F1F5F9";

    if (!present) {
        title = "未检测到人体";
        detail = "请确认上半身进入摄像头画面，或检查 pose detector 输出。";
        color = "#64748B";
        bgColor = "#F1F5F9";
    } else if (ok || label == "normal") {
        title = "坐姿规范";
        detail = "肩部基本水平，头部和躯干位置正常。";
        color = "#059669";
        bgColor = "#D1FAE5";
    } else if (label == "hunchback") {
        title = "疑似驼背";
        detail = "头部相对肩线过低，建议挺直背部并抬高视线。";
        color = "#DC2626";
        bgColor = "#FEE2E2";
    } else if (label == "shoulder_tilt") {
        title = "斜肩";
        detail = "左右肩高度差偏大，建议放松肩膀并调整坐姿。";
        color = "#EA580C";
        bgColor = "#FFEDD5";
    } else if (label == "hand_support_head") {
        title = "疑似单手撑头";
        detail = "手腕靠近头部，可能正在撑头，建议双手离开脸部。";
        color = "#DC2626";
        bgColor = "#FEE2E2";
    } else if (label == "lean_left") {
        title = "身体左倾";
        detail = "肩部中心相对髋部偏左，建议回到椅子中央。";
        color = "#EA580C";
        bgColor = "#FFEDD5";
    } else if (label == "lean_right") {
        title = "身体右倾";
        detail = "肩部中心相对髋部偏右，建议回到椅子中央。";
        color = "#EA580C";
        bgColor = "#FFEDD5";
    } else if (label == "head_offset") {
        title = "头部偏移";
        detail = "头部相对肩部中心偏移明显，建议回正头部。";
        color = "#EA580C";
        bgColor = "#FFEDD5";
    } else if (label == "unknown") {
        title = "关键点不足";
        detail = "肩部或头部关键点置信度不足，请检查画面遮挡和光照。";
        color = "#64748B";
        bgColor = "#F1F5F9";
    } else {
        title = label.isEmpty() ? "等待数据" : label;
        detail = "收到未知坐姿标签，请结合调试 dump 检查后处理映射。";
        color = "#64748B";
        bgColor = "#F1F5F9";
    }

    if (postureStateVal) {
        postureStateVal->setText(title);
        postureStateVal->setStyleSheet(QString(
            "font-size: 18px; font-weight: 800;"
            "color: %1;"
            "background: %2;"
            "border-radius: 12px;"
        ).arg(color).arg(bgColor));
    }
    if (postureDetailVal) {
        postureDetailVal->setText(detail);
    }
    if (postureMetricVal) {
        postureMetricVal->setText(QString("肩斜 %1 · 侧倾 %2 · 低头 %3 · 扶头 %4 · pose %5/det %6")
            .arg(shoulderTilt, 0, 'f', 2)
            .arg(bodyLean, 0, 'f', 2)
            .arg(headDrop, 0, 'f', 2)
            .arg(handSupportScore, 0, 'f', 2)
            .arg(poseScore, 0, 'f', 2)
            .arg(detectionScore, 0, 'f', 2));
    }
    if (postureAgeVal) {
        postureAgeVal->setText(QString("%1 · 结果年龄 %2 ms")
            .arg(updated ? "刚更新" : "缓存结果")
            .arg(ageMs, 0, 'f', 0));
    }
}

#include "StatsPage.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QWidget>
#include <QtMath>

// ════════════════════════════════════════════════════════════
//  LineChartWidget
// ════════════════════════════════════════════════════════════
LineChartWidget::LineChartWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(0, 160);
    // 模拟 mock 数据（8 个时段）
    m_data = {0.55, 0.72, 0.88, 0.91, 0.76, 0.62, 0.84, 0.93};
}

void LineChartWidget::setData(const QVector<double> &d) { m_data = d; update(); }

void LineChartWidget::paintEvent(QPaintEvent *)
{
    if (m_data.size() < 2) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int pad = 12;
    QRectF area(pad, pad, width() - pad*2, height() - pad*2 - 20);

    int n = m_data.size();
    auto ptX = [&](int i) { return area.left() + area.width() * i / (n - 1); };
    auto ptY = [&](double v) { return area.bottom() - area.height() * v; };

    // 构造平滑贝塞尔路径
    QPainterPath linePath;
    linePath.moveTo(ptX(0), ptY(m_data[0]));
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

    // 折线本身
    p.setPen(QPen(QColor(0x4F, 0x46, 0xE5), 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(linePath);

    // 数据点小圆
    for (int i = 0; i < n; ++i) {
        QPointF pt(ptX(i), ptY(m_data[i]));
        p.setPen(QPen(QColor(0x4F, 0x46, 0xE5), 2));
        p.setBrush(Qt::white);
        p.drawEllipse(pt, 4, 4);
    }

    // X 轴标签（时段）
    QStringList labels = {"08","09","10","11","12","13","14","15"};
    p.setPen(QColor(0x6B, 0x72, 0x80));
    p.setFont(QFont("Inter", 10));
    for (int i = 0; i < n && i < labels.size(); ++i) {
        p.drawText(QRectF(ptX(i)-20, area.bottom()+4, 40, 18),
                   Qt::AlignCenter, labels[i]+"时");
    }
}

// ════════════════════════════════════════════════════════════
//  StackedBarWidget
// ════════════════════════════════════════════════════════════
StackedBarWidget::StackedBarWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(0, 100);
    // mock: {专注, 走神, 离座} 分钟
    m_segs  = {{45,8,7},{42,5,13},{55,3,2},{58,2,0},{38,12,10},{30,15,15},{50,5,5},{57,2,1}};
    m_labels = QStringList{"08","09","10","11","12","13","14","15"};
}

void StackedBarWidget::setData(const QVector<QVector<int>> &segs, const QStringList &labels)
{
    m_segs = segs; m_labels = labels; update();
}

void StackedBarWidget::paintEvent(QPaintEvent *)
{
    if (m_segs.isEmpty()) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int n = m_segs.size();
    const int padX = 12, padTop = 8, padBot = 24;
    const int barW = qMax(8, (width() - padX*2) / n - 6);
    const double totalH = height() - padTop - padBot;

    // 每列最大总分钟
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
    QLabel *tcVal = new QLabel("3 小时 42 分");
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
    LineChartWidget *lineChart = new LineChartWidget(lineCard);
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

    StackedBarWidget *barChart = new StackedBarWidget(barCard);
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

    QLabel *gradeVal = new QLabel("A");
    gradeVal->setAlignment(Qt::AlignCenter);
    gradeVal->setFixedHeight(80);
    gradeVal->setStyleSheet(
        "font-size: 72px; font-weight: 900;"
        "color: #4F46E5;"
        "background: #EEF2FF;"
        "border-radius: 16px;"
        "letter-spacing: -2px;"
    );

    QLabel *gradeDesc = new QLabel("状态良好，保持专注！");
    gradeDesc->setAlignment(Qt::AlignCenter);
    gradeDesc->setStyleSheet("font-size: 14px; color: #10B981; background: transparent;");

    gradeLay->addWidget(gradeTitle);
    gradeLay->addWidget(gradeVal);
    gradeLay->addWidget(gradeDesc);
    rightLay->addWidget(gradeCard);

    // 干扰记录卡片
    QWidget *distCard = new QWidget();
    distCard->setStyleSheet("background: white; border-radius: 18px;");
    QVBoxLayout *distLay = new QVBoxLayout(distCard);
    distLay->setContentsMargins(20, 18, 20, 20);
    distLay->setSpacing(12);

    QLabel *distTitle = new QLabel("干扰记录");
    distTitle->setStyleSheet("font-size: 14px; color: #6B7280; background: transparent;");
    distLay->addWidget(distTitle);

    auto makeDistRow = [](const QString &icon, const QString &label, const QString &val, const QColor &vc) -> QWidget* {
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
        QLabel *v = new QLabel(val);
        v->setStyleSheet(QString("font-size: 20px; font-weight: 700; color: %1; background: transparent;").arg(vc.name()));
        rl->addWidget(ico);
        rl->addWidget(lbl);
        rl->addStretch();
        rl->addWidget(v);
        return row;
    };

    distLay->addWidget(makeDistRow("😶", "发呆次数", "5 次", QColor(0xF5,0x9E,0x0B)));
    distLay->addWidget(makeDistRow("⏱", "走神时长", "12 分钟", QColor(0xEF,0x44,0x44)));
    distLay->addWidget(makeDistRow("🚶", "离座次数", "2 次", QColor(0x6B,0x72,0x80)));
    rightLay->addWidget(distCard);

    rightLay->addStretch();
    root->addWidget(rightPane, 38);
}

void StatsPage::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0xF4, 0xF5, 0xF7));
}

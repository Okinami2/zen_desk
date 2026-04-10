#ifndef STATSPAGE_H
#define STATSPAGE_H

#include <QWidget>
#include <QLabel>
#include <QVector>

// ── 折线图 Widget ─────────────────────────────────────────────────────────
class LineChartWidget : public QWidget {
    Q_OBJECT
public:
    explicit LineChartWidget(QWidget *parent = nullptr);
    void setData(const QVector<double> &focusScores); // 0.0~1.0，每小时一个点

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QVector<double> m_data;
};

// ── 堆叠柱状图 Widget ────────────────────────────────────────────────────
class StackedBarWidget : public QWidget {
    Q_OBJECT
public:
    explicit StackedBarWidget(QWidget *parent = nullptr);
    // 每个 bar: {focus, distract, away}  单位分钟
    void setData(const QVector<QVector<int>> &segments, const QStringList &labels);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QVector<QVector<int>> m_segs;
    QStringList m_labels;
};

// ── 主统计页 ──────────────────────────────────────────────────────────────
class StatsPage : public QWidget {
    Q_OBJECT
public:
    explicit StatsPage(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *) override;
};

#endif // STATSPAGE_H

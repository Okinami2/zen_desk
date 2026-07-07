#ifndef STATSPAGE_H
#define STATSPAGE_H

#include <QWidget>
#include <QLabel>
#include <QVector>
#include <QString>

// ── 折线图 Widget ─────────────────────────────────────────────────────────
class LineChartWidget : public QWidget {
    Q_OBJECT
public:
    explicit LineChartWidget(QWidget *parent = nullptr);
    void setData(const QVector<double> &focusScores, const QStringList &labels); // 0.0~1.0，每小时一个点

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QVector<double> m_data;
    QStringList m_labels;
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
    void updateStatsData(int totalSeconds, int absentCount, int distractedCount, int distractedSeconds);
    void updateTimelineData(const QVector<QVector<int>> &segments,
                            const QStringList &labels,
                            const QVector<double> &focusScores);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QLabel *tcVal;
    QLabel *absentCountVal;
    QLabel *distractedCountVal;
    QLabel *distractedTimeVal;
    QLabel *gradeVal;
    QLabel *gradeDesc;
    LineChartWidget *lineChart;
    StackedBarWidget *barChart;
};

#endif // STATSPAGE_H

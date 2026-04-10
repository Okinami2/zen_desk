#ifndef STUDYPAGE_H
#define STUDYPAGE_H

#include <QWidget>
#include <QTimer>
#include <QLabel>
#include <QPropertyAnimation>

// ── 圆环进度条（自绘） ────────────────────────────────────────────────────
class RingProgressWidget : public QWidget {
    Q_OBJECT
    Q_PROPERTY(double progress READ progress WRITE setProgress)
public:
    explicit RingProgressWidget(QWidget *parent = nullptr);
    double progress() const { return m_progress; }
    void setProgress(double v);          // 0.0 ~ 1.0
    void setCountdown(bool isCountdown);
    void setRingColor(const QColor &c);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    double m_progress = 1.0;
    bool   m_countdown = true;
    QColor m_ringColor{0x4F, 0x46, 0xE5};
};

// ── 学习模式主页 ──────────────────────────────────────────────────────────
class StudyPage : public QWidget {
    Q_OBJECT
public:
    explicit StudyPage(QWidget *parent = nullptr);

    void startTimer(int minutes);   // <0 = 正计时
    void stopTimer();

signals:
    void studyFinished();   // 倒计时结束 / 用户点击结束

private slots:
    void tick();
    void showTip(const QString &icon, const QString &text);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    RingProgressWidget *ring;
    QLabel *timeDisplay;
    QLabel *statusLabel;
    QLabel *tipWidget;
    QLabel *pomodoroLabel;   // 番茄计数 "番茄钟 1/4"

    QTimer *timer;
    QTimer *tipHideTimer;
    QPropertyAnimation *tipAnim;

    int  seconds    = 0;
    int  totalSecs  = 0;
    bool isCountdown = true;
    int  pomodoroCount = 1;
};

#endif // STUDYPAGE_H

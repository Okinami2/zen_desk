#ifndef HOMEPAGE_H
#define HOMEPAGE_H

#include <QWidget>
#include <QLabel>
#include <QTimer>

class HomePage : public QWidget
{
    Q_OBJECT
public:
    explicit HomePage(QWidget *parent = nullptr);

signals:
    void enterStudyRequested();

private slots:
    void updateTime();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QLabel *timeLabel;
    QLabel *dateLabel;
    QLabel *quoteLabel;
    QTimer *timer;

    // 毛玻璃卡片（手动绘制背景，QLabel 只做文字）
    QWidget *glassCard;
};

#endif // HOMEPAGE_H

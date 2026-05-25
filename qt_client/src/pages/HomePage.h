#ifndef HOMEPAGE_H
#define HOMEPAGE_H

#include <QWidget>
#include <QLabel>
#include <QTimer>
#include <QPushButton> // 新增：解决 QPushButton 未定义的报错

class HomePage : public QWidget
{
    Q_OBJECT
public:
    explicit HomePage(QWidget *parent = nullptr);
    QPushButton* getEnterBtn() const { return enterBtn; } // 新增 Getter

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

    QPushButton *enterBtn; // 新增成员变量
};

#endif // HOMEPAGE_H

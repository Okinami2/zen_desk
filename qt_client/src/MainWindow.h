#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStackedWidget>
#include <QPushButton>
#include <QLabel>

class HomePage;
class StatsPage;
class StudyPage;

// ── 侧边栏导航按钮（带活跃态指示条） ─────────────────────────────────────
class NavButton : public QPushButton {
    Q_OBJECT
public:
    explicit NavButton(const QString &icon, QWidget *parent = nullptr);
    void setActive(bool active);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    bool m_active = false;
    QString m_icon;
};

// ── 半透明遮罩（弹窗背景） ───────────────────────────────────────────────
class Overlay : public QWidget {
    Q_OBJECT
public:
    explicit Overlay(QWidget *parent = nullptr);

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void showHome();
    void showStats();
    void showStudySetupDialog();
    void startStudy(int minutes = -1);
    void stopStudy();

private:
    QStackedWidget *stack;

    HomePage  *homePage;
    StatsPage *statsPage;
    StudyPage *studyPage;

    NavButton *btnHome;
    NavButton *btnStats;
    NavButton *btnConfig;

    bool inStudyMode = false;

    void setActiveNav(NavButton *active);
};

#endif // MAINWINDOW_H

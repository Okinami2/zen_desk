#!/usr/bin/env bash
# 文件名建议： generate_initial_qt_pages.sh
# 运行前请 cd 到 qt_client 目录

set -e

mkdir -p src/pages
mkdir -p src/components   # 暂时空着，后续可放计时器等

echo "正在生成初版页面框架..."

# =================== src/main.cpp ===================
cat > src/main.cpp << 'EOF'
#include "MainWindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    // w.showFullScreen();  // 嵌入式建议打开
    return a.exec();
}
EOF

# =================== src/MainWindow.h ===================
cat > src/MainWindow.h << 'EOF'
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStackedWidget>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class HomePage;
class StatsPage;
class StudyPage;
class QPushButton;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onHomeBtnClicked();
    void onStatsBtnClicked();
    void onStudyBtnClicked();
    void startStudySession(int minutes = -1);

private:
    Ui::MainWindow *ui;

    QStackedWidget *stack;

    HomePage  *homePage;
    StatsPage *statsPage;
    StudyPage *studyPage;

    QPushButton *btnHome;
    QPushButton *btnStats;
    QPushButton *btnStudy;

    bool isStudying = false;
};

#endif // MAINWINDOW_H
EOF

# =================== src/MainWindow.cpp ===================
cat > src/MainWindow.cpp << 'EOF'
#include "MainWindow.h"
#include "pages/HomePage.h"
#include "pages/StatsPage.h"
#include "pages/StudyPage.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QInputDialog>
#include <QMessageBox>
#include <QTimer>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowTitle("Pegasus Desk");
    resize(1024, 600);

    stack = new QStackedWidget(this);

    homePage  = new HomePage(this);
    statsPage = new StatsPage(this);
    studyPage = new StudyPage(this);

    stack->addWidget(homePage);
    stack->addWidget(statsPage);
    stack->addWidget(studyPage);

    // 底部导航
    QWidget *navBar = new QWidget();
    QHBoxLayout *navLayout = new QHBoxLayout(navBar);
    navLayout->setContentsMargins(30, 8, 30, 8);
    navLayout->setSpacing(60);

    btnHome  = new QPushButton("首页");
    btnStats = new QPushButton("统计");
    btnStudy = new QPushButton("开始学习");

    QString btnStyle = "QPushButton { font-size: 18px; padding: 12px 24px; border-radius: 8px; }";
    btnHome->setStyleSheet(btnStyle);
    btnStats->setStyleSheet(btnStyle);
    btnStudy->setStyleSheet(btnStyle + "background-color: #4CAF50; color: white;");

    navLayout->addWidget(btnHome);
    navLayout->addWidget(btnStats);
    navLayout->addWidget(btnStudy);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addWidget(stack);
    mainLayout->addWidget(navBar);

    QWidget *container = new QWidget();
    container->setLayout(mainLayout);
    setCentralWidget(container);

    connect(btnHome,  &QPushButton::clicked, this, &MainWindow::onHomeBtnClicked);
    connect(btnStats, &QPushButton::clicked, this, &MainWindow::onStatsBtnClicked);
    connect(btnStudy, &QPushButton::clicked, this, &MainWindow::onStudyBtnClicked);

    stack->setCurrentWidget(homePage);
}

void MainWindow::onHomeBtnClicked() {
    stack->setCurrentWidget(homePage);
    btnStudy->setText("开始学习");
    isStudying = false;
}

void MainWindow::onStatsBtnClicked() {
    if (isStudying) {
        QMessageBox::information(this, "提示", "学习中无法查看统计");
        return;
    }
    stack->setCurrentWidget(statsPage);
}

void MainWindow::onStudyBtnClicked() {
    if (isStudying) {
        studyPage->stopTimer();
        isStudying = false;
        btnStudy->setText("开始学习");
        stack->setCurrentWidget(homePage);
        return;
    }

    QStringList items{"正计时", "25分钟番茄", "45分钟", "自定义..."};
    bool ok;
    QString choice = QInputDialog::getItem(this, "选择计时模式", "模式：", items, 0, false, &ok);
    if (!ok) return;

    int minutes = -1;

    if (choice == "25分钟番茄") minutes = 25;
    else if (choice == "45分钟")  minutes = 45;
    else if (choice == "自定义...") {
        minutes = QInputDialog::getInt(this, "自定义时长", "请输入分钟数：", 30, 5, 480, 1, &ok);
        if (!ok) return;
    }

    startStudySession(minutes);
}

void MainWindow::startStudySession(int minutes) {
    isStudying = true;
    btnStudy->setText("结束学习");
    studyPage->startTimer(minutes);
    stack->setCurrentWidget(studyPage);
}

MainWindow::~MainWindow() {
    delete ui;
}
EOF

# =================== src/pages/HomePage.h ===================
cat > src/pages/HomePage.h << 'EOF'
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

private slots:
    void updateTime();

private:
    QLabel *timeLabel;
    QLabel *dateLabel;
    QTimer *timer;
};

#endif // HOMEPAGE_H
EOF

# =================== src/pages/HomePage.cpp ===================
cat > src/pages/HomePage.cpp << 'EOF'
#include "HomePage.h"
#include <QVBoxLayout>
#include <QDateTime>

HomePage::HomePage(QWidget *parent) : QWidget(parent)
{
    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->setAlignment(Qt::AlignCenter);
    lay->setSpacing(20);

    timeLabel = new QLabel("00:00:00");
    timeLabel->setStyleSheet("font-size: 140px; color: white; font-weight: bold;");

    dateLabel = new QLabel(QDate::currentDate().toString("yyyy年MM月dd日  dddd"));
    dateLabel->setStyleSheet("font-size: 42px; color: #e0e0e0;");

    lay->addWidget(timeLabel);
    lay->addWidget(dateLabel);

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &HomePage::updateTime);
    timer->start(1000);
    updateTime();

    // 后续可加壁纸
    // setStyleSheet("background-image: url(:/images/wallpaper.jpg); background-position: center;");
}

void HomePage::updateTime()
{
    timeLabel->setText(QTime::currentTime().toString("hh:mm:ss"));
}
EOF

# =================== src/pages/StatsPage.h ===================
cat > src/pages/StatsPage.h << 'EOF'
#ifndef STATSPAGE_H
#define STATSPAGE_H

#include <QWidget>
#include <QLabel>

class StatsPage : public QWidget
{
    Q_OBJECT
public:
    explicit StatsPage(QWidget *parent = nullptr);
};

#endif // STATSPAGE_H
EOF

# =================== src/pages/StatsPage.cpp ===================
cat > src/pages/StatsPage.cpp << 'EOF'
#include "StatsPage.h"
#include <QVBoxLayout>

StatsPage::StatsPage(QWidget *parent) : QWidget(parent)
{
    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->setAlignment(Qt::AlignCenter);

    QLabel *label = new QLabel("学习统计页面（待实现）\n\n后续放置学习曲线、柱状图、走神次数等");
    label->setStyleSheet("font-size: 36px; color: #ffffff;");
    label->setWordWrap(true);
    lay->addWidget(label);
}
EOF

# =================== src/pages/StudyPage.h ===================
cat > src/pages/StudyPage.h << 'EOF'
#ifndef STUDYPAGE_H
#define STUDYPAGE_H

#include <QWidget>
#include <QTimer>
#include <QLabel>

class StudyPage : public QWidget
{
    Q_OBJECT
public:
    explicit StudyPage(QWidget *parent = nullptr);

    void startTimer(int minutes);   // <0 为正计时
    void stopTimer();

private slots:
    void updateTimer();

private:
    QLabel *timerLabel;
    QTimer *timer;
    int remainingSeconds = 0;
    bool isCountdown = true;
};

#endif // STUDYPAGE_H
EOF

# =================== src/pages/StudyPage.cpp ===================
cat > src/pages/StudyPage.cpp << 'EOF'
#include "StudyPage.h"
#include <QVBoxLayout>
#include <QTime>

StudyPage::StudyPage(QWidget *parent) : QWidget(parent)
{
    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->setAlignment(Qt::AlignCenter);
    lay->setSpacing(40);

    timerLabel = new QLabel("00:00:00");
    timerLabel->setStyleSheet("font-size: 180px; color: #4CAF50; font-weight: bold;");

    QLabel *status = new QLabel("专注中 · 请保持良好坐姿");
    status->setStyleSheet("font-size: 32px; color: white;");

    lay->addWidget(timerLabel);
    lay->addWidget(status);

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &StudyPage::updateTimer);
}

void StudyPage::startTimer(int minutes)
{
    if (minutes < 0) {
        // 正计时
        isCountdown = false;
        remainingSeconds = 0;
        timerLabel->setStyleSheet("font-size: 180px; color: #2196F3;");
    } else {
        // 倒计时
        isCountdown = true;
        remainingSeconds = minutes * 60;
        timerLabel->setStyleSheet("font-size: 180px; color: #4CAF50;");
    }

    updateTimer();
    timer->start(1000);
}

void StudyPage::stopTimer()
{
    timer->stop();
    timerLabel->setText("00:00:00");
}

void StudyPage::updateTimer()
{
    if (isCountdown) {
        if (remainingSeconds <= 0) {
            timer->stop();
            timerLabel->setText("00:00:00");
            // 后续可加提醒
            return;
        }
        remainingSeconds--;
        QTime t(0, 0, remainingSeconds);
        timerLabel->setText(t.toString("hh:mm:ss"));
    } else {
        // 正计时
        remainingSeconds++;
        QTime t(0, 0, remainingSeconds);
        timerLabel->setText(t.toString("hh:mm:ss"));
    }
}
EOF

echo "文件生成完成！"

echo ""
echo "接下来建议执行的步骤："
echo "1. 确认 qt_client.pro 中包含了所有 .cpp 文件（或者用 wildcard）"
echo "2. 在项目根目录运行 qmake （或直接用 qt creator 打开 qt_client.pro）"
echo "3. 编译运行，观察 1024×600 效果"
echo "4. 后续可逐步："
echo "   - 用 Qt Designer 替换代码布局 → .ui 文件"
echo "   - 加 QSS 美化"
echo "   - 接入 MockFusionClient 数据"
echo ""
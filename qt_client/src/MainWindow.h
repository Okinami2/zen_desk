#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStackedWidget>
#include <QPushButton>
#include <QLabel>
#include <QEvent>     // 补充必需的头文件
#include <QKeyEvent>  // 补充必需的头文件

// 定义四大交互层级
enum InteractionLayer {
    LAYER_HOME_BROWSE,  // 浏览模式
    LAYER_HOME_FOCUS,   // 聚焦模式
    LAYER_DIALOG,       // 弹窗设置模式
    LAYER_STUDYING      // 专注计时模式
};

// 定义滑块专属状态
enum SliderState {
    SLIDER_HOVER,       // 仅高亮(绿)
    SLIDER_EDIT         // 调节中(红)
};

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

protected:
    // 拦截全局事件
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void showHome();
    void showStats();
    void showStudySetupDialog();
    void startStudy(int minutes = -1);
    void stopStudy();
    void closeActiveDialog(); // 统一安全的弹窗清理函数

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

    //  状态机核心成员
    InteractionLayer currentLayer = LAYER_HOME_BROWSE;
    
    // 弹窗相关状态追踪
    QWidget *activeDialogPanel = nullptr;
    Overlay *activeOverlay = nullptr;
    int *activeSelectedMin = nullptr;
    // 移除原来的 dialogFocusItems 和 dialogTab
    // 新增：分层级的追踪变量
    int dialogFocusLevel = 0;   // 0: 顶部Tab栏, 1: 下方内容区
    int dialogActiveTab = 0;    // 0: 番茄钟, 1: 正计时
    QList<QWidget*> dialogTabHeaders; // 存放顶部的两个按钮
    QList<QWidget*> dialogPomoItems;  // 存放番茄钟页面的组件链
    QList<QWidget*> dialogStopItems;  // 存放正计时页面的组件链
    int currentDialogFocusIndex = 0;
    
    SliderState currentSliderState = SLIDER_HOVER;
    class QSlider *dialogSlider = nullptr;
    // class QTabWidget *dialogTab = nullptr;
    class QStackedWidget *dialogStackedWidget = nullptr; // 替代原先的 QTabWidget

    // 计时页焦点追踪 (0:暂停, 1:结束)
    int studyFocusIndex = 0; 

    // UI刷新辅助函数
    void updateWidgetFocusStyle(QWidget* w, bool focused, const QString& extraProp = "", bool extraVal = false);
    void handleDialogKey(int key);
};

#endif // MAINWINDOW_H

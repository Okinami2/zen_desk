#include "MainWindow.h"
#include "pages/HomePage.h"
#include "pages/StatsPage.h"
#include "pages/StudyPage.h"

#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFile>
#include <QDialog>
#include <QTabWidget>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QParallelAnimationGroup>

#include <QApplication>
#include <QStyle>

// ════════════════════════════════════════════════════════════
//  NavButton
// ════════════════════════════════════════════════════════════
NavButton::NavButton(const QString &icon, QWidget *parent)
    : QPushButton(parent), m_icon(icon)
{
    setFixedSize(80, 72);
    setCursor(Qt::PointingHandCursor);
    setStyleSheet("background: transparent; border: none;");
}

void NavButton::setActive(bool active)
{
    m_active = active;
    update();
}

void NavButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    if (m_active) {
        // 活跃态：左侧蓝色指示条 + 浅色背景
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x4F, 0x46, 0xE5, 200));
        p.drawRoundedRect(QRectF(0, height()/2.0-20, 3, 40), 2, 2);

        p.setBrush(QColor(255,255,255,14));
        p.drawRoundedRect(QRectF(6, 6, width()-12, height()-12), 12, 12);
    } else if (underMouse()) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255,255,255,8));
        p.drawRoundedRect(QRectF(6, 6, width()-12, height()-12), 12, 12);
    }

    // Icon 文字
    p.setPen(m_active ? QColor(255,255,255,230) : QColor(255,255,255,110));
    p.setFont(QFont("", 26));
    p.drawText(rect(), Qt::AlignCenter, m_icon);
}

// ════════════════════════════════════════════════════════════
//  Overlay
// ════════════════════════════════════════════════════════════
Overlay::Overlay(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setGeometry(parent->rect());
}

void Overlay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 140));
}

void Overlay::mousePressEvent(QMouseEvent *event)
{
    emit clicked();
    QWidget::mousePressEvent(event);
}

// ════════════════════════════════════════════════════════════
//  MainWindow
// ════════════════════════════════════════════════════════════
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle("Pegasus Desk");
    resize(1024, 600);
    // showFullScreen();  // 端侧打开

    QFile file(":/style.qss");
    if (file.open(QFile::ReadOnly)) {
        setStyleSheet(QLatin1String(file.readAll()));
        file.close();
    }

    // ── 根布局：侧边栏 + 内容区 ───────────────────────────────────────────
    QWidget *central = new QWidget(this);
    QHBoxLayout *rootLay = new QHBoxLayout(central);
    rootLay->setContentsMargins(0,0,0,0);
    rootLay->setSpacing(0);

    // ── 侧边栏（半透明深色） ──────────────────────────────────────────────
    QWidget *sidebar = new QWidget();
    sidebar->setFixedWidth(80);
    sidebar->setStyleSheet("background: rgba(8,10,28,0.72);");
    QVBoxLayout *sideLay = new QVBoxLayout(sidebar);
    sideLay->setContentsMargins(0, 32, 0, 32);
    sideLay->setSpacing(0);
    sideLay->setAlignment(Qt::AlignTop);

    // Logo 点
    QLabel *logo = new QLabel("◈");
    logo->setAlignment(Qt::AlignCenter);
    logo->setFixedHeight(52);
    logo->setStyleSheet("color: rgba(79,70,229,0.85); font-size: 22px;");
    sideLay->addWidget(logo);
    sideLay->addSpacing(24);

    btnHome   = new NavButton("○",  sidebar);
    btnStats  = new NavButton("▦",  sidebar);

    sideLay->addWidget(btnHome);
    sideLay->addWidget(btnStats);
    sideLay->addStretch();

    // ── 内容区 ────────────────────────────────────────────────────────────
    stack = new QStackedWidget();

    homePage  = new HomePage(this);
    statsPage = new StatsPage(this);
    studyPage = new StudyPage(this);

    stack->addWidget(homePage);
    stack->addWidget(statsPage);
    stack->addWidget(studyPage);

    rootLay->addWidget(sidebar);
    rootLay->addWidget(stack);
    setCentralWidget(central);

    // ── 信号连接 ──────────────────────────────────────────────────────────
    connect(btnHome,   &QPushButton::clicked, this, &MainWindow::showHome);
    connect(btnStats,  &QPushButton::clicked, this, &MainWindow::showStats);

    // HomePage 中的"进入专注"按钮
    connect(homePage, &HomePage::enterStudyRequested, this, &MainWindow::showStudySetupDialog);

    // StudyPage 完成信号
    connect(studyPage, &StudyPage::studyFinished, this, &MainWindow::stopStudy);

    // ... 原构造函数结尾 ...
    connect(studyPage, &StudyPage::studyFinished, this, &MainWindow::stopStudy);

    // 全局接管事件：此句是突破“焦点陷阱”的核心！
    qApp->installEventFilter(this);

    showHome();
}

void MainWindow::setActiveNav(NavButton *active)
{
    for (NavButton *b : {btnHome, btnStats})
        b->setActive(b == active);
}

void MainWindow::showHome()
{
    if (inStudyMode) return;
    stack->setCurrentWidget(homePage);
    setActiveNav(btnHome);
}

void MainWindow::showStats()
{
    if (inStudyMode) return;
    stack->setCurrentWidget(statsPage);
    setActiveNav(btnStats);
}

// ── 专注设置弹窗（精心设计版） ────────────────────────────────────────────
// ==================== 替换代码 ====================
void MainWindow::showStudySetupDialog()
{
    if (inStudyMode || activeDialogPanel != nullptr) return;

    // 状态初始化
    dialogTabHeaders.clear();
    dialogPomoItems.clear();
    dialogStopItems.clear();
    activeSelectedMin = new int(25);

    // 遮罩层
    activeOverlay = new Overlay(this->centralWidget());
    activeOverlay->resize(this->centralWidget()->size());
    activeOverlay->show();
    activeOverlay->raise();

    // 弹窗容器
    activeDialogPanel = new QWidget(this->centralWidget());
    activeDialogPanel->setFixedSize(460, 370);
    activeDialogPanel->setObjectName("studySetupPanel");
    activeDialogPanel->setStyleSheet(R"(
        QWidget#studySetupPanel {
            background: #1E1E2E;
            border-radius: 24px;
            border: 1px solid rgba(255,255,255,0.10);
        }
        QWidget#studySetupPanel QWidget { background: transparent; border: none; }
        QWidget#studySetupPanel QLabel { background: transparent; color: #E2E8F0; border: none; }
    )");

    int cx = (this->centralWidget()->width() - activeDialogPanel->width()) / 2;
    int cy = (this->centralWidget()->height() - activeDialogPanel->height()) / 2;
    activeDialogPanel->move(cx, cy);
    activeDialogPanel->show();
    activeDialogPanel->raise();

    QVBoxLayout *lay = new QVBoxLayout(activeDialogPanel);
    lay->setContentsMargins(32, 24, 32, 28);
    lay->setSpacing(0);

    // ── 顶部标题 ─────────────────────────────
    QLabel *title = new QLabel("开始专注");
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("font-size: 22px; font-weight: 700;");
    lay->addWidget(title);
    lay->addSpacing(20);

    // ──  自定义 Tab 头 (替代原先的 QTabWidget) ─────────────────
    QHBoxLayout *tabHeaderLay = new QHBoxLayout();
    tabHeaderLay->setContentsMargins(0, 0, 0, 0);
    tabHeaderLay->setSpacing(4);
    
    QString tabBtnStyle = R"(
        QPushButton {
            background: rgba(255,255,255,0.05); color: rgba(255,255,255,0.45);
            padding: 10px 28px; font-size: 15px; border-radius: 10px; border: none;
        }
        /* 选中态：底色变紫，文字变亮 */
        QPushButton[activeTab="true"] {
            background: rgba(79,70,229,0.35); color: #ffffff; font-weight: 600;
        }
        /* 旋钮焦点态：强烈的绿框！ */
        QPushButton[zenFocus="true"] {
            border: 3px solid #10B981; padding: 7px 25px; /* 补足边距避免抖动 */
        }
    )";

    QPushButton *tabBtnPomo = new QPushButton("番茄钟");
    QPushButton *tabBtnStop = new QPushButton("正计时");
    tabBtnPomo->setStyleSheet(tabBtnStyle);
    tabBtnStop->setStyleSheet(tabBtnStyle);
    
    tabHeaderLay->addWidget(tabBtnPomo);
    tabHeaderLay->addWidget(tabBtnStop);
    tabHeaderLay->addStretch(); // 靠左对齐
    lay->addLayout(tabHeaderLay);
    lay->addSpacing(16);

    // 加入 Tab 头追踪链 (Level 0)
    dialogTabHeaders.append(tabBtnPomo);
    dialogTabHeaders.append(tabBtnStop);

    // ── 替代的 QStackedWidget 容器 ─────────────────────────────
    dialogStackedWidget = new QStackedWidget();
    lay->addWidget(dialogStackedWidget);

    // ── Tab1：番茄钟内容 ───────────────────────────────────────
    QWidget *tabPomo = new QWidget();
    QVBoxLayout *pomoLay = new QVBoxLayout(tabPomo);
    pomoLay->setContentsMargins(0, 4, 0, 0);
    pomoLay->setSpacing(12);

    QHBoxLayout *chipsRow = new QHBoxLayout();
    chipsRow->setSpacing(10);
    static const QList<QPair<QString,int>> presets = { {"25 分",25}, {"45 分",45}, {"60 分",60} };

    QList<QPushButton*> chipBtns;
    for (auto &[label, min] : presets) {
        QPushButton *chip = new QPushButton(label);
        chip->setFixedHeight(44);
        chip->setCheckable(true);
        chip->setChecked(min == 25);
        chip->setProperty("chipMin", min);
        chip->setStyleSheet(
            "QPushButton {"
            "  background: rgba(255,255,255,0.07); color: rgba(255,255,255,0.60);"
            "  border: 1px solid rgba(255,255,255,0.10); border-radius: 12px; font-size: 15px;"
            "}"
            "QPushButton:checked { background: rgba(79,70,229,0.50); color: white; border-color: #4F46E5; font-weight: 700; }"
            "QPushButton[zenFocus=\"true\"] { border: 3px solid #10B981; }" 
        );
        chipBtns.append(chip);
        chipsRow->addWidget(chip);
        dialogPomoItems.append(chip); // 放入番茄钟内容链
    }
    pomoLay->addLayout(chipsRow);
    
    QHBoxLayout *sliderRow = new QHBoxLayout();
    QLabel *sliderMinLabel = new QLabel("5");
    dialogSlider = new QSlider(Qt::Horizontal);
    dialogSlider->setRange(5, 120);
    dialogSlider->setValue(25);
    dialogSlider->setStyleSheet(R"(
        QSlider::groove:horizontal { height: 4px; background: rgba(255,255,255,0.12); border-radius: 2px; }
        QSlider::sub-page:horizontal { background: #4F46E5; border-radius: 2px; }
        QSlider::handle:horizontal { width: 18px; height: 18px; margin: -7px 0; background: white; border-radius: 9px; border: 2px solid #4F46E5; }
        QSlider[zenFocus="true"]::handle:horizontal { border: 3px solid #10B981; margin: -8px -2px; width: 22px; height: 22px; border-radius: 11px; }
        QSlider[sliderEdit="true"]::handle:horizontal { border: 3px solid #EF4444; background: #EF4444; }
    )");
    
    QLabel *sliderMaxLabel = new QLabel("120");
    QLabel *sliderValLabel = new QLabel("25 分钟");
    sliderValLabel->setAlignment(Qt::AlignCenter);

    connect(dialogSlider, &QSlider::valueChanged, activeDialogPanel, [=](int v) {
        *activeSelectedMin = v;
        sliderValLabel->setText(QString("%1 分钟").arg(v));
        for (QPushButton *b : chipBtns) b->setChecked(b->property("chipMin").toInt() == v);
    });

    for (QPushButton *b : chipBtns) {
        connect(b, &QPushButton::clicked, activeDialogPanel, [=]() {
            dialogSlider->blockSignals(true);
            dialogSlider->setValue(b->property("chipMin").toInt());
            dialogSlider->blockSignals(false);
            *activeSelectedMin = b->property("chipMin").toInt();
            sliderValLabel->setText(QString("%1 分钟").arg(*activeSelectedMin));
            for (QPushButton *other : chipBtns) other->setChecked(other == b);
        });
    }

    sliderRow->addWidget(sliderMinLabel);
    sliderRow->addWidget(dialogSlider, 1);
    sliderRow->addWidget(sliderMaxLabel);
    pomoLay->addLayout(sliderRow);
    pomoLay->addWidget(sliderValLabel);
    dialogStackedWidget->addWidget(tabPomo);
    dialogPomoItems.append(dialogSlider); // 放入番茄钟内容链

    // ── Tab2：正计时内容 ───────────────────────────────────────
    QWidget *tabStop = new QWidget();
    QVBoxLayout *stopLay = new QVBoxLayout(tabStop);
    stopLay->setAlignment(Qt::AlignCenter);
    QLabel *stopDesc = new QLabel("不限时长，专注到你说停");
    stopDesc->setAlignment(Qt::AlignCenter);
    stopDesc->setStyleSheet("font-size: 16px; color: rgba(255,255,255,0.45);");
    stopLay->addWidget(stopDesc);
    dialogStackedWidget->addWidget(tabStop);

    // ── 开始专注按钮 (跨越两页共有) ────────────────────────────────
    lay->addSpacing(4);
    QPushButton *startBtn = new QPushButton("▶   开始专注");
    startBtn->setFixedHeight(56);
    startBtn->setStyleSheet(
        "QPushButton {"
        "  background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #4F46E5, stop:1 #7C3AED);"
        "  color: white; font-size: 18px; font-weight: 700; border: none; border-radius: 16px;"
        "}"
        "QPushButton[zenFocus=\"true\"] { border: 3px solid #10B981; }"
    );
    lay->addWidget(startBtn);
    
    // 开始按钮不管在哪一页，都是链路的最后一环
    dialogPomoItems.append(startBtn);
    dialogStopItems.append(startBtn);

    connect(startBtn, &QPushButton::clicked, this, [this]() {
        int min = (dialogActiveTab == 0) ? *activeSelectedMin : -1;
        closeActiveDialog();
        startStudy(min);
    });

    QGraphicsOpacityEffect *eff = new QGraphicsOpacityEffect(activeDialogPanel);
    activeDialogPanel->setGraphicsEffect(eff);
    QPropertyAnimation *fadeIn = new QPropertyAnimation(eff, "opacity");
    fadeIn->setStartValue(0.0);
    fadeIn->setEndValue(1.0);
    fadeIn->setDuration(220);
    fadeIn->start(QAbstractAnimation::DeleteWhenStopped);

    // 🚀 初始化弹窗焦点状态：完全符合你的直觉设计！
    currentLayer = LAYER_DIALOG;
    dialogFocusLevel = 0;   // 0层级：焦点锁定在顶部的Tab标题
    dialogActiveTab = 0;    // 默认停留在“番茄钟”
    dialogStackedWidget->setCurrentIndex(0);
    
    // 初始化UI显示：番茄钟按钮既有紫底（被激活）也有绿框（当前有焦点），正计时暗去
    updateWidgetFocusStyle(dialogTabHeaders[0], true, "activeTab", true);
    updateWidgetFocusStyle(dialogTabHeaders[1], false, "activeTab", false);
}
// =======================================================

// ==================== 替换代码 ====================
void MainWindow::startStudy(int minutes)
{
    inStudyMode = true;
    studyPage->startTimer(minutes);
    stack->setCurrentWidget(studyPage); 
    btnHome->setActive(false);
    btnStats->setActive(false);
    
    // 强制切入第三层级
    currentLayer = LAYER_STUDYING;
    studyFocusIndex = 0; // 0代表暂停按钮，1代表结束按钮
    updateWidgetFocusStyle(studyPage->getPauseBtn(), true);
    updateWidgetFocusStyle(studyPage->getStopBtn(), false);
}

void MainWindow::stopStudy()
{
    inStudyMode = false;
    studyPage->stopTimer();
    stack->setCurrentWidget(homePage);
    setActiveNav(btnHome);
    
    // 彻底重置状态，安全退回底层第一层级浏览模式
    currentLayer = LAYER_HOME_BROWSE; 
    updateWidgetFocusStyle(studyPage->getPauseBtn(), false);
    updateWidgetFocusStyle(studyPage->getStopBtn(), false);
    updateWidgetFocusStyle(homePage->getEnterBtn(), false);
}
// =======================================================

// ==================== 底部新增代码 ====================

// 样式强刷新辅助工具
void MainWindow::updateWidgetFocusStyle(QWidget* w, bool focused, const QString& extraProp, bool extraVal) {
    if (!w) return;
    w->setProperty("zenFocus", focused);
    if (!extraProp.isEmpty()) {
        w->setProperty(extraProp.toStdString().c_str(), extraVal);
    }
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

// 弹窗安全销毁
void MainWindow::closeActiveDialog() {
    if (activeSelectedMin) { delete activeSelectedMin; activeSelectedMin = nullptr; }
    if (activeOverlay) { activeOverlay->deleteLater(); activeOverlay = nullptr; }
    if (activeDialogPanel) { activeDialogPanel->deleteLater(); activeDialogPanel = nullptr; }
    
    //  清空并重置分层状态机
    dialogTabHeaders.clear();
    dialogPomoItems.clear();
    dialogStopItems.clear();
    dialogSlider = nullptr;
    dialogStackedWidget = nullptr;
    dialogFocusLevel = 0;
    dialogActiveTab = 0;
    currentDialogFocusIndex = 0;
    
    // 退回第一层级聚焦态
    if (!inStudyMode) {
        currentLayer = LAYER_HOME_FOCUS;
        updateWidgetFocusStyle(homePage->getEnterBtn(), true);
    }
}
// =======================================================

//  核心引擎：拦截并分发底层键盘事件
bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *ke = static_cast<QKeyEvent*>(event);
        int key = ke->key();
        
        // 我们只接管四大天王按键
        if (key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Space || key == Qt::Key_Return || key == Qt::Key_Escape) {
            
            if (key == Qt::Key_Return) key = Qt::Key_Space; // 统一回车和空格为触发键
            
            switch (currentLayer) {
                // 【层级 1.1：主界浏览】
                case LAYER_HOME_BROWSE:
                    if (key == Qt::Key_Left || key == Qt::Key_Right) {
                        if (stack->currentWidget() == homePage) showStats();
                        else showHome();
                    } else if (key == Qt::Key_Space) {
                        if (stack->currentWidget() == homePage) {
                            currentLayer = LAYER_HOME_FOCUS;
                            updateWidgetFocusStyle(homePage->getEnterBtn(), true);
                        }
                    }
                    return true;
                    
                // 【层级 1.2：主页聚焦】
                case LAYER_HOME_FOCUS:
                    if (key == Qt::Key_Space) {
                        homePage->getEnterBtn()->click(); // 触发后弹出窗口，状态机自动切到 LAYER_DIALOG
                    } else if (key == Qt::Key_Escape) {
                        currentLayer = LAYER_HOME_BROWSE;
                        updateWidgetFocusStyle(homePage->getEnterBtn(), false);
                    }
                    return true; // 屏蔽左右旋钮
                    
                // 【层级 2：专注弹窗设置】
                case LAYER_DIALOG:
                    handleDialogKey(key);
                    return true;
                    
                // 【层级 3：计时中】
                case LAYER_STUDYING:
                    if (key == Qt::Key_Left || key == Qt::Key_Right) {
                        updateWidgetFocusStyle(studyFocusIndex == 0 ? studyPage->getPauseBtn() : studyPage->getStopBtn(), false);
                        studyFocusIndex = 1 - studyFocusIndex; // 0和1之间横跳
                        updateWidgetFocusStyle(studyFocusIndex == 0 ? studyPage->getPauseBtn() : studyPage->getStopBtn(), true);
                    } else if (key == Qt::Key_Space) {
                        if (studyFocusIndex == 0) studyPage->getPauseBtn()->click();
                        else studyPage->getStopBtn()->click();
                    }
                    // 彻底无视 Esc
                    return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

// 弹窗内的复杂流转引擎
// ==================== 替换代码 ====================
// 弹窗内的层级交互流转
void MainWindow::handleDialogKey(int key) {
    //  优先响应：滑块 B 阶段 (调节数值中)
    if (currentSliderState == SLIDER_EDIT) {
        if (key == Qt::Key_Left) {
            dialogSlider->setValue(dialogSlider->value() - 5);
        } else if (key == Qt::Key_Right) {
            dialogSlider->setValue(dialogSlider->value() + 5);
        } else if (key == Qt::Key_Space || key == Qt::Key_Escape) {
            currentSliderState = SLIDER_HOVER;
            updateWidgetFocusStyle(dialogSlider, true, "sliderEdit", false); // 恢复绿框
        }
        return;
    }
    
    //  Level 0: 焦点在顶部的 Tab 头
    if (dialogFocusLevel == 0) {
        if (key == Qt::Key_Left || key == Qt::Key_Right) {
            // 剥夺旧Tab的选中紫底和绿色焦点
            updateWidgetFocusStyle(dialogTabHeaders[dialogActiveTab], false, "activeTab", false);
            // 切换 (0变1，1变0)
            dialogActiveTab = 1 - dialogActiveTab;
            // 赋给新Tab选中紫底和绿色焦点，并联动切换显示页面
            updateWidgetFocusStyle(dialogTabHeaders[dialogActiveTab], true, "activeTab", true);
            dialogStackedWidget->setCurrentIndex(dialogActiveTab);
            
        } else if (key == Qt::Key_Space) {
            // [按空格下沉]：进入内容区的控件流转 (Level 1)
            dialogFocusLevel = 1;
            currentDialogFocusIndex = 0;
            // Tab头保留紫色选中底，但剥夺绿色焦点框！
            updateWidgetFocusStyle(dialogTabHeaders[dialogActiveTab], false, "activeTab", true);
            // 给当前选中页面的第一个组件加上绿色焦点框
            QList<QWidget*>& currentList = (dialogActiveTab == 0) ? dialogPomoItems : dialogStopItems;
            updateWidgetFocusStyle(currentList[currentDialogFocusIndex], true);
            
        } else if (key == Qt::Key_Escape) {
            // [按Esc退出]：直接销毁弹窗
            closeActiveDialog();
        }
        return;
    }
    
    //  Level 1: 焦点在底下的内容区
    if (dialogFocusLevel == 1) {
        QList<QWidget*>& currentList = (dialogActiveTab == 0) ? dialogPomoItems : dialogStopItems;
        QWidget* currentItem = currentList[currentDialogFocusIndex];
        
        if (key == Qt::Key_Left) {
            updateWidgetFocusStyle(currentItem, false);
            currentDialogFocusIndex = (currentDialogFocusIndex - 1 + currentList.size()) % currentList.size();
            updateWidgetFocusStyle(currentList[currentDialogFocusIndex], true);
        } else if (key == Qt::Key_Right) {
            updateWidgetFocusStyle(currentItem, false);
            currentDialogFocusIndex = (currentDialogFocusIndex + 1) % currentList.size();
            updateWidgetFocusStyle(currentList[currentDialogFocusIndex], true);
        } else if (key == Qt::Key_Space) {
            if (currentItem == dialogSlider) {
                currentSliderState = SLIDER_EDIT;
                updateWidgetFocusStyle(dialogSlider, true, "sliderEdit", true); // 变红框进入调节
            } else if (QPushButton* btn = qobject_cast<QPushButton*>(currentItem)) {
                btn->click(); // 触发 25/45，或者直接触发最底部的"开始专注"
            }
        } else if (key == Qt::Key_Escape) {
            // [按Esc返回层级]：内容区的焦点全部抹去，退回顶部 Tab 头
            updateWidgetFocusStyle(currentItem, false);
            dialogFocusLevel = 0;
            // 恢复 Tab 头的绿色焦点框
            updateWidgetFocusStyle(dialogTabHeaders[dialogActiveTab], true, "activeTab", true);
        }
    }
}
// =======================================================




MainWindow::~MainWindow() {}

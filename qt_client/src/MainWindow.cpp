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
void MainWindow::showStudySetupDialog()
{
    if (inStudyMode) {
        stopStudy();
        return;
    }

    // 遮罩层
    Overlay *overlay = new Overlay(this->centralWidget());
    overlay->resize(this->centralWidget()->size());
    overlay->show();
    overlay->raise();

    // 让遮罩可点击关闭
    overlay->installEventFilter(this);

    // 弹窗容器
    QWidget *panel = new QWidget(this->centralWidget());
    panel->setFixedSize(460, 370);
    panel->setObjectName("studySetupPanel");
    panel->setStyleSheet(R"(
        QWidget#studySetupPanel {
            background: #1E1E2E;
            border-radius: 24px;
            border: 1px solid rgba(255,255,255,0.10);
        }
    
        QWidget#studySetupPanel QWidget {
            background: transparent;
            border: none;
        }
    
        QWidget#studySetupPanel QLabel {
            background: transparent;
            color: #E2E8F0;
            border: none;
        }
    
        QWidget#studySetupPanel QTabWidget::pane {
            border: none;
            background: transparent;
        }
    
        QWidget#studySetupPanel QTabBar::tab {
            background: rgba(255,255,255,0.05);
            color: rgba(255,255,255,0.45);
            padding: 10px 28px;
            font-size: 15px;
            border-radius: 10px;
            margin-right: 4px;
            border: none;
        }
    
        QWidget#studySetupPanel QTabBar::tab:selected {
            background: rgba(79,70,229,0.35);
            color: #ffffff;
            font-weight: 600;
        }
    
        QWidget#studySetupPanel QTabBar::tab:hover:!selected {
            background: rgba(255,255,255,0.10);
        }
    )");

    int cx = (this->centralWidget()->width() - panel->width()) / 2;
    int cy = (this->centralWidget()->height() - panel->height()) / 2;
    panel->move(cx, cy);
    panel->show();
    panel->raise();

    QVBoxLayout *lay = new QVBoxLayout(panel);
    lay->setContentsMargins(32, 24, 32, 28);
    lay->setSpacing(0);

    // ── 顶部栏：标题 + 关闭按钮 ─────────────────────────────
    QHBoxLayout *headerLay = new QHBoxLayout();
    headerLay->setContentsMargins(0, 0, 0, 0);
    headerLay->setSpacing(0);

    headerLay->addStretch();

    QLabel *title = new QLabel("开始专注");
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(
        "font-size: 22px;"
        "font-weight: 700;"
        "color: #E2E8F0;"
        "background: transparent;"
    );
    headerLay->addWidget(title);

    headerLay->addStretch();

    QPushButton *closeBtn = new QPushButton("✕");
    closeBtn->setFixedSize(32, 32);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(
        "QPushButton {"
        "  background: transparent;"
        "  color: rgba(255,255,255,0.65);"
        "  border: none;"
        "  border-radius: 16px;"
        "  font-size: 16px;"
        "  font-weight: 600;"
        "}"
        "QPushButton:hover {"
        "  background: rgba(255,255,255,0.08);"
        "  color: white;"
        "}"
        "QPushButton:pressed {"
        "  background: rgba(255,255,255,0.14);"
        "}"
    );
    headerLay->addWidget(closeBtn, 0, Qt::AlignRight);

    lay->addLayout(headerLay);
    lay->addSpacing(20);

    // ── Tab 切换 ───────────────────────────────────────────
    QTabWidget *tab = new QTabWidget();
    tab->setStyleSheet(R"(
        QTabWidget::pane { border: none; background: transparent; }
        QTabBar::tab {
            background: rgba(255,255,255,0.05);
            color: rgba(255,255,255,0.45);
            padding: 10px 28px;
            font-size: 15px;
            border-radius: 10px;
            margin-right: 4px;
        }
        QTabBar::tab:selected {
            background: rgba(79,70,229,0.35);
            color: #ffffff;
            font-weight: 600;
        }
        QTabBar::tab:hover:!selected {
            background: rgba(255,255,255,0.10);
        }
    )");
    lay->addWidget(tab);
    lay->addSpacing(16);

    // ── Tab1：番茄钟 ───────────────────────────────────────
    QWidget *tabPomo = new QWidget();
    tabPomo->setStyleSheet("background: transparent;");
    QVBoxLayout *pomoLay = new QVBoxLayout(tabPomo);
    pomoLay->setContentsMargins(0, 16, 0, 0);
    pomoLay->setSpacing(12);

    QHBoxLayout *chipsRow = new QHBoxLayout();
    chipsRow->setSpacing(10);
    static const QList<QPair<QString,int>> presets = {
        {"25 分",25}, {"45 分",45}, {"60 分",60}
    };

    int *selectedMin = new int(25);
    QList<QPushButton*> chipBtns;

    for (auto &[label, min] : presets) {
        QPushButton *chip = new QPushButton(label);
        chip->setFixedHeight(44);
        chip->setCheckable(true);
        chip->setChecked(min == 25);
        chip->setProperty("chipMin", min);
        chip->setStyleSheet(
            "QPushButton {"
            "  background: rgba(255,255,255,0.07);"
            "  color: rgba(255,255,255,0.60);"
            "  border: 1px solid rgba(255,255,255,0.10);"
            "  border-radius: 12px;"
            "  font-size: 15px;"
            "}"
            "QPushButton:checked {"
            "  background: rgba(79,70,229,0.50);"
            "  color: white;"
            "  border-color: #4F46E5;"
            "  font-weight: 700;"
            "}"
            "QPushButton:hover:!checked {"
            "  background: rgba(255,255,255,0.12);"
            "}"
        );
        chipBtns.append(chip);
        chipsRow->addWidget(chip);
    }

    for (QPushButton *b : chipBtns) {
        connect(b, &QPushButton::clicked, panel, [b, chipBtns, selectedMin]() {
            *selectedMin = b->property("chipMin").toInt();
            for (QPushButton *other : chipBtns)
                other->setChecked(other == b);
        });
    }

    pomoLay->addLayout(chipsRow);

    QHBoxLayout *sliderRow = new QHBoxLayout();
    QLabel *sliderMinLabel = new QLabel("5");
    sliderMinLabel->setStyleSheet(
        "color: rgba(255,255,255,0.35);"
        "font-size: 13px;"
        "background: transparent;"
    );

    QSlider *slider = new QSlider(Qt::Horizontal);
    slider->setRange(5, 120);
    slider->setValue(25);
    slider->setStyleSheet(R"(
        QSlider::groove:horizontal {
            height: 4px;
            background: rgba(255,255,255,0.12);
            border-radius: 2px;
        }
        QSlider::sub-page:horizontal {
            background: #4F46E5;
            border-radius: 2px;
        }
        QSlider::handle:horizontal {
            width: 18px;
            height: 18px;
            margin: -7px 0;
            background: white;
            border-radius: 9px;
            border: 2px solid #4F46E5;
        }
    )");

    QLabel *sliderMaxLabel = new QLabel("120");
    sliderMaxLabel->setStyleSheet(
        "color: rgba(255,255,255,0.35);"
        "font-size: 13px;"
        "background: transparent;"
    );

    QLabel *sliderValLabel = new QLabel("25 分钟");
    sliderValLabel->setAlignment(Qt::AlignCenter);
    sliderValLabel->setStyleSheet(
        "color: rgba(255,255,255,0.55);"
        "font-size: 13px;"
        "background: transparent;"
    );

    connect(slider, &QSlider::valueChanged, panel, [sliderValLabel, selectedMin, chipBtns](int v) {
        *selectedMin = v;
        sliderValLabel->setText(QString("%1 分钟").arg(v));
        for (QPushButton *b : chipBtns)
            b->setChecked(b->property("chipMin").toInt() == v);
    });

    for (QPushButton *b : chipBtns) {
        connect(b, &QPushButton::clicked, panel, [b, slider]() {
            slider->blockSignals(true);
            slider->setValue(b->property("chipMin").toInt());
            slider->blockSignals(false);
        });
    }

    sliderRow->addWidget(sliderMinLabel);
    sliderRow->addWidget(slider, 1);
    sliderRow->addWidget(sliderMaxLabel);

    pomoLay->addLayout(sliderRow);
    pomoLay->addWidget(sliderValLabel);

    tab->addTab(tabPomo, "番茄钟");

    // ── Tab2：正计时 ───────────────────────────────────────
    QWidget *tabStop = new QWidget();
    tabStop->setStyleSheet("background: transparent;");
    QVBoxLayout *stopLay = new QVBoxLayout(tabStop);
    stopLay->setContentsMargins(0, 20, 0, 0);
    stopLay->setAlignment(Qt::AlignCenter);

    QLabel *stopDesc = new QLabel("不限时长，专注到你说停");
    stopDesc->setAlignment(Qt::AlignCenter);
    stopDesc->setStyleSheet(
        "font-size: 16px;"
        "color: rgba(255,255,255,0.45);"
        "background: transparent;"
    );
    stopLay->addWidget(stopDesc);

    tab->addTab(tabStop, "正计时");

    // ── 开始按钮 ───────────────────────────────────────────
    lay->addSpacing(4);

    QPushButton *startBtn = new QPushButton("▶   开始专注");
    startBtn->setFixedHeight(56);
    startBtn->setCursor(Qt::PointingHandCursor);
    startBtn->setStyleSheet(
        "QPushButton {"
        "  background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "              stop:0 #4F46E5, stop:1 #7C3AED);"
        "  color: white;"
        "  font-size: 18px;"
        "  font-weight: 700;"
        "  border: none;"
        "  border-radius: 16px;"
        "  letter-spacing: 2px;"
        "}"
        "QPushButton:hover {"
        "  background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "              stop:0 #4338CA, stop:1 #6D28D9);"
        "}"
        "QPushButton:pressed {"
        "  background: #3730A3;"
        "}"
    );
    lay->addWidget(startBtn);

    // 统一关闭逻辑
    auto closeDialog = [=]() {
        if (selectedMin) {
            delete selectedMin;
        }
        overlay->deleteLater();
        panel->deleteLater();
    };

    connect(closeBtn, &QPushButton::clicked, panel, [=]() {
        closeDialog();
    });

    connect(startBtn, &QPushButton::clicked, panel, [=]() {
        int min = -1;
        int tabIdx = tab->currentIndex();
        if (tabIdx == 0)
            min = *selectedMin;

        delete selectedMin;
        overlay->deleteLater();
        panel->deleteLater();
        startStudy(min);
    });

    // 入场淡入
    QGraphicsOpacityEffect *eff = new QGraphicsOpacityEffect(panel);
    panel->setGraphicsEffect(eff);

    QPropertyAnimation *fadeIn = new QPropertyAnimation(eff, "opacity");
    fadeIn->setStartValue(0.0);
    fadeIn->setEndValue(1.0);
    fadeIn->setDuration(220);
    fadeIn->start(QAbstractAnimation::DeleteWhenStopped);

    // 点击遮罩关闭：给 overlay 加一个简单点击事件
    connect(overlay, &Overlay::clicked, panel, [=]() {
        closeDialog();
    });
}

void MainWindow::startStudy(int minutes)
{
    inStudyMode = true;
    studyPage->startTimer(minutes);
    stack->setCurrentWidget(studyPage); // 学习时高亮配置/语音按钮（无对应Nav则全不亮）
    btnHome->setActive(false);
    btnStats->setActive(false);
}

void MainWindow::stopStudy()
{
    inStudyMode = false;
    studyPage->stopTimer();
    stack->setCurrentWidget(homePage);
    setActiveNav(btnHome);
}

MainWindow::~MainWindow() {}

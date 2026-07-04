#include "ControlPage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>

ControlPage::ControlPage(QWidget *parent) : QWidget(parent)
{
    this->setObjectName("ControlPage");
    this->setAttribute(Qt::WA_StyledBackground, true);
    this->setStyleSheet("#ControlPage { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #0F172A, stop:1 #1E1B4B); }");

    QVBoxLayout *mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(60, 60, 60, 60);
    mainLay->setSpacing(30);

    // 标题
    QLabel *title = new QLabel("控制中心", this);
    title->setStyleSheet("font-size: 32px; font-weight: bold; color: white;");
    title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    mainLay->addWidget(title);

    // 亮度卡片
    QWidget *brightnessCard = new QWidget(this);
    brightnessCard->setStyleSheet(
        "QWidget {"
        "  background: rgba(255,255,255,0.05);"
        "  border-radius: 20px;"
        "}"
    );
    QVBoxLayout *cardLay = new QVBoxLayout(brightnessCard);
    cardLay->setContentsMargins(30, 30, 30, 30);
    cardLay->setSpacing(20);

    QLabel *cardTitle = new QLabel("台灯亮度调节", brightnessCard);
    cardTitle->setStyleSheet("font-size: 18px; color: rgba(255,255,255,0.6); background: transparent;");

    QHBoxLayout *sliderLay = new QHBoxLayout();
    brightnessSlider = new QSlider(Qt::Horizontal, brightnessCard);
    brightnessSlider->setRange(0, 100);
    brightnessSlider->setValue(50);
    brightnessSlider->setStyleSheet(R"(
        QSlider::groove:horizontal { height: 8px; background: rgba(255,255,255,0.12); border-radius: 4px; }
        QSlider::sub-page:horizontal { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #4F46E5, stop:1 #7C3AED); border-radius: 4px; }
        QSlider::handle:horizontal { width: 24px; height: 24px; margin: -8px 0; background: white; border-radius: 12px; border: 3px solid #4F46E5; }
        QSlider[zenFocus="true"]::handle:horizontal { border: 4px solid #10B981; }
    )");

    brightnessValLabel = new QLabel("50%", brightnessCard);
    brightnessValLabel->setStyleSheet("font-size: 24px; font-weight: bold; color: white; background: transparent;");
    brightnessValLabel->setFixedWidth(80);
    brightnessValLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    sliderLay->addWidget(brightnessSlider);
    sliderLay->addWidget(brightnessValLabel);

    cardLay->addWidget(cardTitle);
    cardLay->addLayout(sliderLay);

    mainLay->addWidget(brightnessCard);

    // 色温切换大按钮
    toggleColorBtn = new QPushButton("点击确认，切换色温 (冷 / 暖)", this);
    toggleColorBtn->setFixedHeight(72);
    toggleColorBtn->setStyleSheet(R"(
        QPushButton {
            background: rgba(255,255,255,0.05);
            border: 2px solid rgba(255,255,255,0.1);
            border-radius: 20px;
            color: white;
            font-size: 20px;
            font-weight: bold;
        }
        QPushButton:pressed {
            background: rgba(255,255,255,0.1);
        }
        QPushButton[zenFocus="true"] {
            border: 3px solid #10B981;
            background: rgba(16,185,129,0.15);
        }
    )");

    mainLay->addWidget(toggleColorBtn);
    mainLay->addStretch();

    // 信号绑定
    connect(brightnessSlider, &QSlider::valueChanged, this, [this](int v) {
        brightnessValLabel->setText(QString("%1%").arg(v));
        emit brightnessChanged(v);
    });

    connect(toggleColorBtn, &QPushButton::clicked, this, &ControlPage::toggleColorTempRequested);
}

void ControlPage::setBrightness(int percent)
{
    brightnessSlider->blockSignals(true);
    brightnessSlider->setValue(percent);
    brightnessValLabel->setText(QString("%1%").arg(percent));
    brightnessSlider->blockSignals(false);
}

#include "ControlPage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStyle>
#include <QVariant>

ControlPage::ControlPage(QWidget *parent) : QWidget(parent), focusIndex(0), inEditMode(false)
{
    this->setObjectName("ControlPage");
    this->setAttribute(Qt::WA_StyledBackground, true);
    this->setStyleSheet("#ControlPage { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #0F172A, stop:1 #1E1B4B); }");

    QVBoxLayout *mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(60, 40, 60, 40);
    mainLay->setSpacing(20);

    // 标题
    QLabel *title = new QLabel("控制中心", this);
    title->setStyleSheet("font-size: 32px; font-weight: bold; color: white;");
    title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    mainLay->addWidget(title);

    // 亮度卡片
    brightnessCard = new QWidget(this);
    brightnessCard->setStyleSheet(
        "QWidget {"
        "  background: rgba(255,255,255,0.05);"
        "  border-radius: 20px;"
        "  border: 2px solid transparent;"
        "}"
        "QWidget[zenFocus=\"true\"] {"
        "  border: 2px solid #10B981;"
        "}"
    );
    QVBoxLayout *cardLay = new QVBoxLayout(brightnessCard);
    cardLay->setContentsMargins(30, 20, 30, 20);
    cardLay->setSpacing(15);

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
        QSlider[zenEdit="true"]::sub-page:horizontal { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #10B981, stop:1 #34D399); }
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

    // 色温卡片
    colorTempCard = new QWidget(this);
    colorTempCard->setStyleSheet(
        "QWidget {"
        "  background: rgba(255,255,255,0.05);"
        "  border-radius: 20px;"
        "  border: 2px solid transparent;"
        "}"
        "QWidget[zenFocus=\"true\"] {"
        "  border: 2px solid #10B981;"
        "}"
    );
    QVBoxLayout *ctCardLay = new QVBoxLayout(colorTempCard);
    ctCardLay->setContentsMargins(30, 20, 30, 20);
    ctCardLay->setSpacing(15);

    QLabel *ctCardTitle = new QLabel("台灯色温调节", colorTempCard);
    ctCardTitle->setStyleSheet("font-size: 18px; color: rgba(255,255,255,0.6); background: transparent;");

    QHBoxLayout *ctSliderLay = new QHBoxLayout();
    colorTempSlider = new QSlider(Qt::Horizontal, colorTempCard);
    colorTempSlider->setRange(0, 100);
    colorTempSlider->setValue(50);
    // 色温进度条背景从冷色(蓝)渐变到暖色(橙黄)
    colorTempSlider->setStyleSheet(R"(
        QSlider::groove:horizontal { height: 8px; background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #60A5FA, stop:1 #FBBF24); border-radius: 4px; }
        QSlider::sub-page:horizontal { background: transparent; }
        QSlider::handle:horizontal { width: 24px; height: 24px; margin: -8px 0; background: white; border-radius: 12px; border: 3px solid #4F46E5; }
    )");

    colorTempValLabel = new QLabel("50%", colorTempCard);
    colorTempValLabel->setStyleSheet("font-size: 24px; font-weight: bold; color: white; background: transparent;");
    colorTempValLabel->setFixedWidth(80);
    colorTempValLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    ctSliderLay->addWidget(colorTempSlider);
    ctSliderLay->addWidget(colorTempValLabel);

    ctCardLay->addWidget(ctCardTitle);
    ctCardLay->addLayout(ctSliderLay);

    mainLay->addWidget(colorTempCard);

    // 校准按钮
    calibBtn = new QPushButton("自动校准", this);
    calibBtn->setStyleSheet(
        "QPushButton {"
        "  background: rgba(255,255,255,0.05);"
        "  color: white;"
        "  font-size: 18px;"
        "  border-radius: 20px;"
        "  border: 2px solid transparent;"
        "  padding: 15px;"
        "}"
        "QPushButton[zenFocus=\"true\"] {"
        "  border: 2px solid #10B981;"
        "}"
    );
    mainLay->addWidget(calibBtn);

    mainLay->addStretch();

    // 信号绑定
    connect(brightnessSlider, &QSlider::valueChanged, this, [this](int v) {
        brightnessValLabel->setText(QString("%1%").arg(v));
        emit brightnessChanged(v);
    });

    connect(colorTempSlider, &QSlider::valueChanged, this, [this](int v) {
        colorTempValLabel->setText(QString("%1%").arg(v));
        float ratio = v / 100.0f;
        emit colorTempChanged(ratio);
    });

    connect(calibBtn, &QPushButton::clicked, this, [this]() {
        emit calibrationRequested();
    });

    updateFocusStyle();
}

void ControlPage::setLampState(int brightness, float colorRatio)
{
    brightnessSlider->blockSignals(true);
    brightnessSlider->setValue(brightness);
    brightnessValLabel->setText(QString("%1%").arg(brightness));
    brightnessSlider->blockSignals(false);

    int ctVal = colorRatio * 100;
    colorTempSlider->blockSignals(true);
    colorTempSlider->setValue(ctVal);
    colorTempValLabel->setText(QString("%1%").arg(ctVal));
    colorTempSlider->blockSignals(false);
}

void ControlPage::resetFocusState()
{
    focusIndex = -1; // -1 means no focus
    inEditMode = false;
    updateFocusStyle();
}

void ControlPage::enterFocusMode()
{
    focusIndex = 0;
    inEditMode = false;
    updateFocusStyle();
}

void ControlPage::updateFocusStyle()
{
    brightnessCard->setProperty("zenFocus", QVariant((bool)(!inEditMode && focusIndex == 0)));
    brightnessCard->style()->unpolish(brightnessCard);
    brightnessCard->style()->polish(brightnessCard);

    brightnessSlider->setProperty("zenEdit", QVariant((bool)(inEditMode && focusIndex == 0)));
    brightnessSlider->style()->unpolish(brightnessSlider);
    brightnessSlider->style()->polish(brightnessSlider);
    
    colorTempCard->setProperty("zenFocus", QVariant((bool)(!inEditMode && focusIndex == 1)));
    colorTempCard->style()->unpolish(colorTempCard);
    colorTempCard->style()->polish(colorTempCard);

    colorTempSlider->setProperty("zenEdit", QVariant((bool)(inEditMode && focusIndex == 1)));
    colorTempSlider->style()->unpolish(colorTempSlider);
    colorTempSlider->style()->polish(colorTempSlider);

    calibBtn->setProperty("zenFocus", QVariant((bool)(!inEditMode && focusIndex == 2)));
    calibBtn->style()->unpolish(calibBtn);
    calibBtn->style()->polish(calibBtn);
}

void ControlPage::handleKnobLeft()
{
    if (inEditMode) {
        QSlider *activeSlider = (focusIndex == 0) ? brightnessSlider : colorTempSlider;
        int val = activeSlider->value() - 5;
        if (val < 0) val = 0;
        activeSlider->setValue(val);
    } else {
        focusIndex--;
        if (focusIndex < 0) focusIndex = 2;
        updateFocusStyle();
    }
}

void ControlPage::handleKnobRight()
{
    if (inEditMode) {
        QSlider *activeSlider = (focusIndex == 0) ? brightnessSlider : colorTempSlider;
        int val = activeSlider->value() + 5;
        if (val > 100) val = 100;
        activeSlider->setValue(val);
    } else {
        focusIndex++;
        if (focusIndex > 2) focusIndex = 0;
        updateFocusStyle();
    }
}

void ControlPage::handleKnobPress()
{
    if (focusIndex == 2) {
        calibBtn->click();
        return;
    }
    inEditMode = !inEditMode;
    updateFocusStyle();
}

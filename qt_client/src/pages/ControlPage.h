#ifndef CONTROLPAGE_H
#define CONTROLPAGE_H

#include <QWidget>
#include <QLabel>
#include <QSlider>
#include <QPushButton>

class ControlPage : public QWidget
{
    Q_OBJECT
public:
    explicit ControlPage(QWidget *parent = nullptr);

    QSlider* getBrightnessSlider() const { return brightnessSlider; }
    QSlider* getColorTempSlider() const { return colorTempSlider; }

    void setLampState(int brightness, float colorRatio);

    // 旋钮交互接口
    void handleKnobLeft();
    void handleKnobRight();
    void handleKnobPress();
    
    // 强制复位状态，当切出该页面时调用
    void resetFocusState();
    void enterFocusMode();

signals:
    void brightnessChanged(int percent);
    void colorTempChanged(float ratio);
    void calibrationRequested();

private:
    void updateFocusStyle();

    QWidget *brightnessCard;
    QSlider *brightnessSlider;
    QLabel *brightnessValLabel;
    
    QWidget *colorTempCard;
    QSlider *colorTempSlider;
    QLabel *colorTempValLabel;

    QPushButton *calibBtn;

    int focusIndex; // 0 for brightness, 1 for color temp, 2 for calibBtn
    bool inEditMode;
};

#endif // CONTROLPAGE_H

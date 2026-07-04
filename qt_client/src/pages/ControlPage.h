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
    QPushButton* getToggleColorBtn() const { return toggleColorBtn; }

    void setBrightness(int percent);

signals:
    void brightnessChanged(int percent);
    void toggleColorTempRequested();

private:
    QSlider *brightnessSlider;
    QLabel *brightnessValLabel;
    QPushButton *toggleColorBtn;
};

#endif // CONTROLPAGE_H

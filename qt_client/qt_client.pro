QT += core gui widgets network
CONFIG += c++17
TEMPLATE = app
TARGET = qt_client

SOURCES += \
    src/main.cpp \
    src/MainWindow.cpp \
    src/pages/HomePage.cpp \
    src/pages/StatsPage.cpp \
    src/pages/StudyPage.cpp \
    src/services/MockFusionClient.cpp

HEADERS += \
    src/MainWindow.h \
    src/pages/HomePage.h \
    src/pages/StatsPage.h \
    src/pages/StudyPage.h \
    src/services/MockFusionClient.h

INCLUDEPATH += \
    src \
    src/pages \
    src/controllers \
    src/services \
    ../common/include

RESOURCES += \
    resources/resources.qrc \
    resources/fonts.qrc
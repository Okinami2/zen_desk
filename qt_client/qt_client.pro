QT += core gui widgets network sql
CONFIG += c++17
TEMPLATE = app
TARGET = qt_client

SOURCES += \
    src/main.cpp \
    src/MainWindow.cpp \
    src/pages/HomePage.cpp \
    src/pages/StatusPage.cpp \
    src/pages/StatsPage.cpp \
    src/pages/StudyPage.cpp \
    src/pages/ControlPage.cpp \
    src/services/MockFusionClient.cpp \
    src/services/DatabaseManager.cpp

HEADERS += \
    src/MainWindow.h \
    src/pages/HomePage.h \
    src/pages/StatusPage.h \
    src/pages/StatsPage.h \
    src/pages/StudyPage.h \
    src/pages/ControlPage.h \
    src/services/MockFusionClient.h \
    src/services/DatabaseManager.h

INCLUDEPATH += \
    src \
    src/pages \
    src/controllers \
    src/services \
    ../common/include

RESOURCES += \
    resources/resources.qrc \
    resources/fonts.qrc

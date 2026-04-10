#include "MainWindow.h"
#include <QApplication>
#include <QFontDatabase>
#include <QSocketNotifier>
#include <QTextStream>
#include <unistd.h>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    int fontId = QFontDatabase::addApplicationFont(":/fonts/SourceHanSansCN-Medium.otf");
    if (fontId != -1) {
        QString family = QFontDatabase::applicationFontFamilies(fontId).value(0);
        QFont appFont(family);
        appFont.setStyleStrategy(QFont::PreferAntialias);
        a.setFont(appFont);
    }

    MainWindow w;
    w.show();
    // w.showFullScreen();  // 嵌入式建议打开

    // 监听终端标准输入：输入 q / Q 后正常退出
    QSocketNotifier *stdinNotifier = new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, &a);
    QObject::connect(stdinNotifier, &QSocketNotifier::activated, &a, [&](int) {
        QTextStream ts(stdin);
        QString line = ts.readLine().trimmed();
        if (line.compare("q", Qt::CaseInsensitive) == 0) {
            a.quit();
        }
    });

    return a.exec();
}
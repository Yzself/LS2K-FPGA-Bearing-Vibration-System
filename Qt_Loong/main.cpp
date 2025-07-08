#include "widget.h"
#include <QApplication>
#include "inhibit_manager.h"

int main(int argc, char *argv[])
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication a(argc, argv);

    InhibitManager inhibitor;
    inhibitor.inhibit(); // 在程序启动时请求抑制

    Widget w;
    w.showFullScreen();
    return a.exec();
}

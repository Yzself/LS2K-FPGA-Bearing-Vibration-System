#include "widget.h"
#include "mainwindows.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    // 1. 创建并显示登录窗口
    Widget loginWidget;
    loginWidget.show();

    // 2. 创建主窗口实例，但先不显示它
    mainWindows mainApp;

    // 3. 核心连接：将登录窗口的成功信号连接到一个 lambda 函数
    QObject::connect(&loginWidget, &Widget::loginSuccess, [&]() {
        qDebug() << "Login successful. Switching windows...";
        // a. 显示主窗口
        mainApp.showMaximized();
        // b. 关闭登录窗口
        loginWidget.close();
    });

    QObject::connect(&mainApp, &mainWindows::openConnectSetter, [&]() {
        qDebug() << "Open Connect Setter successful. Switching windows...";
        // a. 显示登录窗口
        loginWidget.show();
    });
    // 4.  将 loginWidget 的 dataReceived 信号 连接到 mainApp 的 processReceivedData 槽函数
    QObject::connect(&loginWidget, &Widget::dataReceived,
                     &mainApp, &mainWindows::processReceivedData);

    // 5. 当最后一个窗口关闭时，退出整个应用程序
    QObject::connect(&a, &QApplication::lastWindowClosed, &a, &QApplication::quit);
    return a.exec();
}

#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QTcpSocket>

QT_BEGIN_NAMESPACE
namespace Ui {
class Widget;
}
QT_END_NAMESPACE

class mainWindows;
class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(QWidget *parent = nullptr);
    ~Widget();
    QTcpSocket *tcpSocket;
private slots:
    void on_ConnectButton_clicked();
    void readyRead_SLOT();

    // --- 用于处理TCP连接状态的槽函数 ---
    void onSocketConnected();
    void onSocketError(QAbstractSocket::SocketError socketError);
    void onSocketDisconnected(); // (可选) 处理断开连接的提示
    void on_CloseButton_clicked();

private:
    Ui::Widget *ui;

signals:
    void loginSuccess();
    void dataReceived(const QByteArray &data);
};
#endif // WIDGET_H

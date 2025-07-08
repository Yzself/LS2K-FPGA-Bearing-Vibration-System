#include "widget.h"
#include "ui_widget.h"
#include <QMessageBox>
#include <QTcpSocket>
#include <QNetworkProxy>
Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
{
    ui->setupUi(this);

    // 创建一个“无代理”的 QNetworkProxy 对象
    QNetworkProxy noProxy;
    noProxy.setType(QNetworkProxy::NoProxy);
    tcpSocket = new QTcpSocket(this);
    tcpSocket -> setProxy(noProxy);
    ui->CloseButton->setEnabled(false);
    // --- 将信号槽连接移到构造函数中 ---
    // 1. 连接成功信号
    connect(tcpSocket, &QTcpSocket::connected, this, &Widget::onSocketConnected);

    // 2. 连接错误信号
    connect(tcpSocket, &QTcpSocket::errorOccurred, this, &Widget::onSocketError);

    // 3. 连接接收数据信号
    connect(tcpSocket, &QTcpSocket::readyRead, this, &Widget::readyRead_SLOT);

    // 4. 连接断开信号
    connect(tcpSocket, &QTcpSocket::disconnected, this, &Widget::onSocketDisconnected);
}

Widget::~Widget()
{
    delete ui;
}

void Widget::on_ConnectButton_clicked()
{
    // --- 提供用户反馈，禁用按钮防止重复点击 ---
    ui->ConnectButton->setEnabled(false);
    ui->ConnectButton->setText("连接中...");

    // 获取IP和端口
    QString ip = ui->IPEdit->text();
    quint16 port = ui->PortEdit->text().toUInt();

    // 先尝试断开旧的连接（如果存在）
    if (tcpSocket->state() == QAbstractSocket::ConnectedState) {
        tcpSocket->disconnectFromHost();
    }

    // 发起新的异步连接
    tcpSocket->connectToHost(ip, port);
}

void Widget::readyRead_SLOT()
{
    // 1. 从套接字中读取所有可用的数据
    QByteArray data = tcpSocket->readAll();

    // 2. 如果读到了数据，就通过信号发射出去
    if (!data.isEmpty())
    {
        qDebug() << "Widget received data, emitting signal. Size:" << data.size();
        emit dataReceived(data);
    }
}

void Widget::onSocketConnected()
{
    // --- 连接成功时的处理 ---
    // 1. 更新UI状态
    ui->ConnectButton->setEnabled(false);
    ui->CloseButton->setEnabled(true);
    ui->ConnectButton->setText("已连接");
    // 2. 显示成功提示弹窗
    QMessageBox::information(this, "连接成功", "已成功连接到龙芯服务器！");

    qDebug() << "Successfully connected to host.";

    emit loginSuccess();

    // 连接成功后的处理
}

void Widget::onSocketError(QAbstractSocket::SocketError socketError)
{
    Q_UNUSED(socketError);
    // --- 连接失败时的处理 ---
    // 1. 更新UI状态
    ui->ConnectButton->setEnabled(true); // 恢复按钮可用
    ui->CloseButton->setEnabled(false);
    ui->ConnectButton->setText("连接");

    // 2. 显示详细的错误信息弹窗
    QMessageBox::critical(this, "连接失败", QString("无法连接到龙芯服务器: %1").arg(tcpSocket->errorString()));

    qDebug() << "Connection error:" << tcpSocket->errorString();
}

void Widget::onSocketDisconnected()
{
    // --- 当连接意外断开或手动断开时的处理 ---
    ui->ConnectButton->setEnabled(true);
    ui->CloseButton->setEnabled(false);
    ui->ConnectButton->setText("连接");

    // 可以选择性地给用户一个提示
    QMessageBox::warning(this, "连接已断开", "与服务器的连接已断开。");

    qDebug() << "Socket disconnected.";
}



void Widget::on_CloseButton_clicked()
{
    // 1. 检查套接字当前是否处于连接状态
    if (tcpSocket->state() == QAbstractSocket::ConnectedState)
    {
        // 2. 如果已连接，则发起异步断开连接的请求
        tcpSocket->disconnectFromHost();
        qDebug() << "Disconnect request sent.";
    }
}


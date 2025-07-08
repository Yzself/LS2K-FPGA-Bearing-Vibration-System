#ifndef DATASENDER_H
#define DATASENDER_H

#include <QObject>
#include <QVector>
#include <QTcpSocket>
#include <QtGlobal>

namespace Protocol {
// 包头魔术数字，选择一个不容易在随机数据中出现的值
const quint32 HEADER_MAGIC = 0xAA55AA55;

// 数据类型枚举
enum DataType : quint16 {
    ThreeAxisData = 0x0001, // 三轴加速度数据
    ModelOut = 0x0002,
    State = 0x0003
    // ... 其他数据类型
};
}

class DataSender : public QObject
{
    Q_OBJECT

public:
    explicit DataSender(QObject *parent = nullptr);
    ~DataSender();

public slots:
    // 从主线程接收数据并开始发送
    void sendData(const QVector<double>& xData, const QVector<double>& yData, const QVector<double>& zData);
    void sendModelOutput(const QString& className, double confidence);
    void sendState(const QString& state);
    // 从主线程接收一个新的、已连接的socket
    void setSocket(QTcpSocket* socket);

    // 当客户端断开时，清空socket
    void clientDisconnected();

signals:
    void dataSentStatus(const QString& statusMessage);
    void clientStatusChanged(const QString& statusMessage);

private:
    QTcpSocket* m_clientSocket; // 指向由主线程创建和管理的socket
};

#endif // DATASENDER_H

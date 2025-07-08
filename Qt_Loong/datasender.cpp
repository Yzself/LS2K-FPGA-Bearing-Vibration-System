#include "datasender.h"
#include <QDataStream>
#include <QThread>
#include <QHostAddress>
DataSender::DataSender(QObject *parent)
    : QObject(parent), m_clientSocket(nullptr)
{
}

DataSender::~DataSender()
{
}

void DataSender::setSocket(QTcpSocket* socket)
{
    m_clientSocket = socket;
    if(m_clientSocket) {
        emit clientStatusChanged(QString("新客户端已连接: %1:%2").arg(m_clientSocket->peerAddress().toString()).arg(m_clientSocket->peerPort()));
    }
}

void DataSender::clientDisconnected()
{
    if(m_clientSocket) {
        emit clientStatusChanged(QString("客户端 %1:%2 已断开连接。").arg(m_clientSocket->peerAddress().toString()).arg(m_clientSocket->peerPort()));
    }
    m_clientSocket = nullptr; // 清空指针
}

/**
 * @brief (封包版) 将三轴加速度数据进行封包后发送。
 *        数据包结构: [包头(4B)] [类型(2B)] [长度(4B)] [数据体(...B)]
 * @param xData X轴数据
 * @param yData Y轴数据
 * @param zData Z轴数据
 */
void DataSender::sendData(const QVector<double>& xData, const QVector<double>& yData, const QVector<double>& zData)
{
    if (!m_clientSocket || m_clientSocket->state() != QAbstractSocket::ConnectedState) {
        // emit dataSentStatus("无客户端连接，数据未发送。");
        return;
    }

    // --- 1. 准备数据体 (Payload) ---
    QByteArray payloadBlock;
    QDataStream payloadStream(&payloadBlock, QIODevice::WriteOnly);
    payloadStream.setVersion(QDataStream::Qt_5_12);
    // *** 在这里添加字节序设置:大端 ***
    payloadStream.setByteOrder(QDataStream::BigEndian);
    // 写入数据点的数量
    quint32 pointCount = static_cast<quint32>(xData.size());
    if (pointCount == 0 || xData.size() != yData.size() || xData.size() != zData.size()) {
        emit dataSentStatus("错误: 数据为空或三轴数据长度不一致。");
        return;
    }
    payloadStream << pointCount;
    // 写入三轴数据
    for (quint32 i = 0; i < pointCount; ++i) {
        payloadStream << xData[i] << yData[i] << zData[i];
    }

    // --- 2. 组装完整的数据包 ---
    QByteArray finalPacket;
    QDataStream packetStream(&finalPacket, QIODevice::WriteOnly);
    packetStream.setVersion(QDataStream::Qt_5_12);
    // *** 在这里也添加字节序设置:大端 ***
    packetStream.setByteOrder(QDataStream::BigEndian);
    // 写入包头 (4字节)
    packetStream << Protocol::HEADER_MAGIC;
    // 写入数据类型 (2字节)
    packetStream << static_cast<quint16>(Protocol::ThreeAxisData);
    // 写入数据体(payload)的长度 (4字节)
    packetStream << static_cast<quint32>(payloadBlock.size());

    // --- 3. 将数据体(payload)追加到包后面 ---
    finalPacket.append(payloadBlock);

    // --- 4. 发送数据包 ---
    qint64 bytesWritten = m_clientSocket->write(finalPacket);
    // m_clientSocket->flush(); // write()通常已经足够，flush()可以确保立即发送，但在某些情况下可能阻塞

    if (bytesWritten == finalPacket.size()) {
        QString status = QString("成功封包并发送三轴数据 (%1 个点, 共 %2 字节)")
                             .arg(pointCount)
                             .arg(finalPacket.size());
        // emit dataSentStatus(status);
        qDebug() << "DataSender (thread" << QThread::currentThreadId() << "):" << status;
    } else if (bytesWritten == -1) {
        emit dataSentStatus(QString("错误: 数据发送失败 - %1").arg(m_clientSocket->errorString()));
        qDebug() << "DataSender send error:" << m_clientSocket->errorString();
    }
    else {
        emit dataSentStatus(QString("错误: 数据发送不完整 (%1 / %2 字节)").arg(bytesWritten).arg(finalPacket.size()));
        qDebug() << "DataSender send incomplete.";
    }
}

/**
 * @brief (封包版) 将模型的输出结果（类别名和置信度）进行封包后发送。
 *        数据包结构: [包头(4B)] [类型(2B)] [长度(4B)] [数据体(...B)]
 * @param className  模型预测的类别名称
 * @param confidence 对应的置信度 (0.0 - 100.0)
 */
void DataSender::sendModelOutput(const QString& className, double confidence)
{
    if (!m_clientSocket || m_clientSocket->state() != QAbstractSocket::ConnectedState) {
        // emit dataSentStatus("无客户端连接，模型结果未发送。");
        return;
    }

    // --- 1. 准备数据体 (Payload) ---
    // 数据体将包含一个QString和一个double
    QByteArray payloadBlock;
    QDataStream payloadStream(&payloadBlock, QIODevice::WriteOnly);
    payloadStream.setVersion(QDataStream::Qt_5_12);
    // [重要] 保持与接收端一致的字节序
    payloadStream.setByteOrder(QDataStream::BigEndian);

    // 将类别名(QString)和置信度(double)写入数据体
    payloadStream << className << confidence;


    // --- 2. 组装完整的数据包 ---
    QByteArray finalPacket;
    QDataStream packetStream(&finalPacket, QIODevice::WriteOnly);
    packetStream.setVersion(QDataStream::Qt_5_12);
    // [重要] 整个包的头部也使用相同的字节序
    packetStream.setByteOrder(QDataStream::BigEndian);

    // a. 写入包头魔术字 (4字节)
    packetStream << Protocol::HEADER_MAGIC;
    // b. 写入新的数据类型 (2字节)
    packetStream << static_cast<quint16>(Protocol::ModelOut);
    // c. 写入数据体(payload)的长度 (4字节)
    packetStream << static_cast<quint32>(payloadBlock.size());


    // --- 3. 将数据体(payload)追加到包头后面 ---
    finalPacket.append(payloadBlock);


    // --- 4. 发送完整的数据包 ---
    qint64 bytesWritten = m_clientSocket->write(finalPacket);

    // --- 5. 状态反馈 (与之前的函数类似) ---
    if (bytesWritten == finalPacket.size()) {
        QString status = QString("成功封包并发送模型结果 ('%1', %2%, 共 %3 字节)")
                             .arg(className)
                             .arg(confidence, 0, 'f', 2) // 格式化浮点数
                             .arg(finalPacket.size());
        // emit dataSentStatus(status);
        qDebug() << "DataSender (thread" << QThread::currentThreadId() << "):" << status;
    } else if (bytesWritten == -1) {
        emit dataSentStatus(QString("错误: 模型结果发送失败 - %1").arg(m_clientSocket->errorString()));
        qDebug() << "DataSender send error:" << m_clientSocket->errorString();
    } else {
        emit dataSentStatus(QString("错误: 模型结果发送不完整 (%1 / %2 字节)").arg(bytesWritten).arg(finalPacket.size()));
        qDebug() << "DataSender send incomplete.";
    }
}

/**
 * @brief (封包版) 将状态信息（一个QString）进行封包后发送。
 *        数据包结构: [包头(4B)] [类型(2B)] [长度(4B)] [数据体(...B)]
 * @param state 要发送的状态字符串
 */
void DataSender::sendState(const QString& state)
{
    if (!m_clientSocket || m_clientSocket->state() != QAbstractSocket::ConnectedState) {
        // emit dataSentStatus("无客户端连接，状态信息未发送。");
        return;
    }

    // --- 1. 准备数据体 (Payload) ---
    // 数据体只包含一个QString
    QByteArray payloadBlock;
    QDataStream payloadStream(&payloadBlock, QIODevice::WriteOnly);
    payloadStream.setVersion(QDataStream::Qt_5_12);
    payloadStream.setByteOrder(QDataStream::BigEndian);

    // 将状态字符串(QString)写入数据体
    payloadStream << state;


    // --- 2. 组装完整的数据包 ---
    QByteArray finalPacket;
    QDataStream packetStream(&finalPacket, QIODevice::WriteOnly);
    packetStream.setVersion(QDataStream::Qt_5_12);
    packetStream.setByteOrder(QDataStream::BigEndian);

    // a. 写入包头魔术字 (4字节)
    packetStream << Protocol::HEADER_MAGIC;
    // b. 写入新的数据类型 (2字节)
    packetStream << static_cast<quint16>(Protocol::State);
    // c. 写入数据体(payload)的长度 (4字节)
    packetStream << static_cast<quint32>(payloadBlock.size());


    // --- 3. 将数据体(payload)追加到包头后面 ---
    finalPacket.append(payloadBlock);


    // --- 4. 发送完整的数据包 ---
    qint64 bytesWritten = m_clientSocket->write(finalPacket);


    // --- 5. 状态反馈 ---
    if (bytesWritten == finalPacket.size()) {
        QString status = QString("成功封包并发送状态信息 ('%1', 共 %2 字节)")
                             .arg(state)
                             .arg(finalPacket.size());
        // emit dataSentStatus(status);
        qDebug() << "DataSender (thread" << QThread::currentThreadId() << "):" << status;
    } else if (bytesWritten == -1) {
        emit dataSentStatus(QString("错误: 状态信息发送失败 - %1").arg(m_clientSocket->errorString()));
        qDebug() << "DataSender send error:" << m_clientSocket->errorString();
    } else {
        emit dataSentStatus(QString("错误: 状态信息发送不完整 (%1 / %2 字节)").arg(bytesWritten).arg(finalPacket.size()));
        qDebug() << "DataSender send incomplete.";
    }
}

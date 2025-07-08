#ifndef DATAREADER_H
#define DATAREADER_H

#include <QObject>
#include <QVector>
#include <QString>

#define DEVICE_NAME "/dev/FPGA_SPI_dev" // 仅在目标Linux系统上有效
#define ADC_BYTES_PER_AXIS 2048         // 每个轴的原始字节数
#define SAMPLES_PER_AXIS (ADC_BYTES_PER_AXIS / 2) // 每个轴的采样点数 (1024)
#define NUM_AXES 3                      //三轴数据
#define DELIMITER_BYTES 2               // 每个分隔符的字节数
// 计算总缓冲区大小
#define BUFFER_SIZE_CALC (ADC_BYTES_PER_AXIS * NUM_AXES + DELIMITER_BYTES * (NUM_AXES - 1))

class DataReader : public QObject
{
    Q_OBJECT
public:
    explicit DataReader(QObject *parent = nullptr);
    ~DataReader();
    int fd = -1; // 文件描述符
    bool openDevice();
    void closeDevice();
    // 这个函数将执行实际的读取和解析
    // 返回值: true 表示成功, false 表示失败
    // 输出参数: timeKeys, xValues, yValues, zValues 将被填充
    bool readDeviceData(QVector<double>& timeKeys,
                        QVector<double>& xValues,
                        QVector<double>& yValues,
                        QVector<double>& zValues,
                        int batchNumber); // batchNumber 用于生成时间戳

private:

};

#endif // DATAREADER_H

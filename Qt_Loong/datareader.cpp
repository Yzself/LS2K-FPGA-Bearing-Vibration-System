#include "datareader.h"
#include <fcntl.h>   // For open
#include <unistd.h>  // For read, close
#include <QDebug>
#include <vector>    // std::vector for char buffer
#include <cstdio>    // perror, fprintf (can be replaced with qDebug)

DataReader::DataReader(QObject *parent) : QObject(parent)
{
}

DataReader::~DataReader()
{
    closeDevice();
}

bool DataReader::openDevice()
{
    if (fd != -1) {
        qDebug() << "Device already open.";
        return true; // 或者根据逻辑决定是否重新打开
    }
    fd = open(DEVICE_NAME, O_RDWR);
    if (fd == -1) {
        qDebug("Failed to open device %s", DEVICE_NAME);
        return false;
    }
    qDebug() << "Device" << DEVICE_NAME << "opened successfully.";
    return true;
}

void DataReader::closeDevice()
{
    if (fd != -1) {
        if (close(fd) == -1) {
            qDebug("Failed to close device");
        } else {
            qDebug() << "Device closed.";
        }
        fd = -1;//代表设备驱动已关闭
    }
}

bool DataReader::readDeviceData(QVector<double>& timeKeys,
                                QVector<double>& xValues,
                                QVector<double>& yValues,
                                QVector<double>& zValues,
                                int batchNumber)
{

    // --- 实际的 Linux 设备读取逻辑 ---
    if (fd == -1) { // 尝试打开设备，如果尚未打开
        qDebug("FPGA Driver open:Start");
        if (!openDevice()) {
            qDebug("FPGA Driver open:Failed");
            return false;
        }
        qDebug("FPGA Driver open:Successful");
    }
    std::vector<char> read_buffer_vec(BUFFER_SIZE_CALC); // 使用 std::vector
    ssize_t bytes_read;
    qDebug("Data Read:Start");
    bytes_read = read(fd, read_buffer_vec.data(), BUFFER_SIZE_CALC);

    if (bytes_read == -1) {
        qDebug("Failed to read from device");
        // closeDevice(); // 可选：是否在此处关闭
        return false;
    }

    if (bytes_read != BUFFER_SIZE_CALC) {
        qDebug("Read data length (%zd) does not match expected size (%d).", bytes_read, BUFFER_SIZE_CALC);
        return false;
    }
    qDebug("Data Read:Successful");
    timeKeys.resize(SAMPLES_PER_AXIS);
    xValues.resize(SAMPLES_PER_AXIS);
    yValues.resize(SAMPLES_PER_AXIS);
    zValues.resize(SAMPLES_PER_AXIS);

    double timeOffset = static_cast<double>(batchNumber * SAMPLES_PER_AXIS);

    // 解析采样数据
    // X轴数据: 0 to (ADC_BYTES_PER_AXIS - 1)
    // Y轴数据: ADC_BYTES_PER_AXIS + DELIMITER_BYTES to (ADC_BYTES_PER_AXIS*2 + DELIMITER_BYTES - 1)
    // Z轴数据: ADC_BYTES_PER_AXIS*2 + DELIMITER_BYTES*2 to (ADC_BYTES_PER_AXIS*3 + DELIMITER_BYTES*2 - 1)

    const char* buffer_ptr = read_buffer_vec.data();

    for (int i = 0; i < SAMPLES_PER_AXIS; ++i) {
        timeKeys[i] = timeOffset + i; // 或者使用实际的时间戳

        unsigned char x_byte1 = static_cast<unsigned char>(buffer_ptr[2 * i]);
        unsigned char x_byte2 = static_cast<unsigned char>(buffer_ptr[2 * i + 1]);
        xValues[i] = static_cast<double>(((x_byte1 << 2) | x_byte2))*10.0/1023.0 - 5.0;
        // 此处通过观察数据，发现x轴在固定一些时刻出现了不正常的噪声数据，因此手动消除这些异常点.
        if(i > 0)
        {
            if(abs(xValues[i] - xValues[i - 1])>=0.55) // 异常抖动
            {
                xValues[i] = xValues[i - 1];
            }
        }
        // Y-axis data
        size_t y_offset = ADC_BYTES_PER_AXIS + DELIMITER_BYTES;
        unsigned char y_byte1 = static_cast<unsigned char>(buffer_ptr[y_offset + 2 * i]);
        unsigned char y_byte2 = static_cast<unsigned char>(buffer_ptr[y_offset + 2 * i + 1]);
        yValues[i] = static_cast<double>(((y_byte1 << 2) | y_byte2))*10.0/1023.0 - 5.0;

        // Z-axis data
        size_t z_offset = ADC_BYTES_PER_AXIS * 2 + DELIMITER_BYTES * 2;
        unsigned char z_byte1 = static_cast<unsigned char>(buffer_ptr[z_offset + 2 * i]);
        unsigned char z_byte2 = static_cast<unsigned char>(buffer_ptr[z_offset + 2 * i + 1]);
        zValues[i] = static_cast<double>(((z_byte1 << 2) | z_byte2))*10.0/1023.0 - 5.0;

        // qDebug() << "Raw ADC:" << xValues[i] << yValues[i] << zValues[i];
        // **重要**: 您可能需要将这些原始ADC值转换为实际的加速度单位 (e.g., g)
        // 例如: xValues[i] = (xValues[i] - offset) * scale_factor;
    }
    return true;
}

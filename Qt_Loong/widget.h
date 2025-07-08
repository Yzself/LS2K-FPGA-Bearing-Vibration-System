#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QTimer>
#include "qcustomplot.h"
#include "datareader.h"
#include <QRandomGenerator> // 用于生成模拟数据
#include <cmath>          // 用于 std::sin, std::cos
#include <QProcess>
#include <QTime>
#include <QTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include "widget_2.h"
#include "datasender.h"
#include "beepctl.h"
#include <QThread>
QT_BEGIN_NAMESPACE
namespace Ui {
class Widget;
}
QT_END_NAMESPACE

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(QWidget *parent = nullptr);
    ~Widget();

private slots:
    void updatePlotWithNewBatch(); // 接收并处理新一批数据的槽函数
    void get_LocalTime();
    void on_CollectStartButton_clicked();
    void on_CollectStopButton_clicked();
    // 网络相关
    void newConnection_SLOT();
    void readyRead_SLOT();
    // 新增一个用于接收 DataSender 状态的槽
    void onDataSenderStatus(const QString& message);
    void onClientStatusChanged(const QString& message);
    // 新增一个用于处理客户端断开的槽
    void onClientSocketDisconnected();

    void on_MfccPlotButton_clicked();
    void showMainWindow();
    void on_CollectCleanButton_clicked();

    void on_HistoryBackButton_clicked();

    void on_HistoryCleanButton_clicked();

    void on_HistoryCleanAllButton_clicked();

    void on_MoniterButton_clicked();

    void on_beepOffButton_clicked();

private:
    Ui::Widget *ui;
    // 心跳定时器，阻止屏幕休眠
    QTimer *m_uiHeartbeatTimer;
    // 当前轴承状态
    QString className = NULL;
    QString className_state = NULL;
    bool changeFlag = false;
    double confidence = 0;
    double confidence_state = 0;
    char rankAlert = 0; // 警报等级
    char rankAlert_Buf[10]; // 蜂鸣器判别缓存器
    // 网络服务器相关
    QTcpServer *tcpServer;
    QTcpSocket *tcpSocket;
    quint16 Port;
    DataSender* m_dataSender;
    QThread* m_senderThread;

    // 添加MFCC显示窗口的指针
    widget_2 *m_mfccDisplayWindow;

    // 用于Collect模式的成员变量
    QString m_currentCollectCsvPath;  // 当前正在收集的CSV文件的完整路径
    QFile m_collectCsvFile;           // 当前打开用于收集的文件对象
    QTextStream m_collectCsvStream;   // 当前用于写入收集文件的文本流
    int m_collect_cnt = 0;           // 用于记录已收集的样本数量
    bool finish = false;

    // 辅助函数声明
    bool openAndPrepareCollectCsvFile(const QString& filePath, const QString& headerLine);
    void closeCollectCsvFile();

    bool Model_Deploy = false; //模型部署标志位
    QProcess *m_pythonModelProcess; // 用于管理 Python 模型进程
    QString m_csvDataPath; // 存储CSV文件的路径
    bool writeDataToCsv(const QString& filename,
                        const QVector<double>& timeData,
                        const QVector<double>& xData,
                        const QVector<double>& yData,
                        const QVector<double>& zData);
    void setLED(QLabel* label, int color, int size); //LED模拟
    void setupMultiAxisPlot(); // 波形显示设置函数
    void generateDataBatch(QVector<double>& timeKeys,
                           QVector<double>& xValues,
                           QVector<double>& yValues,
                           QVector<double>& zValues,
                           int batchNumber);

    // QCustomPlot 控件本身是从 UI 获取的 (ui->time)
    // 但我们需要单独的图表和轴矩形指针
    QCPGraph *m_graphX;
    QCPGraph *m_graphY;
    QCPGraph *m_graphZ;

    QCPAxisRect *m_axisRectX; // 用于X加速度的轴矩形
    QCPAxisRect *m_axisRectY; // 用于Y加速度的轴矩形
    QCPAxisRect *m_axisRectZ; // 用于Z加速度的轴矩形

    QTimer m_dataBatchTimer;  // 获取数据定时器

    QTime currentTime;
    QDate currentDate;
    QString dateTimeString;
    QTimer local_timer;       // 获取本地时间定时器

    QString Mode = "Monitor";        //工作模式 Monitor/Collect/History
    QString Mode_Buf = "Monitor";
    DataReader m_dataReader;  //采样数据获取器
    const int m_batchSize = 1024;//每次分析1024个点
    int m_currentBatchNumber = 0;

    BeepCtl* beepctl;; //蜂鸣器

    // --- 历史功能相关 ---
    void populateHistoryBox(); // 填充 HistoryBox 下拉列表
    QString getProcessedCsvDir(); // 辅助函数获取 processed_csv 目录路径
    QString getSensorDataDir();   // 辅助函数获取 sensor_data_for_python 目录路径
    void addHistoryItem(const QString& fullFileName);
    bool loadAndDisplayCsvData(const QString& csvFilePath); //解析csv文件并显示波形
    void cleanupOldHistoryFiles(); // 删除较早的波形
    QString convertFileNameToDisplayFormat(const QString &fileName); // 辅助函数，用于将文件名格式转换为HistoryBox中的显示格式

    // --- 获取屏幕分辨率 ---
    void checkScreenResolution();

    // --- 用于执行滑动平均滤波的辅助函数 ---
    QVector<double> applyMovingAverageFilter(const QVector<double>& rawData, int windowSize);
signals:
    // 新增一个用于触发数据发送的信号
    void newDataReadyToSend(const QVector<double>& xData, const QVector<double>& yData, const QVector<double>& zData);
    // 用于传输模型发送
    void newModelOutReadyToSend(const QString& className, double confidence);
    // 用于状态发送
    void newStateToSend(const QString& state);
    // 新增一个用于传递socket指针的信号
    void socketReady(QTcpSocket* socket);
    // 新增一个用于通知客户端断开的信号
    void clientHasDisconnected();

};
#endif // WIDGET_H

#ifndef MAINWINDOWS_H
#define MAINWINDOWS_H

#include <QWidget>
#include <qcustomplot.h>
#include <QProcess>

QT_BEGIN_NAMESPACE
namespace QtCharts {
class QChartView;
class QLineSeries;
class QValueAxis;
}
QT_END_NAMESPACE

namespace Ui {
class mainWindows;
}
// 与服务端要对应
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


class mainWindows : public QWidget
{
    Q_OBJECT

public:
    explicit mainWindows(QWidget *parent = nullptr);
    ~mainWindows();

    // 定义一个枚举来表示日志级别
    enum LogLevel {
        Info,
        Success,
        Warning,
        Error
    };
    // 声明日志函数，并注册枚举到元对象系统
    Q_ENUM(LogLevel)

    void updateMultiAxisPlot(QVector<double> &xData,QVector<double> &yData,QVector<double> &zData);// 更新波形

private:
    Ui::mainWindows *ui;

    // 波形显示相关定义(ui->WavePlot)
    QCPGraph *m_graphX;
    QCPGraph *m_graphY;
    QCPGraph *m_graphZ;
    QCPAxisRect *m_axisRectX; // 用于X加速度的轴矩形
    QCPAxisRect *m_axisRectY; // 用于Y加速度的轴矩形
    QCPAxisRect *m_axisRectZ; // 用于Z加速度的轴矩形
    void setupMultiAxisPlot(); // 波形显示设置函数
    const int m_batchSize = 1024;//每次分析1024个点
    QByteArray m_receiveBuffer;// 用于接收数据的缓冲区
    // --- 网络数据处理函数 ---
    void parseBuffer();

    // --- 专门解析三轴数据的函数 ---
    void parseThreeAxisData(const QByteArray& payload);
    // --- 专门解析模型输出数据的函数 ---
    void parseModelOutput(const QByteArray& payload);
    // --- 专门解析服务器模式的函数 ---
    void parseState(const QByteArray& payload);

    // --- 日志打印函数声明 ---

    void logMessage(LogLevel level, const QString& message);

    // --- 图表相关成员变量 ---
    // Loss 图表
    QtCharts::QChartView* m_lossChartView;
    QtCharts::QLineSeries* m_lossSeries;
    QtCharts::QValueAxis* m_lossXAxis;
    QtCharts::QValueAxis* m_lossYAxis;

    // Accuracy 图表
    QtCharts::QChartView* m_accuracyChartView;
    QtCharts::QLineSeries* m_accuracySeries;
    QtCharts::QValueAxis* m_accuracyXAxis;
    QtCharts::QValueAxis* m_accuracyYAxis;

    // --- 图表初始化函数 ---
    void setupCharts();

    // --- 模型训练相关 ---
    QString m_trainScriptPath; //保存选择的train.py路径
    QString m_pythonExecutablePath; // 用于存储选择的Python解释器路径
    QProcess *m_trainProcess;

    // 用于加载和保存py解释器设置的辅助函数
    void saveSettings();
    void loadSettings();

public slots:
    // --- 用于接收和处理网络数据 ---
    void processReceivedData(const QByteArray &data);

    // --- 用于更新图表的槽函数 ---
    // 添加一个新的数据点到Loss图表
    void addLossDataPoint(int epoch, double lossValue);
    // 添加一个新的数据点到Accuracy图表
    void addAccuracyDataPoint(int epoch, double accuracyValue);
    // (可选) 清空所有图表数据
    void clearAllChartData();

private slots:

    void on_selectTrainScriptButton_clicked();
    void on_beginTrainButton_clicked();
    void on_stopTrainButton_clicked();

    // --- 用于处理Python进程的槽函数 ---
    void onTrainProcessReadyRead(); // 读取Python的标准输出
    void onTrainProcessErrorOccurred(QProcess::ProcessError error);
    void onTrainProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void on_selectPythonPathButton_clicked();
    void on_ConnectSetButton_clicked();

signals:
    void openConnectSetter();
};

#endif // MAINWINDOWS_H

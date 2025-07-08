#include "mainwindows.h"
#include "ui_mainwindows.h"
#include "qcustomplot.h"
#include <QDataStream>
#include <QDebug>
#include <QDateTime>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QSettings>
#include <QFileInfo>        // 用于处理文件路径
#include <QProcessEnvironment> // 用于处理环境变量
#include <QMessageBox>      // 用于弹窗提示
#include <QTextCodec>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
QT_CHARTS_USE_NAMESPACE

mainWindows::mainWindows(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::mainWindows)
{
    ui->setupUi(this);
    setupMultiAxisPlot();
    setupCharts();

    // 创建QProcess实例
    m_trainProcess = new QProcess(this);

    // --- 连接QProcess的信号到我们的槽函数 ---
    connect(m_trainProcess, &QProcess::readyReadStandardOutput, this, &mainWindows::onTrainProcessReadyRead);
    connect(m_trainProcess, &QProcess::errorOccurred, this, &mainWindows::onTrainProcessErrorOccurred);
    connect(m_trainProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &mainWindows::onTrainProcessFinished);

    // 更新UI状态
    ui->beginTrainButton->setEnabled(true);
    ui->stopTrainButton->setEnabled(false);
    ui->DatasetprogressBar->setEnabled(false);

    loadSettings();
}

mainWindows::~mainWindows()
{
    saveSettings();
    delete ui;
}

/**
 * @brief 波形窗口初始化。
 */
void mainWindows::setupMultiAxisPlot()
{
    if (!ui->WavePlot) {
        qWarning("QCustomPlot widget 'time' not found in UI!");
        return;
    }
    QCustomPlot *customPlot = ui->WavePlot;

    QCPLegend *legend = customPlot->legend;
    if (legend && customPlot->plotLayout() && customPlot->plotLayout()->elements(false).contains(legend)) {
        customPlot->plotLayout()->take(legend);
    }
    customPlot->plotLayout()->clear();

    QCPMarginGroup *marginGroup = new QCPMarginGroup(customPlot);

    m_axisRectX = new QCPAxisRect(customPlot);
    if (!m_axisRectX) { qCritical("Failed to create m_axisRectX!"); return; }
    m_axisRectX->setMinimumSize(0, 100);
    m_axisRectX->setupFullAxesBox(true);
    m_axisRectX->axis(QCPAxis::atBottom)->setTickLabels(false);
    m_axisRectX->axis(QCPAxis::atBottom)->setTicks(false);
    m_axisRectX->axis(QCPAxis::atLeft)->setLabel("Acc X (g)");
    m_axisRectX->setMarginGroup(QCP::msLeft | QCP::msRight, marginGroup);

    m_axisRectY = new QCPAxisRect(customPlot);
    if (!m_axisRectY) { qCritical("Failed to create m_axisRectY!"); return; }
    m_axisRectY->setMinimumSize(0, 100);
    m_axisRectY->setupFullAxesBox(true);
    m_axisRectY->axis(QCPAxis::atBottom)->setTickLabels(false);
    m_axisRectY->axis(QCPAxis::atBottom)->setTicks(false);
    m_axisRectY->axis(QCPAxis::atLeft)->setLabel("Acc Y (g)");
    m_axisRectY->setMarginGroup(QCP::msLeft | QCP::msRight, marginGroup);

    m_axisRectZ = new QCPAxisRect(customPlot);
    if (!m_axisRectZ) { qCritical("Failed to create m_axisRectZ!"); return; }
    m_axisRectZ->setMinimumSize(0, 100);
    m_axisRectZ->setupFullAxesBox(true);
    m_axisRectZ->axis(QCPAxis::atLeft)->setLabel("Acc Z (g)");
    m_axisRectZ->axis(QCPAxis::atBottom)->setLabel("Time (s)");
    m_axisRectZ->setMarginGroup(QCP::msLeft | QCP::msRight, marginGroup);

    QCPLayoutGrid *gridLayout = qobject_cast<QCPLayoutGrid*>(customPlot->plotLayout());
    if (gridLayout) {
        gridLayout->setRowStretchFactor(0, 1);
        gridLayout->setRowStretchFactor(1, 1);
        gridLayout->setRowStretchFactor(2, 1);
        gridLayout->addElement(0, 0, m_axisRectX);
        gridLayout->addElement(1, 0, m_axisRectY);
        gridLayout->addElement(2, 0, m_axisRectZ);
    } else {
        customPlot->plotLayout()->addElement(m_axisRectX);
        customPlot->plotLayout()->addElement(m_axisRectY);
        customPlot->plotLayout()->addElement(m_axisRectZ);
    }

    m_graphX = customPlot->addGraph(m_axisRectX->axis(QCPAxis::atBottom), m_axisRectX->axis(QCPAxis::atLeft));
    if (!m_graphX) { qCritical("Failed to create m_graphX!"); return; }
    m_graphX->setName("X-Axis");
    m_graphX->setPen(QPen(Qt::blue));

    m_graphY = customPlot->addGraph(m_axisRectY->axis(QCPAxis::atBottom), m_axisRectY->axis(QCPAxis::atLeft));
    if (!m_graphY) { qCritical("Failed to create m_graphY!"); return; }
    m_graphY->setName("Y-Axis");
    m_graphY->setPen(QPen(Qt::red));

    m_graphZ = customPlot->addGraph(m_axisRectZ->axis(QCPAxis::atBottom), m_axisRectZ->axis(QCPAxis::atLeft));
    if (!m_graphZ) { qCritical("Failed to create m_graphZ!"); return; }
    m_graphZ->setName("Z-Axis");
    m_graphZ->setPen(QPen(Qt::green));

    connect(m_axisRectZ->axis(QCPAxis::atBottom), SIGNAL(rangeChanged(QCPRange)), m_axisRectX->axis(QCPAxis::atBottom), SLOT(setRange(QCPRange)));
    connect(m_axisRectZ->axis(QCPAxis::atBottom), SIGNAL(rangeChanged(QCPRange)), m_axisRectY->axis(QCPAxis::atBottom), SLOT(setRange(QCPRange)));
    //反向连接以允许拖动任何一个X轴
    connect(m_axisRectX->axis(QCPAxis::atBottom), SIGNAL(rangeChanged(QCPRange)), m_axisRectZ->axis(QCPAxis::atBottom), SLOT(setRange(QCPRange)));
    connect(m_axisRectY->axis(QCPAxis::atBottom), SIGNAL(rangeChanged(QCPRange)), m_axisRectZ->axis(QCPAxis::atBottom), SLOT(setRange(QCPRange)));


    m_axisRectX->axis(QCPAxis::atLeft)->setRange(-2.5, 2.5);
    m_axisRectY->axis(QCPAxis::atLeft)->setRange(-2.5, 2.5);
    m_axisRectZ->axis(QCPAxis::atLeft)->setRange(-2.5, 2.5);

    double timePerSample = 1.0 / 10000.0;
    double totalTime = (m_batchSize > 0 ? (m_batchSize - 1) : 0) * timePerSample;
    m_axisRectZ->axis(QCPAxis::atBottom)->setRange(0, totalTime);

    if (legend) legend->setVisible(false);
    customPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables);
}
/**
 * @brief 初始化LossPlot和AccuarcyPlot
 */
void mainWindows::setupCharts()
{
    // ==================== 1. 设置 Loss 图表 ====================
    m_lossSeries = new QLineSeries();
    m_lossSeries->setName("训练损失 (Loss)");
    m_lossSeries->setPen(QPen(QColor("#E91E63"), 2)); // 醒目的粉红色

    QChart* lossChart = new QChart();
    lossChart->addSeries(m_lossSeries);
    lossChart->setTitle("训练损失曲线");
    lossChart->legend()->hide();

    m_lossXAxis = new QValueAxis();
    m_lossXAxis->setTitleText("轮次 (Epoch)");
    m_lossXAxis->setLabelFormat("%i"); // 整数格式
    m_lossXAxis->setTickCount(11); // 显示10个间隔
    lossChart->addAxis(m_lossXAxis, Qt::AlignBottom);
    m_lossSeries->attachAxis(m_lossXAxis);

    m_lossYAxis = new QValueAxis();
    m_lossYAxis->setTitleText("Loss值");
    m_lossYAxis->setLabelFormat("%.4f"); // 4位小数
    lossChart->addAxis(m_lossYAxis, Qt::AlignLeft);
    m_lossSeries->attachAxis(m_lossYAxis);

    m_lossChartView = new QChartView(lossChart);
    m_lossChartView->setRenderHint(QPainter::Antialiasing);

    // 将图表放入UI占位符
    QVBoxLayout* lossLayout = new QVBoxLayout(ui->LossPlot);
    lossLayout->setContentsMargins(0, 0, 0, 0);
    lossLayout->addWidget(m_lossChartView);

    // ==================== 2. 设置 Accuracy 图表 ====================
    m_accuracySeries = new QLineSeries();
    m_accuracySeries->setName("训练准确率 (Accuracy)");
    m_accuracySeries->setPen(QPen(QColor("#007BFF"), 2)); // 经典的蓝色

    QChart* accuracyChart = new QChart();
    accuracyChart->addSeries(m_accuracySeries);
    accuracyChart->setTitle("训练准确率曲线");
    accuracyChart->legend()->hide();

    m_accuracyXAxis = new QValueAxis();
    m_accuracyXAxis->setTitleText("轮次 (Epoch)");
    m_accuracyXAxis->setLabelFormat("%i");
    m_accuracyXAxis->setTickCount(11);
    accuracyChart->addAxis(m_accuracyXAxis, Qt::AlignBottom);
    m_accuracySeries->attachAxis(m_accuracyXAxis);

    m_accuracyYAxis = new QValueAxis();
    m_accuracyYAxis->setTitleText("Accuracy (%)");
    m_accuracyYAxis->setRange(0, 100); // 准确率通常在0-100范围
    m_accuracyYAxis->setLabelFormat("%.2f"); // 2位小数
    accuracyChart->addAxis(m_accuracyYAxis, Qt::AlignLeft);
    m_accuracySeries->attachAxis(m_accuracyYAxis);

    m_accuracyChartView = new QChartView(accuracyChart);
    m_accuracyChartView->setRenderHint(QPainter::Antialiasing);

    // 将图表放入UI占位符
    QVBoxLayout* accuracyLayout = new QVBoxLayout(ui->AccuracyPlot);
    accuracyLayout->setContentsMargins(0, 0, 0, 0);
    accuracyLayout->addWidget(m_accuracyChartView);
}

/**
 * @brief 更新绘制波形
 * @param xData，yData, zData分别为三轴数据
 */
void mainWindows::updateMultiAxisPlot(QVector<double> &xData,QVector<double> &yData,QVector<double> &zData)
{
    QCustomPlot *customPlot = ui->WavePlot;
    // 时间轴生成
    QVector<double> actualTimeKeys;
    if (!xData.isEmpty()) {
        actualTimeKeys.resize(xData.size());
        double timePerSample = 1.0 / 10000.0;
        for (int i = 0; i < xData.size(); ++i) {
            actualTimeKeys[i] = i * timePerSample;
        }
    } else {
        qWarning("Received empty data batch. Skipping plot update.");
        return;
    }

    // 波形绘制
    m_graphX->data()->clear();
    m_graphY->data()->clear();
    m_graphZ->data()->clear();

    m_graphX->addData(actualTimeKeys, xData);
    m_graphY->addData(actualTimeKeys, yData);
    m_graphZ->addData(actualTimeKeys, zData);

    if (!actualTimeKeys.isEmpty()) { // 使用 actualTimeKeys 来设置范围
        m_axisRectZ->axis(QCPAxis::atBottom)->setRange(actualTimeKeys.first(), actualTimeKeys.last());
    } else {
        double timePerSample = 1.0 / 10000.0;
        m_axisRectZ->axis(QCPAxis::atBottom)->setRange(0, (m_batchSize > 0 ? (m_batchSize - 1) : 0) * timePerSample);
    }

    m_graphX->rescaleValueAxis(false, true);
    m_graphY->rescaleValueAxis(false, true);
    m_graphZ->rescaleValueAxis(false, true);

    customPlot->replot();
}

/**
 * @brief (数据接收槽) 这个槽函数由 Widget 的 dataReceived 信号触发。
 * @param data 从Widget传来的原始数据块。
 */
void mainWindows::processReceivedData(const QByteArray &data)
{
    // 1. 将新收到的数据块追加到内部缓冲区末尾
    m_receiveBuffer.append(data);
    qDebug() << "mainWindows received" << data.size() << "bytes, buffer size is now" << m_receiveBuffer.size();

    // 2. 调用内部的解析函数，尝试从缓冲区中提取数据包
    parseBuffer();
}

/**
 * @brief (核心解析) 从内部缓冲区 m_receiveBuffer 中循环解析一个或多个完整的数据包。
 */
void mainWindows::parseBuffer()
{
    // 使用一个循环，只要缓冲区中可能存在完整的数据包，就持续解析
    while (true) {
        // --- 1. 检查包头信息是否完整 (10字节) ---
        if (m_receiveBuffer.size() < 10) {
            // 数据不够一个包头，停止解析，等待下一次数据到来
            return;
        }

        QDataStream in(m_receiveBuffer);
        in.setVersion(QDataStream::Qt_5_12);
        // *** 在这里添加字节序设置:大端 ***
        in.setByteOrder(QDataStream::BigEndian);

        // --- 2. 解析包头，但不移动数据 ---
        quint32 magic;
        in >> magic;
        if (magic != Protocol::HEADER_MAGIC) {
            qWarning() << "Invalid packet header! Clearing buffer.";
            m_receiveBuffer.clear(); // 简单的错误处理
            return;
        }

        quint16 dataType;
        in >> dataType;

        quint32 payloadLength;
        in >> payloadLength;

        // [健壮性检查] 检查payloadLength是否在一个合理的范围内，防止恶意或错误的数据包
        if (payloadLength > 5000000) { // 例如，限制数据体最大为5MB
            qWarning() << "Packet payload length is too large:" << payloadLength;
            m_receiveBuffer.clear();
            return;
        }


        // --- 3. 检查数据体是否完整 ---
        if (m_receiveBuffer.size() < 10 + payloadLength) {
            // 数据体不完整，停止解析，等待下一次数据到来
            return;
        }

        // --- 4. 确认有一个完整包，进行处理 ---
        // 从缓冲区中移除已经解析的头部 (10字节)
        m_receiveBuffer.remove(0, 10);
        // 提取数据体部分
        QByteArray payload = m_receiveBuffer.left(payloadLength);
        // 从缓冲区中移除已经处理的数据体
        m_receiveBuffer.remove(0, payloadLength);

        qDebug() << "Successfully extracted a packet. Type:" << dataType << "Length:" << payloadLength;

        // --- 5. 根据数据类型分发 ---
        switch (static_cast<Protocol::DataType>(dataType)) {
        case Protocol::ThreeAxisData:
            parseThreeAxisData(payload);
            break;
        case Protocol::ModelOut:
            parseModelOutput(payload);
            break;
        case Protocol::State:
            parseState(payload);
            break;
        default:
            qWarning() << "Received unknown data type:" << dataType;
            break;
        }

        // 循环继续，检查缓冲区中是否还有下一个包
    }
}

/**
 * @brief (专门解析) 解析三轴加速度数据体。
 */
void mainWindows::parseThreeAxisData(const QByteArray& payload)
{
    QDataStream payloadStream(payload);
    payloadStream.setVersion(QDataStream::Qt_5_12);
    // *** 在这里也添加字节序设置:大端 ***
    payloadStream.setByteOrder(QDataStream::BigEndian);

    quint32 pointCount;
    payloadStream >> pointCount;

    qDebug() << "Parsing ThreeAxisData with" << pointCount << "points.";

    QVector<double> xData, yData, zData;
    xData.reserve(pointCount);
    yData.reserve(pointCount);
    zData.reserve(pointCount);

    for (quint32 i = 0; i < pointCount; ++i) {
        double x, y, z;
        payloadStream >> x >> y >> z;
        xData.append(x);
        yData.append(y);
        zData.append(z);
    }

    // --- 在这里，已经成功获取了三轴数据！---
    // 更新你的UI...
    qDebug() << "Successfully parsed ThreeAxisData. First point: ("
             << xData.first() << "," << yData.first() << "," << zData.first() << ")";
    updateMultiAxisPlot(xData,yData,zData);
}

/**
 * @brief (专门解析) 解析模型输出的数据体 (className 和 confidence)。
 * @param payload 包含一个QString和一个double的数据体。
 */
void mainWindows::parseModelOutput(const QByteArray& payload)
{
    QDataStream payloadStream(payload);
    payloadStream.setVersion(QDataStream::Qt_5_12);
    // [重要] 确保使用与发送端一致的字节序
    payloadStream.setByteOrder(QDataStream::BigEndian);

    QString className;
    double confidence;

    // 从数据流中按顺序提取数据
    payloadStream >> className >> confidence;

    // 检查数据流的状态，确保没有读取错误
    if (payloadStream.status() != QDataStream::Ok) {
        qWarning() << "Error while parsing ModelOut payload.";
        return;
    }

    // --- 在这里，已经成功获取了模型输出！---

    // 1. 打印到日志或控制台进行调试
    qDebug() << "Successfully parsed ModelOut. Class:" << className
             << ", Confidence:" << QString::number(confidence, 'f', 2) << "%";

    // 2. 更新UI
    if (ui->ModelOutLabel) {
        QString resultText = QString("%1-%2%")
                                 .arg(className)
                                 .arg(confidence, 0, 'f', 2);
        ui->ModelOutLabel->setText(resultText);

        // 根据className，改变标签的颜色
        if (className.contains("Healthy")||className.contains("healthy")) {
            ui->ModelOutLabel->setStyleSheet("color: green;");
        } else if (className.contains("0.7") || className.contains("0.9")) {
            ui->ModelOutLabel->setStyleSheet("color: darkgoldenrod;");
        } else if (className.contains("1.1") || className.contains("1.3")){
            ui->ModelOutLabel->setStyleSheet("color: orange;");
        } else if (className.contains("1.5") || className.contains("1.7")){
            ui->ModelOutLabel->setStyleSheet("color: red;");
        }
    }
}
/**
 * @brief (专门解析) 解析服务器模式。
 * @param payload 包含一个QString。
 */
void mainWindows::parseState(const QByteArray& payload)
{
    QDataStream payloadStream(payload);
    payloadStream.setVersion(QDataStream::Qt_5_12);
    payloadStream.setByteOrder(QDataStream::BigEndian);

    QString stateMessage;
    payloadStream >> stateMessage;

    if (payloadStream.status() != QDataStream::Ok) {
        qWarning() << "Error while parsing State payload.";
        return;
    }

    qDebug() << "Successfully parsed State message:" << stateMessage;

    // 更新状态栏
    QDate currentDate = QDate::currentDate();
    QTime currentTime = QTime::currentTime();
    QString dateTimeString = "Time: " + currentDate.toString("yyyy-MM-dd") + " " + currentTime.toString("HH:mm:ss"); // Removed 'a' for 24h
    QString ModeString = "Mode: " + stateMessage;
    QString StateString = dateTimeString + " | " + ModeString;
    ui->StateLabel->setText(StateString);
}

/**
 * @brief 在UI的LogTextEdit控件中打印一条带时间戳和颜色的日志。
 * @param level 日志级别 (Info, Success, Warning, Error)
 * @param message 要打印的日志内容
 */
void mainWindows::logMessage(LogLevel level, const QString& message)
{
    // 确保UI控件存在，控件名为 LogTextEdit
    if (!ui->LogTextEdit) {
        return;
    }

    // 1. 获取当前时间并格式化
    QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");

    // 2. 根据日志级别选择颜色和前缀
    QString color;
    QString prefix;

    switch (level) {
    case LogLevel::Success:
        color = "green";
        prefix = "[成功]";
        break;
    case LogLevel::Warning:
        color = "orange";
        prefix = "[警告]";
        break;
    case LogLevel::Error:
        color = "red";
        prefix = "[错误]";
        break;
    case LogLevel::Info:
    default:
        color = "black"; // 在亮色主题下，black 或 darkblue 都是不错的选择
        prefix = "[信息]";
        break;
    }

    // 3. 组合最终的HTML格式化字符串
    QString htmlMessage = QString("[%1] <font color='%2'><b>%3</b> %4</font>")
                              .arg(currentTime)
                              .arg(color)
                              .arg(prefix)
                              .arg(message.toHtmlEscaped());

    // 4. 追加到UI控件
    ui->LogTextEdit->appendHtml(htmlMessage);

    // 5. 自动滚动到底部
    ui->LogTextEdit->ensureCursorVisible();
}
/**
 * @brief 向LossPlot中加入数据点
 * @param epoch 当前轮数
 * @param lossValue loss值
 */
void mainWindows::addLossDataPoint(int epoch, double lossValue)
{
    if (!m_lossSeries || !m_lossXAxis || !m_lossYAxis) return;

    // 向系列中添加新点
    m_lossSeries->append(epoch, lossValue);

    // 自动调整坐标轴范围以适应新数据
    m_lossXAxis->setMax(epoch); // 更新X轴最大值

    // 对于Y轴，我们可以让它稍微动态一些
    if (lossValue > m_lossYAxis->max()) {
        m_lossYAxis->setMax(lossValue * 1.1); // 留出10%的顶部空间
    }
    if (lossValue < m_lossYAxis->min() || m_lossYAxis->min() < 0) {
        // 通常loss不会是负数，但以防万一
        m_lossYAxis->setMin(qMax(0.0, lossValue * 0.9));
    }
}
/**
 * @brief 向AccuracyPlot中加入数据点
 * @param epoch 当前轮数
 * @param accuracyValue 准确率
 */
void mainWindows::addAccuracyDataPoint(int epoch, double accuracyValue)
{
    if (!m_accuracySeries || !m_accuracyXAxis) return;

    // 假设传入的accuracy是0-1的小数，我们乘以100变为百分比
    m_accuracySeries->append(epoch, accuracyValue * 100.0);

    // 自动调整X轴
    m_accuracyXAxis->setMax(epoch);

    // Y轴范围通常固定在0-100，所以不需要调整
}
/**
 * @brief 清除模型训练的loss和accuracy
 */
void mainWindows::clearAllChartData()
{
    if(m_lossSeries) m_lossSeries->clear();
    if(m_accuracySeries) m_accuracySeries->clear();

    // 重置坐标轴范围
    if(m_lossXAxis) m_lossXAxis->setRange(0, 10); // 重置为初始范围
    if(m_lossYAxis) m_lossYAxis->setRange(0, 1.0);
    if(m_accuracyXAxis) m_accuracyXAxis->setRange(0, 10);
    // Y轴的accuracy范围固定，无需重置
}
/**
 * @brief selectTrainScriptButton按键槽
 */
void mainWindows::on_selectTrainScriptButton_clicked()
{
    // 1. 弹出文件选择对话框
    // 参数：父窗口, 对话框标题, 默认目录, 文件类型过滤器
    QString filePath = QFileDialog::getOpenFileName(
        this,
        "请选择 train.py 脚本文件",
        "", // 默认目录，留空则使用上次的或系统默认
        "Python脚本 (*.py);;所有文件 (*.*)" // 文件过滤器，优先显示.py文件
        );

    // 2. 检查用户是否选择了文件（而不是点击了“取消”）
    if (!filePath.isEmpty()) {
        // a. 验证文件名是否是 train.py (可选，但强烈推荐)
        QFileInfo fileInfo(filePath);
        if (fileInfo.fileName() != "train.py") {
            // 如果不是 train.py，给用户一个提示
            QMessageBox::warning(this, "文件选择错误", "请选择名为 train.py 的脚本文件。");
            logMessage(LogLevel::Warning, QString("用户选择了错误的文件: %1").arg(filePath));
            return; // 中断操作
        }

        // b. 如果文件正确，更新UI并记录日志
        ui->trainScriptPathLineEdit->setText(filePath);
        logMessage(LogLevel::Info, "已选择训练脚本路径: " + filePath);

        // c. 保存这个路径到成员变量或设置中，以备后用
        this->m_trainScriptPath = filePath;
    }
}
/**
 * @brief (最终版) 当“开始训练”按钮被点击时调用的槽函数。
 *        负责验证参数、设置环境、并启动Python训练进程。
 */
void mainWindows::on_beginTrainButton_clicked()
{
    // 1. ================== 检查进程状态 ==================
    if (m_trainProcess->state() != QProcess::NotRunning) {
        logMessage(LogLevel::Warning, "已有训练正在进行中，请先停止。");
        QMessageBox::warning(this, "提示", "已有训练正在进行中，请先停止。");
        return;
    }

    // 2. ================== 获取并验证UI参数 ==================
    QString pythonExecutable = ui->pythonPathLineEdit->text();
    if (pythonExecutable.isEmpty() || !QFile::exists(pythonExecutable)) {
        logMessage(LogLevel::Error, "Python解释器路径无效或文件不存在！");
        QMessageBox::critical(this, "错误", "请先选择一个有效的Python解释器！");
        return;
    }

    QString trainScriptPath = ui->trainScriptPathLineEdit->text();
    if (trainScriptPath.isEmpty() || !QFile::exists(trainScriptPath)) {
        logMessage(LogLevel::Error, "训练脚本路径无效或文件不存在！");
        QMessageBox::critical(this, "错误", "请先选择一个有效的train.py脚本文件！");
        return;
    }

    int epochs = ui->TrainNumBox->value();
    ui->TrainNumBox->setEnabled(false);
    // 3. ================== 准备UI和数据 ==================
    clearAllChartData(); // 清空旧的图表数据
    logMessage(LogLevel::Info, "开始新的训练...");

    // 4. ================== [核心] 设置进程环境 ==================
    // a. 获取系统当前的环境变量
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    // b. 从Python解释器路径推断出Conda环境的根目录
    QFileInfo pythonInfo(pythonExecutable);
    QString condaEnvPath = pythonInfo.dir().path(); // e.g., "D:/anaconda/envs/pytorch"

    // c. 构建需要添加到PATH中的Conda标准路径列表
    QStringList pathsToPrepend;
    pathsToPrepend << condaEnvPath;
    pathsToPrepend << condaEnvPath + "/Library/mingw-w64/bin";
    pathsToPrepend << condaEnvPath + "/Library/usr/bin";
    pathsToPrepend << condaEnvPath + "/Library/bin";
    pathsToPrepend << condaEnvPath + "/Scripts";
    pathsToPrepend << condaEnvPath + "/bin";

    // d. 将这些新路径添加到现有PATH变量的最前面，用分号分隔 (Windows)
    QString newPath = pathsToPrepend.join(';') + ";" + env.value("Path");
    env.insert("PATH", newPath);

    // e. 将这个包含了正确PATH的新环境应用到我们的进程上
    m_trainProcess->setProcessEnvironment(env);

    // f. (推荐) 设置脚本的工作目录为脚本所在的目录，避免相对路径问题
    QFileInfo scriptInfo(trainScriptPath);
    m_trainProcess->setWorkingDirectory(scriptInfo.path());

    // 5. ================== 准备参数并启动进程 ==================
    QStringList arguments;
    arguments << trainScriptPath << "--epochs" << QString::number(epochs);

    logMessage(LogLevel::Info, QString("执行命令: \"%1\" %2").arg(pythonExecutable).arg(arguments.join(" ")));

    // 使用设置好的环境和工作目录来启动进程
    m_trainProcess->start(pythonExecutable, arguments);

    // 6. ================== 更新UI状态 ==================
    ui->beginTrainButton->setEnabled(false);
    ui->stopTrainButton->setEnabled(true);
    clearAllChartData(); // 清空图表
    ui->DatasetprogressBar->setValue(0); // 将进度条重置为0
    ui->DatasetprogressBar->setEnabled(true); // 将进度条重置为0
}
/**
 * @brief stopTrainButton按键槽
 */
void mainWindows::on_stopTrainButton_clicked()
{
    if (m_trainProcess->state() == QProcess::NotRunning) {
        logMessage(LogLevel::Info, "当前没有训练在进行。");
        return;
    }

    logMessage(LogLevel::Warning, "正在尝试停止训练...");

    // 温和地终止进程。这会给Python一个机会去处理SIGTERM信号并优雅退出。
    m_trainProcess->terminate();

    // 设置一个短暂的超时，如果进程没有在1秒内退出，则强制杀死它
    if (!m_trainProcess->waitForFinished(1000)) {
        logMessage(LogLevel::Error, "进程未能优雅退出，将强制终止。");
        m_trainProcess->kill();
    }

    // 更新UI状态
    ui->beginTrainButton->setEnabled(true);
    ui->stopTrainButton->setEnabled(false);
    ui->TrainNumBox->setEnabled(true);
    ui->DatasetprogressBar->setValue(0);
    ui->batchProgressBar->setValue(0);
    ui->totalProgressBar->setValue(0);
}

/**
 * @brief (最终版) 当Python进程有标准输出时被调用。
 *        负责解码、解析JSON，并更新UI，忽略所有非JSON输出。
 */
void mainWindows::onTrainProcessReadyRead()
{
    // 使用UTF-8解码器来处理Python的输出，防止中文乱码
    QTextCodec *codec = QTextCodec::codecForName("UTF-8");

    // QProcess可能一次性发出多行数据，需要循环读取
    while (m_trainProcess->canReadLine()) {
        // 读取一行数据并解码
        QString outputLine = codec->toUnicode(m_trainProcess->readLine().trimmed());
        if (outputLine.isEmpty()) {
            continue;
        }

        // 尝试将这行输出解析为JSON对象
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(outputLine.toUtf8(), &parseError);

        // 只有当解析成功且根元素是一个对象时，才处理它
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            // --- 成功解析为JSON，这是我们唯一关心的数据 ---
            QJsonObject jsonObj = doc.object();

            // 确保JSON对象包含我们需要的所有键
            if (jsonObj.contains("epoch") && jsonObj.contains("loss") && jsonObj.contains("accuracy") &&
                jsonObj.contains("val_loss") && jsonObj.contains("val_accuracy"))
            {
                int epoch = jsonObj["epoch"].toInt();
                double loss = jsonObj["loss"].toDouble();
                double accuracy = jsonObj["accuracy"].toDouble();
                double val_loss = jsonObj["val_loss"].toDouble();
                double val_accuracy = jsonObj["val_accuracy"].toDouble();

                // 1. 更新图表
                addLossDataPoint(epoch, loss);
                addAccuracyDataPoint(epoch, accuracy);

                // 2. 使用我们自己的logMessage打印格式化后的关键信息
                logMessage(LogLevel::Info, QString("轮次 %1 - 训练损失: %2, 验证准确率: %3%")
                                               .arg(epoch, 2, 10, QChar('0')) // 格式化为两位数，如 01, 02
                                               .arg(loss, 0, 'f', 4)
                                               .arg(val_accuracy * 100, 0, 'f', 2));
            }else if (jsonObj.contains("type") && jsonObj["type"] == "dataset_progress") {
                // --- 数据集进度更新的JSON ---
                int progress = jsonObj["progress"].toInt();
                ui->DatasetprogressBar->setValue(progress);
                if(progress >= 100)
                {
                    logMessage(LogLevel::Success, QString("数据集已加载完毕!"));
                }
            }else if (jsonObj.contains("type") && jsonObj["type"] == "dataset_summary") {
                int sample_num = jsonObj["sample_num"].toInt();
                int class_num = jsonObj["class_num"].toInt();
                QStringList classLabels;
                if (jsonObj.contains("class_labels") && jsonObj["class_labels"].isArray()) {
                    QJsonArray labelsArray = jsonObj["class_labels"].toArray();
                    for (const QJsonValue &value : labelsArray) {
                        if (value.isString()) {
                            classLabels.append(value.toString());
                        }
                    }
                }
                QVector<int> featureShape;
                if (jsonObj.contains("feature_shape") && jsonObj["feature_shape"].isArray()) {
                    QJsonArray shapeArray = jsonObj["feature_shape"].toArray();
                    for (const QJsonValue &value : shapeArray) {
                        // isDouble() 可以同时判断整数和浮点数
                        if (value.isDouble()) {
                            featureShape.append(value.toInt());
                        }
                    }
                }
                // --- 解析结束，现在可以使用这些变量来更新UI ---
                logMessage(LogLevel::Info, QString("数据集摘要: 共 %1 个样本, 分为 %2 个类别。")
                                               .arg(sample_num)
                                               .arg(class_num));
                logMessage(LogLevel::Info, QString("类别标签: %1").arg(classLabels.join(", ")));
                // 为了美观地打印形状，手动构建 "3 x 9 x 13" 这样的字符串
                QStringList shapeParts;
                for (int dim : featureShape) {
                    shapeParts.append(QString::number(dim));
                }
                logMessage(LogLevel::Info, QString("特征形状: %1").arg(shapeParts.join(" x ")));
            }else if(jsonObj.contains("train_dataset_num") && jsonObj.contains("val_dataset_num"))
            {
                int train_dataset_num = jsonObj["train_dataset_num"].toInt();
                int val_dataset_num = jsonObj["val_dataset_num"].toInt();
                logMessage(LogLevel::Info, QString("训练集样本数: %1").arg(train_dataset_num));
                logMessage(LogLevel::Info, QString("测试集样本数: %1").arg(val_dataset_num));
            }else if(jsonObj.contains("save_dir"))
            {
                QString saveDir = jsonObj["save_dir"].toString();
                logMessage(LogLevel::Info, QString("模型权重将保存到目录: %1").arg(saveDir));
            }else if(jsonObj.contains("type") && jsonObj["type"] == "Model_Val")
            {
                double best_model_accuracy = jsonObj["val_acc"].toDouble();
                logMessage(LogLevel::Success, QString("新的最佳模型已保存！验证准确率: %1%").arg(best_model_accuracy));
            }else if(jsonObj.contains("type") && jsonObj["type"] == "training_batch_progress")
            {
                // 1. 从JSON对象中提取数据
                int epoch = jsonObj.value("epoch").toInt(0);
                int batch_index = jsonObj.value("batch_index").toInt(0);
                int total_batches = jsonObj.value("total_batches").toInt(0);
                double loss = jsonObj.value("loss").toDouble(0.0);
                double accuracy = jsonObj.value("accuracy").toDouble(0.0);

                // 2. 计算一个全局的、连续的批次索引，用于绘图
                //int global_batch_index = (epoch - 1) * total_batches + (batch_index - 1);

                // 3. 更新图表
                //addLossDataPoint(global_batch_index, loss);
                //addAccuracyDataPoint(global_batch_index, accuracy);

                // 4. 更新【当前轮次】的进度条 (batchProgressBar)
                if (ui->batchProgressBar) { // 检查UI指针是否有效
                    if (total_batches > 0) {
                        int batch_progress_percent = (batch_index * 100) / total_batches;
                        ui->batchProgressBar->setValue(batch_progress_percent);
                    }
                }

                // 5. 更新【总训练】的进度条 (totalProgressBar)
                if (ui->totalProgressBar && ui->TrainNumBox->value() > 0 && total_batches > 0) {
                    // 总进度的百分比计算稍微复杂一点：
                    // (已完成的轮数百分比) + (当前轮次已完成的百分比) / (总轮数)

                    // a. 先计算已完成的轮数占了多少进度
                    double completed_epochs_progress = static_cast<double>(epoch - 1) / ui->TrainNumBox->value();

                    // b. 再计算当前这一轮内部完成了多少进度
                    double current_epoch_progress = static_cast<double>(batch_index) / total_batches;

                    // c. 将当前轮的进度，按比例缩放到总进度中
                    double total_progress = completed_epochs_progress + (current_epoch_progress / ui->TrainNumBox->value());

                    // d. 转换为百分比整数并设置给进度条
                    ui->totalProgressBar->setValue(static_cast<int>(total_progress * 100));
                }

                // 6. 更新文本标签，与进度条配合显示
                if (ui->progressLabel) {
                    QString progressText = QString("轮次: %1/%2")
                                               .arg(epoch).arg(ui->TrainNumBox->value());
                    ui->progressLabel->setText(progressText);
                }
            }else if (jsonObj.contains("type") && jsonObj["type"] == "Train_device") {

                // 2. 提取设备名称字符串
                QString deviceName = jsonObj["dev"].toString(); // "cuda" 或 "cpu"
                logMessage(LogLevel::Info, QString("训练将在设备上运行: %1").arg(deviceName.toUpper()));
            }
        }
    }
}

void mainWindows::onTrainProcessErrorOccurred(QProcess::ProcessError error)
{
    Q_UNUSED(error);
    logMessage(LogLevel::Warning, "训练进程中止: " + m_trainProcess->errorString());
    ui->DatasetprogressBar->setValue(0);
    ui->batchProgressBar->setValue(0);
    ui->totalProgressBar->setValue(0);
}

void mainWindows::onTrainProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitStatus == QProcess::CrashExit) {
        logMessage(LogLevel::Success, QString("训练进程已关闭！"));
    } else {
        logMessage(LogLevel::Success, QString("训练进程已结束，退出代码: %1").arg(exitCode));
    }
    ui->TrainNumBox->setEnabled(true);

    // 恢复UI状态
    ui->beginTrainButton->setEnabled(true);
    ui->stopTrainButton->setEnabled(false);
    ui->DatasetprogressBar->setValue(0);
    ui->batchProgressBar->setValue(0);
    ui->totalProgressBar->setValue(0);
}


void mainWindows::on_selectPythonPathButton_clicked()
{
    // 在Windows上，过滤器应该是可执行文件
    QString filter = "Python解释器 (python.exe);;所有文件 (*.*)";

    QString filePath = QFileDialog::getOpenFileName(
        this,
        "请选择Python解释器",
        "",
        filter
        );

    if (!filePath.isEmpty()) {
        ui->pythonPathLineEdit->setText(filePath);
        m_pythonExecutablePath = filePath; // 更新成员变量
        logMessage(LogLevel::Info, "已选择Python解释器: " + filePath);
    }
}

void mainWindows::saveSettings()
{
    // 创建一个QSettings对象，指定组织和应用程序名称
    QSettings settings("MyCompany", "LoongClient");

    // 保存Python解释器路径和训练脚本路径
    settings.setValue("pythonPath", ui->pythonPathLineEdit->text());
    settings.setValue("trainScriptPath", ui->trainScriptPathLineEdit->text());

    logMessage(LogLevel::Info, "设置已保存。");
}

void mainWindows::loadSettings()
{
    QSettings settings("MyCompany", "LoongClient");

    // 加载Python解释器路径
    QString pythonPath = settings.value("pythonPath").toString();
    if (!pythonPath.isEmpty() && QFile::exists(pythonPath)) {
        ui->pythonPathLineEdit->setText(pythonPath);
        m_pythonExecutablePath = pythonPath;
        logMessage(LogLevel::Info, "已加载上次的Python解释器路径。");
    } else {
        // 如果没有保存的路径，尝试自动检测
        // 在Windows上找python.exe, 在Linux上找python3
#ifdef Q_OS_WIN
        QStringList defaultPaths = {"python.exe", "python3.exe"};
#else
        QStringList defaultPaths = {"python3.7", "python3", "python"};
#endif
        for(const QString& cmd : defaultPaths) {
            QString foundPath = QStandardPaths::findExecutable(cmd);
            if (!foundPath.isEmpty()) {
                m_pythonExecutablePath = foundPath;
                ui->pythonPathLineEdit->setText(m_pythonExecutablePath);
                logMessage(LogLevel::Info, QString("自动检测到Python解释器: %1").arg(m_pythonExecutablePath));
                break; // 找到一个就停止
            }
        }
    }

    // 加载训练脚本路径
    QString trainScriptPath = settings.value("trainScriptPath").toString();
    if (!trainScriptPath.isEmpty()) {
        ui->trainScriptPathLineEdit->setText(trainScriptPath);
    }
    logMessage(LogLevel::Info, "设置已加载。");
}


void mainWindows::on_ConnectSetButton_clicked()
{
    emit openConnectSetter();
}


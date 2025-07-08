#include "widget.h"
#include "ui_widget.h"
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QStandardPaths>
#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QtNetwork>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QFileInfo>
#include <QDirIterator>
#include <QMessageBox>
#include <QScreen>
#include <QGuiApplication> // 包含屏幕信息
Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
    , m_pythonModelProcess(nullptr)
    , m_mfccDisplayWindow(nullptr)
    , m_axisRectX(nullptr), m_axisRectY(nullptr), m_axisRectZ(nullptr)
    , m_graphX(nullptr), m_graphY(nullptr), m_graphZ(nullptr)
{
    ui->setupUi(this);
    // --- 防止图形界面卡死心跳 ---
    m_uiHeartbeatTimer = new QTimer(this);
    connect(m_uiHeartbeatTimer, &QTimer::timeout, this, [=]() {
        this->update();
    });
    m_uiHeartbeatTimer->start(10000);
    // --- 图形界面初始化，Beep控制器初始化 ---
    beepctl = new BeepCtl(this);
    ui->MfccPlotButton->setEnabled(false);
    tcpSocket = nullptr;
    setLED(ui->NetworkLabel,0,16);
    setupMultiAxisPlot();
    setLED(ui->ModelStateLabel,2,16);
    setLED(ui->DeviceStateLabel,0,16);
    ui->MoniterButton->setEnabled(false);
    ui->CollectProgressBar->setEnabled(false);
    ui->CollectStopButton->setEnabled(false);
    ui->HistoryBox->setMaxVisibleItems(5);

    if (qApp->organizationName().isEmpty()) qApp->setOrganizationName("Loong");
    if (qApp->applicationName().isEmpty()) qApp->setApplicationName("Crazy");
    // --- 数据共享目录m_csvDataPath ---
    m_csvDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (m_csvDataPath.isEmpty()) {
        m_csvDataPath = QDir::currentPath();
        qWarning() << "Could not get AppDataLocation, using current path:" << m_csvDataPath;
    }
    m_csvDataPath += "/sensor_data_for_python";
    // --- 清除历史数据 ---
    QDir dir(getProcessedCsvDir());
    if (dir.exists()) {
        qDebug() << "Directory exists, clearing its contents:" << getProcessedCsvDir();
        dir.setFilter(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
        for (const QFileInfo &fileInfo : dir.entryInfoList()) {
            if (fileInfo.isDir()) {
                QDir subDir(fileInfo.absoluteFilePath());
                if (!subDir.removeRecursively()) {
                    qWarning() << "Failed to remove sub-directory:" << fileInfo.absoluteFilePath();
                }
            } else {
                if (!QFile::remove(fileInfo.absoluteFilePath())) {
                    qWarning() << "Failed to remove file:" << fileInfo.absoluteFilePath();
                }
            }
        }
    } else {
        qDebug() << "Directory does not exist, creating it:" << getProcessedCsvDir();
        if (!dir.mkpath(".")) {
            qWarning() << "Failed to create directory:" << getProcessedCsvDir();
        }
    }
    // --- 初始化 HistoryBox ---
    populateHistoryBox(); // 程序启动时填充一次

    // --- Python模型部署进程创建 ---
    m_pythonModelProcess = new QProcess(this);

    // --- Python进程端模型输出监听并处理 ---
    connect(m_pythonModelProcess, &QProcess::readyReadStandardOutput, this, [this](){
        QDate currentDate = QDate::currentDate();
        QTime currentTime = QTime::currentTime();
        QString dateTimePrefix = QString("[%1 %2] ")
                                     .arg(currentDate.toString("yyyy-MM-dd"))
                                     .arg(currentTime.toString("HH:mm:ss"));

        while (m_pythonModelProcess->canReadLine()) {
            QByteArray lineData = m_pythonModelProcess->readLine().trimmed();
            if (lineData.isEmpty()) continue;
            QString messageContent = QString::fromUtf8(lineData);
            QJsonParseError parseError;
            QJsonDocument doc = QJsonDocument::fromJson(lineData, &parseError);
            if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                QJsonObject jsonObj = doc.object();
                if (jsonObj.value("status").toString() == "success") {
                    // * 解析Json输出，与python端键值对一一对应.
                    className = jsonObj.value("predicted_class_name").toString();
                    confidence = jsonObj.value("confidence").toDouble();
                    QString fileName = jsonObj.value("file_name").toString("N/A");

                    // * 仅当置信度足够大时才确定为轴承状态
                    if(confidence >= 85)
                    {
                        confidence_state = confidence;
                        className_state = className;
                        // * TCP/IP发送模型预测类型-置信度
                        if(tcpSocket != nullptr && tcpSocket->state() == QAbstractSocket::ConnectedState)
                        {
                            if(className.contains("healthy")||className.contains("Healthy"))
                            {
                                emit newModelOutReadyToSend(QString("Healthy"),confidence);
                            }else{
                                emit newModelOutReadyToSend(className,confidence);
                            }
                        }
                        if(className.contains("inner"))
                        {
                            setLED(ui->ModelStateLabel,4,16);
                        }else if(className.contains("outer"))
                        {
                            setLED(ui->ModelStateLabel,6,16);
                        }else
                        {
                            setLED(ui->ModelStateLabel,2,16);
                        }
                        // * 制定轴承损失级别
                        if(className.contains("healthy")||className.contains("Healthy")){
                            rankAlert = 0;
                            setLED(ui->DeviceStateLabel,2,16); //绿色
                        }else if(className == "0.7inner" || className == "0.7outer" || className == "0.9inner" || className == "0.9outer"){
                            rankAlert = 1;
                            setLED(ui->DeviceStateLabel,3,16); //黄色
                        }else if(className == "1.1inner" || className == "1.1outer" || className == "1.3inner" || className == "1.3outer"){
                            rankAlert = 2;
                            setLED(ui->DeviceStateLabel,5,16); //橙色
                        }else if(className == "1.5inner" || className == "1.5outer" || className == "1.7inner" || className == "1.7outer"){
                            rankAlert = 3;
                            setLED(ui->DeviceStateLabel,1,16); //红色
                        }
                        // * 更新蜂鸣器判别缓冲器
                        for(int i=0;i<9;i++)
                        {
                            rankAlert_Buf[i] = rankAlert_Buf[i+1];
                        }
                        rankAlert_Buf[9] = rankAlert;
                        char Light = 0;
                        char Medium = 0;
                        char Severe = 0;
                        for(int i=0;i<9;i++)
                        {
                            if(rankAlert_Buf[i] == 1)
                            {
                                Light ++;
                            }else if(rankAlert_Buf[i] == 2)
                            {
                                Medium ++;
                            }else if(rankAlert_Buf[i] == 3)
                            {
                                Severe ++;
                            }
                        }
                        // * 蜂鸣器警报
                        if(Mode == "Monitor")
                        {
                            if(rankAlert == 0)
                            {
                                beepctl -> stopAlert();
                            }else if(Light >= 6)
                            {
                                beepctl -> alertLightDamage();
                            }else if(Medium >= 6)
                            {
                                beepctl -> alertMediumDamage();
                            }else if(Severe >= 6)
                            {
                                beepctl -> alertSevereDamage();
                            }
                        }
                        // * 将成功预测处理的文件添加到 HistoryBox，即历史数据保存，Python 在处理成功后，文件 fileName 已经被移动到了 processed_csv 目录
                        if (!fileName.isEmpty() && fileName != "N/A") {
                            if (ui->HistoryBox->count() == 1 && ui->HistoryBox->itemData(0).toString().isEmpty()) {
                                if (ui->HistoryBox->itemText(0) == "没有历史数据" || ui->HistoryBox->itemText(0) == "历史数据为空") {
                                    ui->HistoryBox->removeItem(0);
                                }
                            }
                            // ** 检查文件是否真的在 processed_csv 目录中
                            QString processedFilePath = getProcessedCsvDir() + "/" + fileName;
                            if (QFile::exists(processedFilePath)) {
                                addHistoryItem(fileName);
                                qDebug() << "Added to HistoryBox from Python success:" << fileName;
                                // ** 清理超限的历史数据，最多保存50条
                                cleanupOldHistoryFiles();
                            } else {
                                qWarning() << "Python reported success for" << fileName << "but it was not found in processed_csv directory.";
                            }
                        }

                        ui->HistoryBox->setEnabled(true);
                        ui->HistoryBackButton->setEnabled(true);
                        ui->HistoryCleanButton->setEnabled(true);
                        ui->HistoryCleanAllButton->setEnabled(true);

                        // * 时间序列图的更新
                        int classIndex = jsonObj.value("predicted_class_index").toInt();
                        if (m_mfccDisplayWindow) {
                            m_mfccDisplayWindow->addClassTimeData(className, classIndex);
                        }

                        // * 处理 MFCC 特征，3轴-9帧-每帧13个MFCC系数
                        if (jsonObj.contains("features_to_display") && jsonObj.value("features_to_display").isArray()) {
                            QJsonArray allAxesJsonArray = jsonObj.value("features_to_display").toArray();
                            QVector<QVector<QVector<double>>> allAxesMfccData;

                            for (const QJsonValue& axisVal : allAxesJsonArray) {
                                if (axisVal.isArray()) {
                                    QVector<QVector<double>> singleAxisMfcc;
                                    QJsonArray framesArray = axisVal.toArray();
                                    for (const QJsonValue& frameVal : framesArray) {
                                        if (frameVal.isArray()) {
                                            QVector<double> frameCoefficients;
                                            QJsonArray coeffsArray = frameVal.toArray();
                                            for (const QJsonValue& coeffVal : coeffsArray) {
                                                if (coeffVal.isDouble()) {
                                                    frameCoefficients.append(coeffVal.toDouble());
                                                }
                                            }
                                            singleAxisMfcc.append(frameCoefficients);
                                        }
                                    }
                                    allAxesMfccData.append(singleAxisMfcc);
                                }
                            }

                            if (allAxesMfccData.size() == 3 && m_mfccDisplayWindow) {
                                // ** 将MFCC系数传给第二个窗口显示.
                                m_mfccDisplayWindow->displayMfccFeatures(allAxesMfccData);
                            } else if (!m_mfccDisplayWindow) {
                                qWarning() << "MFCC显示窗口未创建，无法显示特征。";
                            } else if (allAxesMfccData.size() != 3) {
                                qWarning() << "解析到的MFCC数据轴数不为3:" << allAxesMfccData.size();
                            }
                        }

                        // * 类别概率饼图更新
                        QJsonObject probabilitiesObj = jsonObj.value("all_class_probabilities").toObject();
                        if (!probabilitiesObj.isEmpty()) {
                            QMap<QString, double> probabilitiesMap;
                            for (auto it = probabilitiesObj.constBegin(); it != probabilitiesObj.constEnd(); ++it) {
                                probabilitiesMap.insert(it.key(), it.value().toDouble());
                            }

                            qDebug() << "Map size after parsing:" << probabilitiesMap.size();
                            if (m_mfccDisplayWindow) {
                                m_mfccDisplayWindow->updatePieChart(probabilitiesMap);
                            }
                        }
                    }

                    qDebug() << dateTimePrefix << "Prediction for" << fileName << ":" << className << confidence << "%";
                }
            } else {
                if (messageContent.contains("Python: 成功加载模型", Qt::CaseInsensitive)) {
                    QString deploymentSuccessMsg = QString("%1<font color='blue'><b>模型部署成功.</b></font>")
                                                       .arg(dateTimePrefix);
                    Model_Deploy = true;
                    if (ui->SysEdit) {
                        ui->SysEdit->appendHtml(deploymentSuccessMsg);
                        ui->SysEdit->ensureCursorVisible();
                    }
                    qDebug() << dateTimePrefix << "Model deployed successfully (from stdout).";
                    ui->MfccPlotButton->setEnabled(true);
                }
            }
        }
    });

    // --- Python进程端模型文件不存在处理 ---
    connect(m_pythonModelProcess, &QProcess::readyReadStandardError, this, [this](){
        QDate currentDate = QDate::currentDate();
        QTime currentTime = QTime::currentTime();
        QString dateTimePrefix = QString("[%1 %2] ")
                                     .arg(currentDate.toString("yyyy-MM-dd"))
                                     .arg(currentTime.toString("HH:mm:ss"));
        QByteArray errorData = m_pythonModelProcess->readAllStandardError();
        QString errorMessageContent = QString::fromUtf8(errorData).trimmed();
        if (errorMessageContent.isEmpty()) return;
        qWarning() << dateTimePrefix << "Python (stderr):" << errorMessageContent;
        if (errorMessageContent.contains("模型文件", Qt::CaseInsensitive) &&
            errorMessageContent.contains("不存在", Qt::CaseInsensitive)) {
            if (ui->SysEdit) {
                QString deploymentFailedMsg = QString("%1<font color='red'><b>模型部署失败:</b> 模型文件可能不存在.</font>")
                                                  .arg(dateTimePrefix);
                ui->SysEdit->appendHtml(deploymentFailedMsg);
                ui->SysEdit->ensureCursorVisible();
            }
        }
    });

    // --- Python进程端模型服务意外中断处理 ---
    connect(m_pythonModelProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus exitStatus){
                QDate currentDate = QDate::currentDate();
                QTime currentTime = QTime::currentTime();
                QString dateTimePrefix = QString("[%1 %2] ")
                                             .arg(currentDate.toString("yyyy-MM-dd"))
                                             .arg(currentTime.toString("HH:mm:ss"));
                QString finishMessageContent = QString("Python process finished. Exit code: %1, Status: %2")
                                                   .arg(exitCode)
                                                   .arg(exitStatus == QProcess::NormalExit ? "Normal" : "Crash");
                qDebug() << dateTimePrefix << finishMessageContent;
                if (exitStatus == QProcess::CrashExit) {
                    qWarning() << dateTimePrefix << "Python process crashed!";
                    if (ui->SysEdit) {
                        QString crashMsg = QString("%1<font color='red'><b>Python模型服务意外终止 (崩溃).</b></font>")
                                               .arg(dateTimePrefix);
                        ui->SysEdit->appendHtml(crashMsg);
                        ui->SysEdit->ensureCursorVisible();
                    }
                }
            });

    // --- Python进程端模型部署启动失败处理 ---
    connect(m_pythonModelProcess, &QProcess::errorOccurred,
            this, [this](QProcess::ProcessError error){
                QDate currentDate = QDate::currentDate();
                QTime currentTime = QTime::currentTime();
                QString dateTimePrefix = QString("[%1 %2] ")
                                             .arg(currentDate.toString("yyyy-MM-dd"))
                                             .arg(currentTime.toString("HH:mm:ss"));
                QString processErrorString = m_pythonModelProcess->errorString();
                qWarning() << dateTimePrefix << "Python process QProcess error:" << error << processErrorString;
                if (ui->SysEdit) {
                    QString deploymentFailedMsg = QString("%1<font color='red'><b>模型部署失败 (启动错误):</b> %2</font>")
                                                      .arg(dateTimePrefix)
                                                      .arg(processErrorString.toHtmlEscaped());
                    ui->SysEdit->appendHtml(deploymentFailedMsg);
                    ui->SysEdit->ensureCursorVisible();
                }
            });

    // --- 启动 Python 进程,部署模型 ---
    if (ui->SysEdit) {
        QDate currentDate = QDate::currentDate();
        QTime currentTime = QTime::currentTime();
        QString dateTimePrefix = QString("[%1 %2] ")
                                     .arg(currentDate.toString("yyyy-MM-dd"))
                                     .arg(currentTime.toString("HH:mm:ss"));
        QString initialMessage = QString("%1<font color='orange'>模型部署中...</font>").arg(dateTimePrefix);
        ui->SysEdit->appendHtml(initialMessage);
        ui->SysEdit->ensureCursorVisible();
    }

    QString pythonExecutable = "python3";
    QStringList arguments;
    QString scriptPath = QCoreApplication::applicationDirPath() + "/model_loader.py";
    arguments << scriptPath;
    arguments << m_csvDataPath;

    qDebug() << "Starting Python model server:" << pythonExecutable << arguments;
    m_pythonModelProcess->start(pythonExecutable, arguments);

    if (!m_pythonModelProcess->waitForStarted(5000)) {
        qWarning() << "Failed to start Python model server:" << m_pythonModelProcess->errorString();
    } else {
        qDebug() << "Python model server started successfully.";
    }

    // --- 数据更新，状态栏更新 ---
    connect(&m_dataBatchTimer, &QTimer::timeout, this, &Widget::updatePlotWithNewBatch);
    m_dataBatchTimer.start(500);
    connect(&local_timer, &QTimer::timeout, this, &Widget::get_LocalTime);
    local_timer.start(10);


    // --- TCP/IP线程和数据发送器初始化 ---
    m_senderThread = new QThread(this);
    m_dataSender = new DataSender();
    m_dataSender->moveToThread(m_senderThread);

    // --- 建立主线程和TCP/IP副线程之间的通信 ---
    connect(this, &Widget::newDataReadyToSend, m_dataSender, &DataSender::sendData);
    connect(this, &Widget::newModelOutReadyToSend, m_dataSender, &DataSender::sendModelOutput);
    connect(this, &Widget::newStateToSend, m_dataSender, &DataSender::sendState);
    connect(this, &Widget::socketReady, m_dataSender, &DataSender::setSocket);
    connect(this, &Widget::clientHasDisconnected, m_dataSender, &DataSender::clientDisconnected);
    connect(m_dataSender, &DataSender::dataSentStatus, this, &Widget::onDataSenderStatus);
    connect(m_dataSender, &DataSender::clientStatusChanged, this, &Widget::onClientStatusChanged);
    connect(m_senderThread, &QThread::finished, m_dataSender, &QObject::deleteLater);
    m_senderThread->start();

    // --- TCP/IP网络服务器监听 ---
    tcpServer = new QTcpServer(this);
    Port = 0;
    bool serverStarted = tcpServer->listen(QHostAddress::Any,0);
    if (serverStarted) {
        Port = tcpServer->serverPort();
        qDebug() << "Server started listening on port:" << Port;
        connect(tcpServer,SIGNAL(newConnection()),this,SLOT(newConnection_SLOT()));
        setLED(ui->NetworkLabel,3,16);
    } else {
        qWarning() << "Server failed to start:" << tcpServer->errorString();
        setLED(ui->NetworkLabel,1,16);
    }

    // --- 创建第二窗口初始化 ---
    m_mfccDisplayWindow = new widget_2();
    // --- 监听第二窗口放回主窗口信号 ---
    connect(m_mfccDisplayWindow, &widget_2::backToMainRequested, this, &Widget::showMainWindow);
    // --- 获取屏幕分辨率 ---
    checkScreenResolution();

}

Widget::~Widget()
{
    // --- QT程序退出处理 ---
    // * 关闭数据读取驱动
    m_dataReader.closeDevice();
    // * 清除共享目录m_csvDataPath下来不及预测的CSV文件
    if (!m_csvDataPath.isEmpty()) {
        QDir csvDir(m_csvDataPath);
        if (csvDir.exists()) {
            qDebug() << "Cleaning up CSV files in:" << m_csvDataPath;
            csvDir.setNameFilters(QStringList() << "*.csv");
            csvDir.setFilter(QDir::Files);
            QStringList csvFiles = csvDir.entryList();
            if (csvFiles.isEmpty()) {
                qDebug() << "No .csv files to remove in" << m_csvDataPath;
            } else {
                for (const QString &fileName : csvFiles) {
                    QString filePath = csvDir.filePath(fileName);
                    if (QFile::remove(filePath)) {
                        qDebug() << "Removed CSV file:" << filePath;
                    } else {
                        qWarning() << "Failed to remove CSV file:" << filePath << "Error:" << QFile(filePath).errorString();
                    }
                }
                qDebug() << "Finished cleaning CSV files.";
            }
        } else {
            qWarning() << "CSV data directory does not exist, skipping cleanup:" << m_csvDataPath;
        }
    } else {
        qWarning() << "m_csvDataPath is empty, skipping CSV cleanup.";
    }
    // * 关闭Python进程
    if (m_pythonModelProcess) {
        qDebug() << "Terminating Python model server process...";
        m_pythonModelProcess->terminate();
        if (!m_pythonModelProcess->waitForFinished(3000)) {
            qWarning() << "Python process did not terminate gracefully, killing it.";
            m_pythonModelProcess->kill();
            m_pythonModelProcess->waitForFinished(1000);
        }
    }
    // * m_mfccDisplayWindow 由于没有父对象，需要手动删除
    if (m_mfccDisplayWindow) {
        delete m_mfccDisplayWindow;
        m_mfccDisplayWindow = nullptr;
    }

    // * 安全地退出线程
    if (m_senderThread && m_senderThread->isRunning()) {
        m_senderThread->quit();
        m_senderThread->wait(1000);
    }
    delete ui;
}

/**
 * @brief 获取历史数据所在目录
 */
QString Widget::getProcessedCsvDir()
{
    return m_csvDataPath + "/processed_csv";
}

/**
 * @brief 获取数据存储总目录
 */
QString Widget::getSensorDataDir()
{
    return m_csvDataPath;
}

/**
 * @brief 向HistoryBox中添加已处理数据选项
 * @param fullFileName 数据文件名称
 */
void Widget::addHistoryItem(const QString& fullFileName)
{
    if (!ui->HistoryBox) {
        qWarning() << "addHistoryItem: HistoryBox UI element is missing.";
        return;
    }

    if (fullFileName.isEmpty()) {
        qWarning() << "addHistoryItem: Received an empty filename. Skipping.";
        return;
    }

    // * 检查是否重复添加
    for (int i = 0; i < ui->HistoryBox->count(); ++i) {
        if (ui->HistoryBox->itemData(i).toString() == fullFileName) {
            qDebug() << "addHistoryItem: File" << fullFileName << "already exists in HistoryBox. Skipping.";
            return;
        }
    }

    // * 如果当前只有 "没有历史数据" 占位符，则移除它
    if (ui->HistoryBox->count() == 1 && ui->HistoryBox->itemData(0).toString().isEmpty()) {
        if (ui->HistoryBox->itemText(0) == "没有历史数据" || ui->HistoryBox->itemText(0) == "历史数据为空") {
            ui->HistoryBox->removeItem(0);
        }
    }

    // * 格式化文件名以供显示,数据文件名格式是 "data_YYYYMMDD_HHMMSS_ZZZ.csv"
    QString displayString;
    QString tempName = fullFileName;
    tempName.remove("data_");
    tempName.remove(".csv");

    if (tempName.length() >= 15) {                        // YYYYMMDD_HHMMSS (ZZZ部分可选)
        displayString += tempName.left(4) + "-";          // YYYY
        displayString += tempName.mid(4, 2) + "-";        // MM
        displayString += tempName.mid(6, 2) + " ";        // DD
        displayString += tempName.mid(9, 2) + ":";        // HH
        displayString += tempName.mid(11, 2) + ":";       // MM
        displayString += tempName.mid(13, 2);             // SS
        if (tempName.length() >= 19 && tempName.at(15) == '_') { // 检查是否有毫秒部分
            displayString += "." + tempName.mid(16, 3);   // ZZZ
        }
    } else {
        displayString = fullFileName;                     // 如果格式不符，直接显示完整文件名
    }

    // * 将新项插入到 ComboBox 的顶部
    ui->HistoryBox->insertItem(0, displayString, QVariant(fullFileName)); // 存储完整文件名作为用户数据

    // * 将新添加的项设为当前选中项
    ui->HistoryBox->setCurrentIndex(0);
    ui->HistoryBox->setMaxVisibleItems(5);
    // * 确保相关控件状态正确
    if (!ui->HistoryBox->isEnabled()) {
        ui->HistoryBox->setEnabled(true);
    }
    if (ui->HistoryBackButton && !ui->HistoryBackButton->isEnabled()) {
        ui->HistoryBackButton->setEnabled(true);
    }
    if (ui->HistoryCleanButton && !ui->HistoryCleanButton->isEnabled()) {
        ui->HistoryCleanButton->setEnabled(true);
    }
    if (ui->HistoryCleanAllButton && !ui->HistoryCleanAllButton->isEnabled()) {
        ui->HistoryCleanAllButton->setEnabled(true);
    }
}

/**
 * @brief HistoryBox初始化
 */
void Widget::populateHistoryBox()
{
    if (!ui->HistoryBox) {
        qWarning() << "populateHistoryBox: HistoryBox UI element is missing.";
        return;
    }

    ui->HistoryBox->clear(); // 清空现有项

    QString processedPath = getProcessedCsvDir();
    QDir processedDir(processedPath);

    if (!processedDir.exists()) {
        qWarning() << "populateHistoryBox: Processed CSV directory does not exist:" << processedPath;
        return;
    }

    processedDir.setNameFilters(QStringList() << "data_*.csv"); // 只查找 data_ 开头的CSV文件
    processedDir.setFilter(QDir::Files | QDir::NoDotAndDotDot);
    processedDir.setSorting(QDir::Name | QDir::Reversed);       // 按名称降序排列（新的在前）

    QFileInfoList fileList = processedDir.entryInfoList();
    if (fileList.isEmpty()) {
        ui->HistoryBox->addItem("没有历史数据");
        ui->HistoryBox->setEnabled(false);
        if(ui->HistoryBackButton)
        {
            if(Mode == "Monitor")
            {
                ui->HistoryBackButton->setEnabled(false);
            }
        }
        if(ui->HistoryCleanButton) ui->HistoryCleanButton->setEnabled(false);
    } else {
        ui->HistoryBox->setEnabled(true);
        if(ui->HistoryBackButton) ui->HistoryBackButton->setEnabled(true);
        if(ui->HistoryCleanButton) ui->HistoryCleanButton->setEnabled(true);
        for (const QFileInfo &fileInfo : fileList) {
            QString fileName = fileInfo.fileName();
            QString displayTime = fileName;
            displayTime.remove("data_");
            displayTime.remove(".csv");
            if (displayTime.length() >= 19) {                          // YYYYMMDD_HHMMSS_ZZZ
                // * 20231027_153005_123 -> 2023-10-27 15:30:05.123
                QString formattedDisplay;
                formattedDisplay += displayTime.left(4) + "-";         // YYYY
                formattedDisplay += displayTime.mid(4,2) + "-";        // MM
                formattedDisplay += displayTime.mid(6,2) + " ";        // DD
                formattedDisplay += displayTime.mid(9,2) + ":";        // HH
                formattedDisplay += displayTime.mid(11,2) + ":";       // MM
                formattedDisplay += displayTime.mid(13,2);             // SS
                if (displayTime.length() >= 19 && displayTime.at(15) == '_') { // 检查是否有毫秒
                    formattedDisplay += "." + displayTime.mid(16,3);   // ZZZ
                }
                ui->HistoryBox->addItem(formattedDisplay, fileName); // 显示格式化时间，用户数据存储完整文件名
            } else {
                ui->HistoryBox->addItem(fileName, fileName); // 格式不符，直接显示文件名
            }
            ui->HistoryBox->setMaxVisibleItems(5);
        }
    }
}

/**
 * @brief 加载历史数据进行回放功能
 * @param csvFilePath 待读取的历史数据文件路径
 */
bool Widget::loadAndDisplayCsvData(const QString& csvFilePath)
{
    if (!ui->time || !m_graphX || !m_graphY || !m_graphZ || !m_axisRectX || !m_axisRectY || !m_axisRectZ) {
        qWarning("Plot, graphs, or axis rects not initialized in updatePlotWithNewBatch!");
        return false;
    }

    QCustomPlot *customPlot = ui->time;

    QFile file(csvFilePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "loadAndDisplayCsvData: Could not open CSV file for reading:" << csvFilePath << file.errorString();
        return false;
    }

    QTextStream in(&file);
    QVector<double> timeKeys, xData, yData, zData;
    bool firstLine = true;                // 用于跳过表头
    int sampleIndex = 0;
    double timePerSample = 1.0 / 10000.0; // 与实时数据采样率一致,10KHz

    // * 遍历csv文件，加载数据, CSV格式是: Time,X,Y,Z
    while (!in.atEnd()) {
        QString line = in.readLine();
        if (firstLine) {
            firstLine = false;
            continue;
        }

        QStringList fields = line.split(',');
        if (fields.size() >= 3) { // 至少需要X,Y,Z三列数据
            bool okX, okY, okZ;
            // 如果CSV就是 X,Y,Z (没有独立时间列)，则是 fields[0], fields[1], fields[2]
            int xIdx = 1, yIdx = 2, zIdx = 3;

            double xVal = fields[xIdx].toDouble(&okX);
            double yVal = fields[yIdx].toDouble(&okY);
            double zVal = fields[zIdx].toDouble(&okZ);

            if (okX && okY && okZ) {
                xData.append(xVal);
                yData.append(yVal);
                zData.append(zVal);
                timeKeys.append(sampleIndex * timePerSample);
                sampleIndex++;
            } else {
                qWarning() << "loadAndDisplayCsvData: Could not parse line in" << csvFilePath << ":" << line;
            }
        }
    }
    file.close();

    if (xData.isEmpty()) {
        qWarning() << "loadAndDisplayCsvData: No valid data parsed from" << csvFilePath;
        return false;
    }

    // * 滤波处理
    const int filterWindowSize = 3; // [可调] 滤波窗口大小，可以设为3, 5, 7等奇数。值越大越平滑。
    xData = applyMovingAverageFilter(xData, filterWindowSize);
    yData = applyMovingAverageFilter(yData, filterWindowSize);
    zData = applyMovingAverageFilter(zData, filterWindowSize);
    // * 更新时域波形
    m_graphX->data()->clear();
    m_graphY->data()->clear();
    m_graphZ->data()->clear();

    m_graphX->addData(timeKeys, xData);
    m_graphY->addData(timeKeys, yData);
    m_graphZ->addData(timeKeys, zData);

    if (!timeKeys.isEmpty()) {
        m_axisRectZ->axis(QCPAxis::atBottom)->setRange(timeKeys.first(), timeKeys.last());
    } else {
        m_axisRectZ->axis(QCPAxis::atBottom)->setRange(0, (m_batchSize > 0 ? (m_batchSize - 1) : 0) * timePerSample);
    }

    m_graphX->rescaleValueAxis(false, true);
    m_graphY->rescaleValueAxis(false, true);
    m_graphZ->rescaleValueAxis(false, true);

    customPlot->replot();
    qDebug() << "loadAndDisplayCsvData: Successfully loaded and displayed data from" << csvFilePath;
    return true;
}

/**
 * @brief 清理旧的历史文件，并同步更新UI。
 * (修正版 v2：支持文件名与UI显示文本不一致的场景)
 * 最大保留maxFilesToKeep条历史数据，防止无限占用内存。
 */
void Widget::cleanupOldHistoryFiles()
{
    // * 获取已处理文件目录的路径和文件列表
    QString processedDir = getProcessedCsvDir();
    QDir dir(processedDir);
    // * 按修改时间降序排序（最新的在前，最旧的在后）
    QFileInfoList fileList = dir.entryInfoList({"*.csv"}, QDir::Files, QDir::Time);

    // * 定义要保留的最大文件数
    const int maxFilesToKeep = 50;

    // * 如果文件数量未超标，则无需清理，直接返回
    if (fileList.size() <= maxFilesToKeep) {
        return;
    }

    // * 计算需要删除的文件数量
    int filesToRemoveCount = fileList.size() - maxFilesToKeep;

    // * 循环删除多余的、最旧的文件
    for (int i = 0; i < filesToRemoveCount; ++i)
    {
        // ** 从文件列表的末尾获取最旧的文件信息
        const QFileInfo& oldestFileInfo = fileList.last();
        QString fileName = oldestFileInfo.fileName();
        QString filePathToRemove = oldestFileInfo.absoluteFilePath();

        // ** 将文件名转换为UI中的显示文本
        QString displayFormat = convertFileNameToDisplayFormat(fileName);

        // ** 如果转换失败(返回空)，无法在UI中定位，但仍然可以删除文件
        if (displayFormat.isEmpty()) {
            qWarning() << "Skipping UI removal for un-parsable file:" << fileName;
        } else {
            // *** 使用转换后的显示文本，在HistoryBox中精确查找
            int indexInComboBox = ui->HistoryBox->findText(displayFormat);

            // *** 如果在UI中找到了，就删除它
            if (indexInComboBox != -1) {
                qDebug() << "Removing item from HistoryBox:" << displayFormat;
                ui->HistoryBox->removeItem(indexInComboBox);
            } else {
                qWarning() << "Could not find display text" << displayFormat << "in HistoryBox.";
            }
        }

        // ** 从文件系统中删除最旧的数据文件
        if (QFile::remove(filePathToRemove)) {
            qDebug() << "Successfully deleted old history file:" << filePathToRemove;
        } else {
            qWarning() << "Failed to delete old history file:" << filePathToRemove;
            break;
        }

        // ** 从正在处理的文件列表中也移除它，以便下次循环能获取到下一个最旧的文件
        fileList.removeLast();
    }
}

/**
 * @brief 辅助函数，用于将文件名格式转换为HistoryBox中的显示格式
 * 例如: "data_20250705_161134_580.csv" -> "2025-07-05 16:11:34:580"
 */
QString Widget::convertFileNameToDisplayFormat(const QString &fileName)
{
    // * 使用正则表达式来解析文件名
    QRegExp rx("data_(\\d{8})_(\\d{6})_(\\d{3})\\.csv");

    if (rx.indexIn(fileName) != -1) {
        QString datePart = rx.cap(1); // 捕获 "20250705"
        QString timePart = rx.cap(2); // 捕获 "161134"
        QString msPart = rx.cap(3);   // 捕获 "580"

        // ** 格式化日期部分: "20250705" -> "2025-07-05"
        QString formattedDate = QString("%1-%2-%3")
                                    .arg(datePart.left(4))
                                    .arg(datePart.mid(4, 2))
                                    .arg(datePart.right(2));

        // ** 格式化时间部分: "161134" -> "16:11:34"
        QString formattedTime = QString("%1:%2:%3")
                                    .arg(timePart.left(2))
                                    .arg(timePart.mid(2, 2))
                                    .arg(timePart.right(2));

        return QString("%1 %2:%3").arg(formattedDate).arg(formattedTime).arg(msPart);
    }

    // * 如果文件名格式不匹配，返回一个空字符串或原始文件名以作标识
    qWarning() << "Could not parse filename:" << fileName;
    return QString();
}

/**
 * @brief 模型分析界面按键槽
 */
void Widget::on_MfccPlotButton_clicked()
{
    if (m_mfccDisplayWindow) {
        m_mfccDisplayWindow->showFullScreen(); // 显示MFCC窗口
        m_mfccDisplayWindow->raise();          // 将窗口置于顶层
        m_mfccDisplayWindow->activateWindow(); // 激活窗口
    } else {
        qWarning("MFCC display window is not initialized!");
    }
}

/**
 * @brief 显示主窗口信号槽
 */
void Widget::showMainWindow()
{
    this->showFullScreen(); // 显示主窗口
    this->raise();          // 将窗口置于顶层
    this->activateWindow(); // 激活窗口
}

/**
 * @brief 模拟LED
 */
void Widget::setLED(QLabel* label, int color, int size)
{
    label->setText("");
    QString min_width = QString("min-width: %1px;").arg(size);
    QString min_height = QString("min-height: %1px;").arg(size);
    QString max_width = QString("max-width: %1px;").arg(size);
    QString max_height = QString("max-height: %1px;").arg(size);
    QString border_radius = QString("border-radius: %1px;").arg(size/2);
    QString border = QString("border:1px solid black;");
    QString background = "background-color:";
    switch (color) {
    case 0: background += "rgb(190,190,190)"; break; // grey
    case 1: background += "rgb(255,0,0)";     break; // red
    case 2: background += "rgb(0,255,0)";     break; // green
    case 3: background += "rgb(255,255,0)";   break; // yellow
    case 4: background += "rgb(0,0,255)";     break; // Blue
    case 5: background += "rgb(255,165,0)";   break; // Orange
    case 6: background += "rgb(128,0,128)";   break; // Purple
    case 7: background += "rgb(0,255,255)";   break; // Cyan
    case 8: background += "rgb(0,0,0)";       break; // Black
    default: break;
    }
    const QString SheetStyle = min_width + min_height + max_width + max_height + border_radius + border + background;
    label->setStyleSheet(SheetStyle);
}

/**
 * @brief 初始化时域波形窗口
 */
void Widget::setupMultiAxisPlot()
{
    if (!ui->time) {
        qWarning("QCustomPlot widget 'time' not found in UI!");
        return;
    }
    QCustomPlot *customPlot = ui->time;

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
    m_axisRectX->axis(QCPAxis::atLeft)->setLabel("Acc X (V)");
    m_axisRectX->setMarginGroup(QCP::msLeft | QCP::msRight, marginGroup);

    m_axisRectY = new QCPAxisRect(customPlot);
    if (!m_axisRectY) { qCritical("Failed to create m_axisRectY!"); return; }
    m_axisRectY->setMinimumSize(0, 100);
    m_axisRectY->setupFullAxesBox(true);
    m_axisRectY->axis(QCPAxis::atBottom)->setTickLabels(false);
    m_axisRectY->axis(QCPAxis::atBottom)->setTicks(false);
    m_axisRectY->axis(QCPAxis::atLeft)->setLabel("Acc Y (V)");
    m_axisRectY->setMarginGroup(QCP::msLeft | QCP::msRight, marginGroup);

    m_axisRectZ = new QCPAxisRect(customPlot);
    if (!m_axisRectZ) { qCritical("Failed to create m_axisRectZ!"); return; }
    m_axisRectZ->setMinimumSize(0, 100);
    m_axisRectZ->setupFullAxesBox(true);
    m_axisRectZ->axis(QCPAxis::atLeft)->setLabel("Acc Z (V)");
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
 * @brief 在Moniter和Collect模式下，更新采样数据，更新显示波形.(核心)
 * 每批数据有1024个xyz三轴加速度样点.
 * Moniter模式下，将采样数据打包放入总数据目录下，等待模型分析.
 * Collect模式下，将采样数据放入对应标签的csv文件中.
 */
void Widget::updatePlotWithNewBatch()
{
    // * 在Monitor和Collect模式中，采用不同的数据收集速率.
    if(Mode_Buf != Mode)
    {
        if(Mode == "Monitor")
        {
            m_dataBatchTimer.start(500); // 500 ms/次
        }else if(Mode == "Collect")
        {
            beepctl->stopAlert();
            m_dataBatchTimer.start(100); // 100 ms/次
        }else if(Mode == "History")
        {
            beepctl->stopAlert();
        }
    }
    Mode_Buf = Mode;
    // * History 不实时更新数据，直接返回.
    if(Mode == "History")
    {
        return;
    }
    if (!ui->time || !m_graphX || !m_graphY || !m_graphZ || !m_axisRectX || !m_axisRectY || !m_axisRectZ) {
        qWarning("Plot, graphs, or axis rects not initialized in updatePlotWithNewBatch!");
        return;
    }
    QCustomPlot *customPlot = ui->time;
    // * 数据读取
    QVector<double> timeData, xData_raw, yData_raw, zData_raw;
    bool success = m_dataReader.readDeviceData(timeData, xData_raw, yData_raw, zData_raw, m_currentBatchNumber);
    if (!success) {
        qDebug("Failed to read data batch from device. Skipping plot update.");
        return;
    }

    // * 滤波处理
    const int filterWindowSize = 3; // [可调] 滤波窗口大小，可以设为3, 5, 7等奇数。值越大越平滑。
    QVector<double> xData = applyMovingAverageFilter(xData_raw, filterWindowSize);
    QVector<double> yData = applyMovingAverageFilter(yData_raw, filterWindowSize);
    QVector<double> zData = applyMovingAverageFilter(zData_raw, filterWindowSize);

    // * 时间轴生成
    QVector<double> actualTimeKeys;
    if (!xData.isEmpty()) {
        if(tcpSocket != nullptr && tcpSocket->state() == QAbstractSocket::ConnectedState)
        {
          emit newDataReadyToSend(xData, yData, zData);
        }
        actualTimeKeys.resize(xData.size());
        double timePerSample = 1.0 / 10000.0;
        for (int i = 0; i < xData.size(); ++i) {
            actualTimeKeys[i] = i * timePerSample;
        }
    } else {
        qWarning("Received empty data batch. Skipping plot update.");
        return;
    }
    // * 模式选择与功能执行
    if(Mode == "Monitor")
    {
        // ** 将数据打包成一个个csv文件供模型预测，也用于历史回溯
        if(Model_Deploy == true)
        {
            if (!timeData.isEmpty()) {
                if (m_csvDataPath.isEmpty()) {
                    m_csvDataPath = QDir::currentPath() + "/sensor_data_for_python";
                    QDir dir;
                    if (!dir.exists(m_csvDataPath)) dir.mkpath(m_csvDataPath);
                }
                QDateTime currentDateTime = QDateTime::currentDateTime();
                QString timestamp = currentDateTime.toString("yyyyMMdd_HHmmss_zzz");
                QString csvFilename = m_csvDataPath + QString("/data_%1.csv").arg(timestamp);
                qDebug() << "Writing data to CSV:" << csvFilename;
                if (!writeDataToCsv(csvFilename, timeData, xData_raw, yData_raw, zData_raw)) {
                    qWarning() << "Failed to write data to CSV:" << csvFilename;
                } else {
                    qDebug() << "Successfully wrote" << csvFilename;
                }
            }
        }
    }else if(Mode == "Collect")
    {
        // ** 根据选择的标签将数据放入对应的csv文件中
        if(!finish)
        {
            if (!xData_raw.isEmpty() && ui->LabelBox) {
                // *** 标签检查
                QString currentLabel = ui->LabelBox->currentText().trimmed();
                if (currentLabel.isEmpty()) {
                    qWarning() << "Collect Mode: LabelBox is empty. Cannot determine CSV filename.";
                    if (ui->SysEdit) {
                        QDate cd = QDate::currentDate(); QTime ct = QTime::currentTime();
                        QString dtp = QString("[%1 %2] ").arg(cd.toString("yyyy-MM-dd")).arg(ct.toString("HH:mm:ss"));
                        ui->SysEdit->appendHtml(QString("%1<font color='orange'>Collect Mode Warning:</font> LabelBox is empty, data not saved.").arg(dtp));
                        ui->SysEdit->ensureCursorVisible();
                    }
                } else {
                    // *** 数据存放路径检查
                    QString collectSubPath = m_csvDataPath + "/Collect";
                    QDir collectDir(collectSubPath);
                    if (!collectDir.exists()) {
                        if (!collectDir.mkpath(".")) {
                            qWarning() << "Collect Mode: Failed to create Collect sub-directory:" << collectSubPath;
                            if (ui->SysEdit) {
                                QDate cd = QDate::currentDate(); QTime ct = QTime::currentTime();
                                QString dtp = QString("[%1 %2] ").arg(cd.toString("yyyy-MM-dd")).arg(ct.toString("HH:mm:ss"));
                                ui->SysEdit->appendHtml(QString("%1<font color='red'>Collect Mode Error:</font> Failed to create dir %2.").arg(dtp).arg(collectSubPath.toHtmlEscaped()));
                                ui->SysEdit->ensureCursorVisible();
                            }
                        }
                    }
                    // *** 标签裁剪修改(防止出现非法字符)
                    QString cleanLabel = currentLabel;
                    cleanLabel.remove(QRegExp(QStringLiteral("[^a-zA-Z0-9_.-]")));
                    if (cleanLabel.isEmpty()) cleanLabel = "default_collection";
                    QString targetCsvFilename = collectSubPath + QString("/%1.csv").arg(cleanLabel);

                    // *** 若更换标签，则打开新的标签文件
                    if (m_currentCollectCsvPath != targetCsvFilename) {
                        closeCollectCsvFile();
                        m_currentCollectCsvPath = targetCsvFilename;
                        if (!openAndPrepareCollectCsvFile(m_currentCollectCsvPath, "Time Stamp,X-axis,Y-axis,Z-axis\n")) {
                            qWarning() << "Collect Mode: Failed to open CSV for collection:" << m_currentCollectCsvPath;
                            if (ui->SysEdit) {
                                QDate cd = QDate::currentDate(); QTime ct = QTime::currentTime();
                                QString dtp = QString("[%1 %2] ").arg(cd.toString("yyyy-MM-dd")).arg(ct.toString("HH:mm:ss"));
                                ui->SysEdit->appendHtml(QString("%1<font color='red'>Collect Mode Error:</font> Failed to open %2.").arg(dtp).arg(m_currentCollectCsvPath.toHtmlEscaped()));
                                ui->SysEdit->ensureCursorVisible();
                            }
                        }
                    }

                    // *** 写入收集数据
                    bool batchWrittenSuccessfully = false;
                    if (m_collectCsvFile.isOpen() && m_collectCsvFile.isWritable()) {
                        qDebug() << "Collect Mode: Appending to CSV:" << m_currentCollectCsvPath;
                        QString currentBatchTimeStamp = QTime::currentTime().toString("HH:mm:ss.zzz");

                        for (int i = 0; i < xData_raw.size(); ++i) {
                            if (m_collectCsvStream.device() == &m_collectCsvFile) {
                                m_collectCsvStream << currentBatchTimeStamp << ","
                                                   << QString::number(xData_raw[i], 'f', 6) << ","
                                                   << QString::number(yData_raw[i], 'f', 6) << ","
                                                   << QString::number(zData_raw[i], 'f', 6) << "\n";
                            } else {
                                qWarning() << "Collect Mode: CSV stream invalid for" << m_currentCollectCsvPath;
                                break;
                            }
                        }
                        // *** 强制将缓冲区中所有待处理的数据立即写入到关联的设备（即 CSV 文件）中
                        m_collectCsvStream.flush();
                        if (m_collectCsvFile.error() == QFile::NoError) {
                            // *** 本次采集数据写入成功
                            batchWrittenSuccessfully = true;
                        } else {
                            qWarning() << "Collect Mode: Error writing to" << m_currentCollectCsvPath << ":" << m_collectCsvFile.errorString();
                            if (ui->SysEdit) {
                                QDate cd = QDate::currentDate(); QTime ct = QTime::currentTime();
                                QString dtp = QString("[%1 %2] ").arg(cd.toString("yyyy-MM-dd")).arg(ct.toString("HH:mm:ss"));
                                ui->SysEdit->appendHtml(QString("%1<font color='red'>Collect Mode Error:</font> Write failed %2. Err: %3")
                                                            .arg(dtp)
                                                            .arg(m_currentCollectCsvPath.toHtmlEscaped())
                                                            .arg(m_collectCsvFile.errorString().toHtmlEscaped()));
                                ui->SysEdit->ensureCursorVisible();
                            }
                        }
                    } else {
                        qWarning() << "Collect Mode: CSV" << m_currentCollectCsvPath << "not open for writing.";
                        if (ui->SysEdit && !m_currentCollectCsvPath.isEmpty()) {
                            QDate cd = QDate::currentDate(); QTime ct = QTime::currentTime();
                            QString dtp = QString("[%1 %2] ").arg(cd.toString("yyyy-MM-dd")).arg(ct.toString("HH:mm:ss"));
                            ui->SysEdit->appendHtml(QString("%1<font color='red'>Collect Mode Error:</font> File %2 not open.")
                                                        .arg(dtp).arg(m_currentCollectCsvPath.toHtmlEscaped()));
                            ui->SysEdit->ensureCursorVisible();
                        }
                    }

                    if (batchWrittenSuccessfully && ui->SysEdit) {
                        // *** 到此一次数据采集及检查均已通过
                        QDate cd = QDate::currentDate(); QTime ct = QTime::currentTime();
                        QString dtp = QString("[%1 %2] ").arg(cd.toString("yyyy-MM-dd")).arg(ct.toString("HH:mm:ss"));
                        QString successMsg = QString("%1<font color='DarkCyan'>Collect Mode:</font> %2 records to %3")
                                                 .arg(dtp)
                                                 .arg(xData.size())
                                                 .arg(QFileInfo(m_currentCollectCsvPath).fileName().toHtmlEscaped());
                        ui->SysEdit->appendHtml(successMsg);
                        ui->SysEdit->ensureCursorVisible();
                        m_collect_cnt ++;

                        // *** 更新进度条

                        // 1. 获取目标数量
                        int targetCount = ui->CollectTargetBox->value();

                        // 2. 更新进度条的范围（最大值）
                        if (ui->CollectProgressBar->maximum() != targetCount) {
                            ui->CollectProgressBar->setMaximum(targetCount);
                        }
                        // 3. 更新进度条的当前值
                        ui->CollectProgressBar->setValue(qMin(m_collect_cnt, targetCount));

                        // 4. 检查是否已达到目标，并给出反馈
                        if (m_collect_cnt >= targetCount) {
                            finish = true;
                            beepctl->notificationSuccess();
                            QMessageBox::information(this, "采集完成", QString("已成功采集 %1 个样本！").arg(targetCount));
                            Mode = "Monitor";
                            m_collect_cnt = 0;
                            ui->CollectProgressBar->setValue(0);
                            ui->CollectProgressBar->setEnabled(false);
                            ui->CollectStopButton->setEnabled(false);
                            ui->CollectStartButton->setEnabled(true);
                            ui->LabelBox->setEnabled(true);
                        }
                    }
                }
            } else if (xData_raw.isEmpty()) {
                qDebug("Collect Mode: No data to write (xData is empty).");
            } else if (!ui->LabelBox) {
                qWarning("Collect Mode: LabelBox UI element is missing.");
            }
        }
    }

    // * 波形绘制
    m_graphX->data()->clear();
    m_graphY->data()->clear();
    m_graphZ->data()->clear();

    m_graphX->addData(actualTimeKeys, xData);
    m_graphY->addData(actualTimeKeys, yData);
    m_graphZ->addData(actualTimeKeys, zData);

    if (!actualTimeKeys.isEmpty()) {
        m_axisRectZ->axis(QCPAxis::atBottom)->setRange(actualTimeKeys.first(), actualTimeKeys.last());
    } else {
        double timePerSample = 1.0 / 10000.0;
        m_axisRectZ->axis(QCPAxis::atBottom)->setRange(0, (m_batchSize > 0 ? (m_batchSize - 1) : 0) * timePerSample);
    }

    m_graphX->rescaleValueAxis(false, true);
    m_graphY->rescaleValueAxis(false, true);
    m_graphZ->rescaleValueAxis(false, true);

    customPlot->replot();
    m_currentBatchNumber++;
}

/**
 * @brief Moniter模式使用，打包数据为一个个csv文件，供模型分析和历史回溯
 */
bool Widget::writeDataToCsv(const QString& filename,
                            const QVector<double>& timeData,
                            const QVector<double>& xData,
                            const QVector<double>& yData,
                            const QVector<double>& zData)
{
    if (timeData.isEmpty()) {
        qWarning() << "Time data empty for CSV:" << filename;
        return false;
    }
    if (timeData.size() != xData.size() || timeData.size() != yData.size() || timeData.size() != zData.size()) {
        qWarning() << "Data vector size mismatch for CSV!" << filename;
        return false;
    }
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        qWarning() << "Cannot open file for writing:" << filename << file.errorString();
        return false;
    }
    QTextStream out(&file);
    out.setRealNumberPrecision(6); // Qt 5
    out.setRealNumberNotation(QTextStream::FixedNotation); // Qt 5, or use QString::number with 'f'

    out << "Time,X,Y,Z\n";
    for (int i = 0; i < timeData.size(); ++i) {
        out << QString::number(timeData[i], 'f', 6) << ","
            << QString::number(xData[i], 'f', 6) << ","
            << QString::number(yData[i], 'f', 6) << ","
            << QString::number(zData[i], 'f', 6) << "\n";
    }
    file.close();
    if (file.error() != QFile::NoError) {
        qWarning() << "Error during file write/close:" << filename << file.errorString();
        return false;
    }
    return true;
}

/**
 * @brief Collect模式使用，收集数据到对应标签名字的csv文件中.
 */
bool Widget::openAndPrepareCollectCsvFile(const QString& filePath, const QString& headerLine)
{
    if (m_collectCsvFile.isOpen() && m_collectCsvFile.fileName() != filePath) {
        closeCollectCsvFile();
    }
    if (m_collectCsvFile.isOpen() && m_collectCsvFile.fileName() == filePath) {
        if(m_collectCsvStream.device() != &m_collectCsvFile) m_collectCsvStream.setDevice(&m_collectCsvFile);
        return true;
    }
    m_collectCsvFile.setFileName(filePath);
    bool fileExisted = QFile::exists(filePath);
    if (!m_collectCsvFile.open(QIODevice::Append | QIODevice::Text)) {
        qWarning() << "Collect Mode: Cannot open file for appending:" << filePath << m_collectCsvFile.errorString();
        m_currentCollectCsvPath.clear();
        return false;
    }
    m_collectCsvStream.setDevice(&m_collectCsvFile);

    if ((!fileExisted || m_collectCsvFile.size() == 0) && !headerLine.isEmpty()) {
        m_collectCsvStream << headerLine;
        m_collectCsvStream.flush();
        qDebug() << "Collect Mode: Wrote header to new CSV:" << filePath;
    }
    qDebug() << "Collect Mode: Opened CSV for collection:" << filePath << "(Existed:" << fileExisted << ")";
    m_currentCollectCsvPath = filePath;
    return true;
}

/**
 * @brief 关闭程序中打开的csv文件，结束Collect或更换标签时使用
 */
void Widget::closeCollectCsvFile()
{
    if (m_collectCsvFile.isOpen()) {
        m_collectCsvStream.flush();
        m_collectCsvStream.setDevice(nullptr);
        m_collectCsvFile.close();
        qDebug() << "Collect Mode: Closed CSV file:" << m_currentCollectCsvPath;
    }
    m_currentCollectCsvPath.clear();
}

/**
 * @brief 更新状态栏信号槽
 */
void Widget::get_LocalTime()
{
    QDate currentDate = QDate::currentDate();
    QTime currentTime = QTime::currentTime();
    QString dateTimeString = "Time: " + currentDate.toString("yyyy-MM-dd") + " " + currentTime.toString("HH:mm:ss"); // Removed 'a' for 24h
    QString ModeString = NULL;
    ModeString = QString("Mode: %1").arg(Mode);
    if(tcpSocket != nullptr && tcpSocket->state() == QAbstractSocket::ConnectedState)
    {
        emit newStateToSend(Mode);
    }
    QString ipAddressString = "IP: N/A";
    QList<QNetworkInterface> allInterfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &interface : allInterfaces) {
        if (interface.flags().testFlag(QNetworkInterface::IsUp) &&
            !interface.flags().testFlag(QNetworkInterface::IsLoopBack)) {
            QList<QNetworkAddressEntry> allEntries = interface.addressEntries();
            for (const QNetworkAddressEntry &entry : allEntries) {
                if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                    ipAddressString = "IP: " + entry.ip().toString();
                    goto found_ip_time;
                }
            }
        }
    }

found_ip_time:;
    QString portString = (Port == 0) ? "Port: N/A" : ("Port: " + QString::number(Port));
    QString CurrentState = NULL;
    if(className_state != NULL)
    {
        if(className_state.contains("healthy") || className_state.contains("Healthy"))
        {
            CurrentState = QString("State: Healthy-%2%").arg(confidence_state);
        }else{
            CurrentState = QString("State: %1-%2%").arg(className_state).arg(confidence_state);
        }
    }else{
        CurrentState = QString("State: N/A");
    }

    QString State = dateTimeString + "  |  " + ModeString + " |  " + ipAddressString + "  |  " + portString + "  |  " + CurrentState;
    ui->StateLabel->setText(State);
    m_mfccDisplayWindow->setStateLabel(State);
}

/**
 * @brief TCP客户端连接信号槽
 */
void Widget::newConnection_SLOT()
{
    if (tcpServer->hasPendingConnections()) {

        // * 如果已经有一个客户端连接，先断开它（这里只支持一个客户端）
        if (tcpSocket && tcpSocket->isOpen()) {
            qDebug() << "An existing client was connected. Disconnecting it first.";
            tcpSocket->disconnectFromHost();
            tcpSocket->deleteLater(); // 标记旧的socket以便删除
        }

        tcpSocket = tcpServer->nextPendingConnection();

        if (tcpSocket) {
            // * 将新socket传递给TCP/IP副线程
            emit socketReady(tcpSocket);
            connect(tcpSocket, &QTcpSocket::disconnected, this, &Widget::onClientSocketDisconnected);

            // * 读取数据连接
            connect(tcpSocket, &QTcpSocket::readyRead, this, &Widget::readyRead_SLOT);

            // * 更新UI
            QString clientInfo = QString("New client connected: %1:%2").arg(tcpSocket->peerAddress().toString()).arg(tcpSocket->peerPort());
            qDebug() << clientInfo;
            setLED(ui->NetworkLabel, 2, 16); // 绿色: 表示有客户端连接

            // * 在系统日志中显示
            onClientStatusChanged(clientInfo);
        }
    }
}

/**
 * @brief TCP客户端断开连接信号槽
 */
void Widget::onClientSocketDisconnected()
{
    qDebug() << "Client has disconnected.";

    // * 通知工作线程清空它的socket指针
    emit clientHasDisconnected();

    // * 更新UI状态
    setLED(ui->NetworkLabel, 3, 16); // 黄色: 服务器运行，无客户端连接
    onClientStatusChanged("Client has disconnected.");

    // * 清理主线程中的socket资源
    QTimer::singleShot(3000, this, [=](){
        if (tcpSocket) {
            tcpSocket->deleteLater();
            tcpSocket = nullptr;     // 将指针设为null，防止悬挂指针
        }
    });
}

/**
 * @brief TCP读取数据信号槽
 */
void Widget::readyRead_SLOT()
{
    if(tcpSocket && tcpSocket->bytesAvailable() > 0){
        QByteArray reDataArray = tcpSocket->readAll();
        QString reData = QString::fromUtf8(reDataArray); // UTF-8编码
        qDebug() << "Received from client: " << reData;
        // * 处理从客户端接收到的数据
        if(ui->SysEdit){
            QDate cd = QDate::currentDate(); QTime ct = QTime::currentTime();
            QString dtp = QString("[%1 %2] ").arg(cd.toString("yyyy-MM-dd")).arg(ct.toString("HH:mm:ss"));
            ui->SysEdit->appendHtml(QString("%1<font color='purple'><b>[Network Rx]:</b> %2</font>").arg(dtp).arg(reData.toHtmlEscaped()));
            ui->SysEdit->ensureCursorVisible();
        }
    }
}

/**
 * @brief TCP连接状态改变信号槽
 */
void Widget::onClientStatusChanged(const QString& message)
{
    // * 在日志中打印信息
    if (ui->SysEdit) {
        QDate cd = QDate::currentDate();
        QTime ct = QTime::currentTime();
        QString dtp = QString("[%1 %2] ").arg(cd.toString("yyyy-MM-dd")).arg(ct.toString("HH:mm:ss"));
        ui->SysEdit->appendHtml(QString("%1<font color='blue'>网络状态:</font> %2").arg(dtp).arg(message.toHtmlEscaped()));
        ui->SysEdit->ensureCursorVisible();
    }
    qDebug() << "Network Status:" << message;
}

/**
 * @brief TCP发送数据信号槽
 */
void Widget::onDataSenderStatus(const QString& message)
{
    // * 在日志中打印信息
    if (ui->SysEdit) {
        QDate cd = QDate::currentDate();
        QTime ct = QTime::currentTime();
        QString dtp = QString("[%1 %2] ").arg(cd.toString("yyyy-MM-dd")).arg(ct.toString("HH:mm:ss"));
        ui->SysEdit->appendHtml(QString("%1<font color='purple'>网络发送:</font> %2").arg(dtp).arg(message.toHtmlEscaped()));
        ui->SysEdit->ensureCursorVisible();
    }
    qDebug() << "Network Status:" << message;
}

/**
 * @brief 开始收集数据按键槽
 */
void Widget::on_CollectStartButton_clicked()
{
    Mode = "Collect";
    // * 重置进度条
    finish = false;
    ui->CollectProgressBar->setEnabled(true);
    ui->CollectStopButton->setEnabled(true);
    ui->CollectStartButton->setEnabled(false);
    ui->CollectProgressBar->setValue(0);
    setLED(ui->DeviceStateLabel,0,16);
    m_collect_cnt = 0;
    ui->LabelBox->setEnabled(false);
    // * 清理 Monitor 模式可能遗漏的 CSV 文件
    if (!m_csvDataPath.isEmpty()) {
        QDir csvDir(m_csvDataPath);
        if (csvDir.exists()) {
            QStringList nameFilters;
            nameFilters << "data_*.csv"; // 只删除 data_ 开头的，避免删除 Collect 目录下的
            csvDir.setNameFilters(nameFilters);
            csvDir.setFilter(QDir::Files);
            QStringList csvFiles = csvDir.entryList();
            for (const QString &fileName : csvFiles) {
                QString filePath = csvDir.filePath(fileName);
                if (QFile::remove(filePath)) {
                    qDebug() << "Removed monitor CSV file:" << filePath;
                } else {
                    qWarning() << "Failed to remove monitor CSV file:" << filePath;
                }
            }
        }
    }
    // * 日志打印
    if(ui->SysEdit){
        QDate cd = QDate::currentDate(); QTime ct = QTime::currentTime();
        QString dtp = QString("[%1 %2] ").arg(cd.toString("yyyy-MM-dd")).arg(ct.toString("HH:mm:ss"));
        ui->SysEdit->appendHtml(QString("%1<font color='blue'><b>-- Data Collection Mode Started --</b></font>").arg(dtp));
        ui->SysEdit->ensureCursorVisible();
    }
}

/**
 * @brief 停止收集数据按键槽
 */
void Widget::on_CollectStopButton_clicked()
{
    Mode = "Monitor";
    // * 重置进度条
    finish = false;
    m_collect_cnt = 0;
    ui->CollectProgressBar->setValue(0);
    ui->CollectProgressBar->setEnabled(false);
    ui->LabelBox->setEnabled(true);
    ui->CollectStartButton->setEnabled(true);
    ui->CollectStopButton->setEnabled(false);
    closeCollectCsvFile(); // 确保收集文件已关闭并刷新
    if(ui->SysEdit){
        QDate cd = QDate::currentDate(); QTime ct = QTime::currentTime();
        QString dtp = QString("[%1 %2] ").arg(cd.toString("yyyy-MM-dd")).arg(ct.toString("HH:mm:ss"));
        ui->SysEdit->appendHtml(QString("%1<font color='blue'><b>-- Data Collection Mode Stopped. Switched to Monitor Mode --</b></font>").arg(dtp));
        ui->SysEdit->ensureCursorVisible();
    }
}
/**
 * @brief 清除当前标签数据按键槽
 */
void Widget::on_CollectCleanButton_clicked()
{
    if (!ui->LabelBox) {
        qWarning() << "Clean Button: LabelBox UI element is missing.";
        if (ui->SysEdit) {
            QDate cd = QDate::currentDate(); QTime ct = QTime::currentTime();
            QString dtp = QString("[%1 %2] ").arg(cd.toString("yyyy-MM-dd")).arg(ct.toString("HH:mm:ss"));
            ui->SysEdit->appendHtml(QString("%1<font color='red'><b>清理错误:</b> 标签选择框不存在.</font>").arg(dtp));
            ui->SysEdit->ensureCursorVisible();
        }
        return;
    }

    QString currentLabel = ui->LabelBox->currentText().trimmed();

    if (currentLabel.isEmpty()) {
        qWarning() << "Clean Button: No label selected in LabelBox.";
        if (ui->SysEdit) {
            QDate cd = QDate::currentDate(); QTime ct = QTime::currentTime();
            QString dtp = QString("[%1 %2] ").arg(cd.toString("yyyy-MM-dd")).arg(ct.toString("HH:mm:ss"));
            ui->SysEdit->appendHtml(QString("%1<font color='orange'><b>清理操作:</b> 请先选择一个标签.</font>").arg(dtp));
            ui->SysEdit->ensureCursorVisible();
        }
        return;
    }

    // * 构建基础收集目录
    QString collectSubPath = m_csvDataPath + "/Collect";
    QDir collectDir(collectSubPath);

    if (!collectDir.exists()) {
        qWarning() << "Clean Button: Collect directory does not exist:" << collectSubPath;
        if (ui->SysEdit) {
            QDate cd = QDate::currentDate(); QTime ct = QTime::currentTime();
            QString dtp = QString("[%1 %2] ").arg(cd.toString("yyyy-MM-dd")).arg(ct.toString("HH:mm:ss"));
            ui->SysEdit->appendHtml(QString("%1<font color='red'><b>清理错误:</b> 数据收集目录 '%2' 不存在.</font>")
                                        .arg(dtp).arg(collectSubPath.toHtmlEscaped()));
            ui->SysEdit->ensureCursorVisible();
        }
        return;
    }

    // * 清理标签名以匹配文件名 (与保存时的逻辑一致)
    QString cleanLabel = currentLabel;
    cleanLabel.remove(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_.-]")));

    if (cleanLabel.isEmpty()) {
        qWarning() << "Clean Button: Cleaned label is empty for original label:" << currentLabel;
        cleanLabel = "default_collection";
    }

    // * 构建完整的文件路径
    QString targetCsvFilename = collectSubPath + QString("/%1.csv").arg(cleanLabel);
    QFile fileToDelete(targetCsvFilename);

    QDate cd = QDate::currentDate(); QTime ct = QTime::currentTime();
    QString dtp = QString("[%1 %2] ").arg(cd.toString("yyyy-MM-dd")).arg(ct.toString("HH:mm:ss"));

    // * 检查文件是否存在并尝试删除
    if (fileToDelete.exists()) {
        qDebug() << "Clean Button: Attempting to delete file:" << targetCsvFilename;
        if (fileToDelete.remove()) {
            qInfo() << "Clean Button: Successfully deleted file:" << targetCsvFilename;
            if (ui->SysEdit) {
                ui->SysEdit->appendHtml(QString("%1<font color='green'><b>清理成功:</b> 文件 '%2' 已删除.</font>")
                                            .arg(dtp).arg(QFileInfo(targetCsvFilename).fileName().toHtmlEscaped())); // 只显示文件名
                ui->SysEdit->ensureCursorVisible();
            }
        } else {
            qWarning() << "Clean Button: Failed to delete file:" << targetCsvFilename << "Error:" << fileToDelete.errorString();
            if (ui->SysEdit) {
                ui->SysEdit->appendHtml(QString("%1<font color='red'><b>清理失败:</b> 无法删除文件 '%2'. 错误: %3</font>")
                                            .arg(dtp)
                                            .arg(QFileInfo(targetCsvFilename).fileName().toHtmlEscaped())
                                            .arg(fileToDelete.errorString().toHtmlEscaped()));
                ui->SysEdit->ensureCursorVisible();
            }
        }
    } else {
        qInfo() << "Clean Button: File to delete does not exist:" << targetCsvFilename;
        if (ui->SysEdit) {
            ui->SysEdit->appendHtml(QString("%1<font color='orange'><b>清理提示:</b> 文件 '%2' 不存在，无需删除.</font>")
                                        .arg(dtp).arg(QFileInfo(targetCsvFilename).fileName().toHtmlEscaped()));
            ui->SysEdit->ensureCursorVisible();
        }
    }
}

/**
 * @brief 开启历史回放按键槽
 */
void Widget::on_HistoryBackButton_clicked()
{
    QDate cd = QDate::currentDate(); QTime ct = QTime::currentTime();
    QString dtp = QString("[%1 %2] ").arg(cd.toString("yyyy-MM-dd")).arg(ct.toString("HH:mm:ss"));

    if (!ui->HistoryBox || ui->HistoryBox->count() == 0 || ui->HistoryBox->currentIndex() < 0) {
        qWarning() << "History Replay: No history item selected or HistoryBox is empty/invalid.";
        if (ui->SysEdit) {
            QDate cd = QDate::currentDate(); QTime ct = QTime::currentTime();
            QString dtp = QString("[%1 %2] ").arg(cd.toString("yyyy-MM-dd")).arg(ct.toString("HH:mm:ss"));
            ui->SysEdit->appendHtml(QString("%1<font color='orange'><b>历史回放:</b> 请先从列表中选择一个历史数据.</font>").arg(dtp));
            ui->SysEdit->ensureCursorVisible();
        }
        return;
    }

    QString selectedFileName = ui->HistoryBox->currentData().toString();
    if (selectedFileName.isEmpty() || ui->HistoryBox->itemText(ui->HistoryBox->currentIndex()) == "没有历史数据") {
        qWarning() << "History Replay: Invalid file name selected from HistoryBox.";
        if (ui->SysEdit) {
            QDate cd = QDate::currentDate(); QTime ct = QTime::currentTime();
            QString dtp = QString("[%1 %2] ").arg(cd.toString("yyyy-MM-dd")).arg(ct.toString("HH:mm:ss"));
            ui->SysEdit->appendHtml(QString("%1<font color='orange'><b>历史回放:</b> 无效的选择项.</font>").arg(dtp));
            ui->SysEdit->ensureCursorVisible();
        }
        return;
    }

    // * 切换模式
    Mode = "History";
    ui->MoniterButton->setEnabled(true);
    if (ui->SysEdit) {
        ui->SysEdit->appendHtml(QString("%1<font color='purple'><b>模式切换:</b> 进入历史回放模式, 准备分析文件 '%2'.</font>")
                                    .arg(dtp).arg(selectedFileName.toHtmlEscaped()));
        ui->SysEdit->ensureCursorVisible();
    }
    qInfo() << "Mode changed to History for file:" << selectedFileName;

    // * 清理 sensor_data_for_python 目录下的 data_*.csv 文件
    QString sensorDataDir = getSensorDataDir();
    QDir dirToClean(sensorDataDir);
    if (dirToClean.exists()) {
        dirToClean.setNameFilters(QStringList() << "data_*.csv");
        dirToClean.setFilter(QDir::Files);
        QStringList filesToDelete = dirToClean.entryList();
        for (const QString &file : filesToDelete) {
            if (dirToClean.remove(file)) {
                qDebug() << "History Replay: Removed old file from sensor_data_for_python:" << file;
            } else {
                qWarning() << "History Replay: Failed to remove old file:" << dirToClean.filePath(file)
                << "Error:" << QDir(dirToClean.filePath(file)).rmdir("."); //尝试获取更详细的错误
                if (ui->SysEdit) {
                    ui->SysEdit->appendHtml(QString("%1<font color='red'><b>历史回放警告:</b> 无法清理旧文件 '%2' 在工作目录中. 可能影响回放.</font>")
                                                .arg(dtp).arg(file.toHtmlEscaped()));
                    ui->SysEdit->ensureCursorVisible();
                }
                // 根据需求决定是否要因此停止回放
                // return;
            }
        }
    }

    // * 将选定的历史文件从 processed_csv 移动到 sensor_data_for_python
    QString sourceFilePath = getProcessedCsvDir() + "/" + selectedFileName;
    QString targetFilePath = sensorDataDir + "/" + selectedFileName; // 使用原文件名

    QFile sourceFileHandler(sourceFilePath); // 用于检查存在性和执行移动
    if (!sourceFileHandler.exists()) {
        qWarning() << "History Replay: Source file does not exist in processed_csv:" << sourceFilePath;
        if (ui->SysEdit) {
            ui->SysEdit->appendHtml(QString("%1<font color='red'><b>历史回放错误:</b> 源文件 '%2' 在历史记录中不存在.</font>")
                                        .arg(dtp).arg(selectedFileName.toHtmlEscaped()));
            ui->SysEdit->ensureCursorVisible();
        }
        return;
    }

    // * QFile::rename() 可以用于移动文件 (如果源和目标在同一个文件系统分区)
    // * 它会覆盖目标位置的同名文件（如果存在且可写）
    bool fileMoveSuccess = sourceFileHandler.rename(targetFilePath);

    // * 读取并显示历史数据到波形图
    if (fileMoveSuccess) {
        qInfo() << "History Replay: Moved" << sourceFilePath << "to" << targetFilePath;
        if (ui->SysEdit) {
            ui->SysEdit->appendHtml(QString("%1<font color='blue'><b>历史回放:</b> 文件 '%2' 已准备好供模型分析. 正在加载波形...</font>")
                                        .arg(dtp).arg(selectedFileName.toHtmlEscaped()));
            ui->SysEdit->ensureCursorVisible();
        }

        // ** 从新位置 (targetFilePath) 加载并显示CSV数据
        if (loadAndDisplayCsvData(targetFilePath)) {
            if (ui->SysEdit) {
                ui->SysEdit->appendHtml(QString("%1<font color='DarkGreen'><b>历史回放:</b> 文件 '%2' 波形已加载.</font>")
                                            .arg(dtp).arg(selectedFileName.toHtmlEscaped()));
                ui->SysEdit->ensureCursorVisible();
            }
        } else {
            if (ui->SysEdit) {
                ui->SysEdit->appendHtml(QString("%1<font color='orange'><b>历史回放警告:</b> 文件 '%2' 已移动, 但加载波形失败.</font>")
                                            .arg(dtp).arg(selectedFileName.toHtmlEscaped()));
                ui->SysEdit->ensureCursorVisible();
            }
        }
        // ** Python 脚本会检测到 targetFilePath 并处理，处理完后 Python 会将其移回 processed_csv
    } else {
        qWarning() << "History Replay: Failed to move file. Source:" << sourceFilePath << "Target:" << targetFilePath
                   << "Error:" << sourceFileHandler.errorString();
        if (ui->SysEdit) {
            ui->SysEdit->appendHtml(QString("%1<font color='red'><b>历史回放错误:</b> 无法移动文件 '%2' 到工作目录. 错误: %3</font>")
                                        .arg(dtp).arg(selectedFileName.toHtmlEscaped()).arg(sourceFileHandler.errorString().toHtmlEscaped()));
            ui->SysEdit->ensureCursorVisible();
        }
    }
}

/**
 * @brief 清除选中标签历史数据按键槽
 */
void Widget::on_HistoryCleanButton_clicked()
{
    if (!ui->HistoryBox) {
        qWarning() << "History Clean: HistoryBox UI element is missing.";
        return;
    }

    int currentIndex = ui->HistoryBox->currentIndex(); // 获取当前选中的索引

    // * 检查是否有有效选择
    if (currentIndex < 0 || ui->HistoryBox->count() == 0) {
        qWarning() << "History Clean: No item selected or HistoryBox is empty.";
        if (ui->SysEdit) {
            QDate cd = QDate::currentDate(); QTime ct = QTime::currentTime();
            QString dtp = QString("[%1 %2] ").arg(cd.toString("yyyy-MM-dd")).arg(ct.toString("HH:mm:ss"));
            ui->SysEdit->appendHtml(QString("%1<font color='orange'><b>历史清理:</b> 请先从列表中选择一个历史数据.</font>").arg(dtp));
            ui->SysEdit->ensureCursorVisible();
        }
        return;
    }

    // * 从 HistoryBox 的用户数据中获取完整文件名
    QString selectedFileName = ui->HistoryBox->itemData(currentIndex).toString();

    // * 检查获取的文件名是否有效（例如，是否是 "没有历史数据" 占位符对应的数据，该数据应该是空的）
    if (selectedFileName.isEmpty()) {
        QString currentItemText = ui->HistoryBox->itemText(currentIndex);
        if (currentItemText == "没有历史数据") {
            qInfo() << "History Clean: '没有历史数据' selected, nothing to delete.";
            if (ui->SysEdit) {
                QDate cd = QDate::currentDate(); QTime ct = QTime::currentTime();
                QString dtp = QString("[%1 %2] ").arg(cd.toString("yyyy-MM-dd")).arg(ct.toString("HH:mm:ss"));
                ui->SysEdit->appendHtml(QString("%1<font color='gray'><b>历史清理:</b> 当前选择为占位符, 无需操作.</font>").arg(dtp));
                ui->SysEdit->ensureCursorVisible();
            }
        } else {
            qWarning() << "History Clean: Selected item has empty file data, but text is not placeholder:" << currentItemText;
            if (ui->SysEdit) {
                QDate cd = QDate::currentDate(); QTime ct = QTime::currentTime();
                QString dtp = QString("[%1 %2] ").arg(cd.toString("yyyy-MM-dd")).arg(ct.toString("HH:mm:ss"));
                ui->SysEdit->appendHtml(QString("%1<font color='orange'><b>历史清理:</b> 无效的选择项数据.</font>").arg(dtp));
                ui->SysEdit->ensureCursorVisible();
            }
        }
        return;
    }

    QString filePathToDelete = getProcessedCsvDir() + "/" + selectedFileName;
    QFile fileToDelete(filePathToDelete);

    QDate cd = QDate::currentDate(); QTime ct = QTime::currentTime();
    QString dtp = QString("[%1 %2] ").arg(cd.toString("yyyy-MM-dd")).arg(ct.toString("HH:mm:ss"));

    if (fileToDelete.exists()) {

        if (fileToDelete.remove()) {
            qInfo() << "History Clean: Successfully deleted file:" << filePathToDelete;
            if (ui->SysEdit) {
                ui->SysEdit->appendHtml(QString("%1<font color='green'><b>历史清理:</b> 文件 '%2' 已从磁盘删除.</font>")
                                            .arg(dtp).arg(selectedFileName.toHtmlEscaped()));
                ui->SysEdit->ensureCursorVisible();
            }
            // ** 从 HistoryBox 中移除项
            ui->HistoryBox->removeItem(currentIndex);

        } else {
            // ** 文件存在但删除失败
            qWarning() << "History Clean: Failed to delete file:" << filePathToDelete << "Error:" << fileToDelete.errorString();
            if (ui->SysEdit) {
                ui->SysEdit->appendHtml(QString("%1<font color='red'><b>历史清理错误:</b> 无法删除文件 '%2'. 错误: %3</font>")
                                            .arg(dtp)
                                            .arg(selectedFileName.toHtmlEscaped())
                                            .arg(fileToDelete.errorString().toHtmlEscaped()));
                ui->SysEdit->ensureCursorVisible();
            }
            // ** 此时不应该从 HistoryBox 移除，因为文件还在磁盘上
        }
    } else {
            // **文件在磁盘上不存在
        qWarning() << "History Clean: File to delete does not exist on disk:" << filePathToDelete;
        if (ui->SysEdit) {
            ui->SysEdit->appendHtml(QString("%1<font color='orange'><b>历史清理提示:</b> 文件 '%2' 在磁盘上已不存在. 将从列表中移除.</font>")
                                        .arg(dtp).arg(selectedFileName.toHtmlEscaped()));
            ui->SysEdit->ensureCursorVisible();
        }
        // ** 文件在磁盘上不存在，但可能仍在列表中（例如，外部删除了文件），所以也从 HistoryBox 中移除。
        ui->HistoryBox->removeItem(currentIndex);
    }

    // * 统一处理 HistoryBox 在移除项后的状态
    if (ui->HistoryBox->count() == 0) {
        ui->HistoryBox->addItem("历史数据为空"); // 添加占位符
        ui->HistoryBox->setCurrentIndex(0);    // 选中占位符
        ui->HistoryBox->setEnabled(false);
        if(ui->HistoryBackButton)
        {
            if(Mode == "Monitor")
            {
               ui->HistoryBackButton->setEnabled(false);
            }
        }
        if(ui->HistoryCleanButton) ui->HistoryCleanButton->setEnabled(false);
        if(ui->HistoryCleanAllButton) ui->HistoryCleanAllButton->setEnabled(false);
    } else {
        // ** 如果列表不为空，确保按钮是启用的，并选中一个有效的项
        ui->HistoryBox->setEnabled(true);
        if(ui->HistoryBackButton) ui->HistoryBackButton->setEnabled(true);
        if(ui->HistoryCleanButton) ui->HistoryCleanButton->setEnabled(true);
        if(ui->HistoryCleanAllButton) ui->HistoryCleanAllButton->setEnabled(true);
        // ** 删除后总是指向最新的一次数据
        ui->HistoryBox->setCurrentIndex(0);
    }
}

/**
 * @brief 清空历史数据按键槽
 */
void Widget::on_HistoryCleanAllButton_clicked()
{
    QDate cd = QDate::currentDate(); QTime ct = QTime::currentTime();
    QString dtp = QString("[%1 %2] ").arg(cd.toString("yyyy-MM-dd")).arg(ct.toString("HH:mm:ss"));

    // * 添加确认对话框
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "确认清除所有历史记录",
                                  "确定要永久删除所有已处理的历史数据文件吗？\n此操作无法撤销！",
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No); // 默认选中 "No"

    if (reply == QMessageBox::No) {
        if (ui->SysEdit) {
            ui->SysEdit->appendHtml(QString("%1<font color='gray'><b>清除所有历史:</b> 用户取消操作.</font>").arg(dtp));
            ui->SysEdit->ensureCursorVisible();
        }
        return;
    }

    // * 获取 processed_csv 目录路径
    QString processedPath = getProcessedCsvDir();
    QDir processedDir(processedPath);

    int deletedFileCount = 0;
    int failedToDeleteCount = 0;
    QStringList failedFiles;

    if (processedDir.exists()) {
        processedDir.setNameFilters(QStringList() << "data_*.csv"); // 目标文件类型
        processedDir.setFilter(QDir::Files | QDir::NoDotAndDotDot); // 只查找文件

        QFileInfoList fileList = processedDir.entryInfoList();

        if (fileList.isEmpty()) {
            if (ui->SysEdit) {
                ui->SysEdit->appendHtml(QString("%1<font color='orange'><b>清除所有历史:</b> 没有找到可删除的历史文件.</font>").arg(dtp));
                ui->SysEdit->ensureCursorVisible();
            }
        } else {
            for (const QFileInfo &fileInfo : fileList) {
                QString filePathToDelete = fileInfo.absoluteFilePath();
                QFile file(filePathToDelete);
                if (file.remove()) {
                    deletedFileCount++;
                    qInfo() << "Clean All History: Deleted" << filePathToDelete;
                } else {
                    failedToDeleteCount++;
                    failedFiles.append(fileInfo.fileName());
                    qWarning() << "Clean All History: Failed to delete" << filePathToDelete << "Error:" << file.errorString();
                }
            }
        }
    } else {
        qWarning() << "Clean All History: Processed CSV directory does not exist:" << processedPath;
        if (ui->SysEdit) {
            ui->SysEdit->appendHtml(QString("%1<font color='red'><b>清除所有历史错误:</b> 历史数据目录不存在.</font>").arg(dtp));
            ui->SysEdit->ensureCursorVisible();
        }
    }

    // * 清空 ComboBox 并设置占位符
    if (ui->HistoryBox) {
        ui->HistoryBox->clear();
        ui->HistoryBox->addItem("历史数据为空"); // 或者 "没有历史数据"
        ui->HistoryBox->setCurrentIndex(0);
        ui->HistoryBox->setEnabled(false); // 因为没有可选数据了
    }

    // * 禁用相关按钮
    if (ui->HistoryBackButton) {
        ui->HistoryBackButton->setEnabled(false);
    }
    if (ui->HistoryCleanButton) {
        ui->HistoryCleanButton->setEnabled(false);
    }

    ui->HistoryCleanAllButton->setEnabled(false);
    // * 在 SysEdit 中给出反馈
    if (ui->SysEdit) {
        if (deletedFileCount > 0 && failedToDeleteCount == 0) {
            ui->SysEdit->appendHtml(QString("%1<font color='green'><b>清除所有历史成功:</b> 共删除了 %2 个历史文件.</font>")
                                        .arg(dtp).arg(deletedFileCount));
        } else if (deletedFileCount > 0 && failedToDeleteCount > 0) {
            ui->SysEdit->appendHtml(QString("%1<font color='orange'><b>清除所有历史部分成功:</b> 删除了 %2 个文件, %3 个文件删除失败: %4.</font>")
                                        .arg(dtp).arg(deletedFileCount).arg(failedToDeleteCount).arg(failedFiles.join(", ").toHtmlEscaped()));
        } else if (deletedFileCount == 0 && failedToDeleteCount > 0) {
            ui->SysEdit->appendHtml(QString("%1<font color='red'><b>清除所有历史失败:</b> %2 个文件删除失败: %3.</font>")
                                        .arg(dtp).arg(failedToDeleteCount).arg(failedFiles.join(", ").toHtmlEscaped()));
        } else if (deletedFileCount == 0 && failedToDeleteCount == 0 && !processedDir.exists()) {
            // 这个情况已在上面处理目录不存在时给出错误信息
        } else if (deletedFileCount == 0 && failedToDeleteCount == 0 && processedDir.exists()){
            // 这个情况是目录存在但里面没有data_*.csv文件，已经在上面处理"没有找到可删除的历史文件"
        }
        if(ui->SysEdit->toPlainText().endsWith("操作.\n")) {
            // ** 检查是否只有取消操作的消息
            ui->SysEdit->appendHtml(QString("%1<font color='gray'><b>清除所有历史:</b> 操作完成.</font>").arg(dtp));
        }
        ui->SysEdit->ensureCursorVisible();
    }
    qInfo() << "Clean All History: Finished. Deleted:" << deletedFileCount << "Failed:" << failedToDeleteCount;
}

/**
 * @brief 获取屏幕分辨率信息函数
 */
void Widget::checkScreenResolution()
{
    // * 获取主屏幕对象
    QScreen *primaryScreen = QGuiApplication::primaryScreen();

    if (primaryScreen) {
        // * 获取屏幕的几何尺寸 (分辨率)
        QRect screenGeometry = primaryScreen->geometry();
        int width = screenGeometry.width();
        int height = screenGeometry.height();

        qDebug() << "主屏幕分辨率 (geometry):" << width << "x" << height;

        // * 获取可用的桌面区域尺寸 (除去任务栏等)
        QRect availableGeometry = primaryScreen->availableGeometry();
        int availableWidth = availableGeometry.width();
        int availableHeight = availableGeometry.height();

        qDebug() << "可用桌面区域 (availableGeometry):" << availableWidth << "x" << availableHeight;

        // * 获取物理尺寸和DPI (可选，用于高级适配)
        QSizeF physicalSize = primaryScreen->physicalSize(); // 毫米为单位
        qreal dpi = primaryScreen->logicalDotsPerInch();

        qDebug() << "物理尺寸:" << physicalSize.width() << "mm x" << physicalSize.height() << "mm";
        qDebug() << "逻辑DPI:" << dpi;
    } else {
        qWarning() << "无法获取主屏幕信息！";
    }
}

/**
 * @brief 实时监测按键槽
 */
void Widget::on_MoniterButton_clicked()
{
    QDate cd = QDate::currentDate(); QTime ct = QTime::currentTime();
    QString dtp = QString("[%1 %2] ").arg(cd.toString("yyyy-MM-dd")).arg(ct.toString("HH:mm:ss"));
    if(Mode == "History")
    {
        Mode = "Monitor";
        ui->MoniterButton->setEnabled(false);
        if (ui->SysEdit) {
            ui->SysEdit->appendHtml(QString("%1<font color='purple'><b>模式切换:</b> 进入实时监测模式.</font>")
                                        .arg(dtp));
            ui->SysEdit->ensureCursorVisible();
        }
    }
}

/**
 * @brief 对一个数据向量应用滑动平均滤波器。
 * @param rawData 原始数据向量。
 * @param windowSize 滤波窗口的大小，必须是正奇数。
 * @return 平滑滤波后的新数据向量。
 */
QVector<double> Widget::applyMovingAverageFilter(const QVector<double>& rawData, int windowSize)
{
    if (rawData.isEmpty() || windowSize <= 0) {
        return rawData; // 如果数据为空或窗口大小无效，返回原始数据
    }

    // * 确保窗口大小是奇数，这样中心点才明确
    if (windowSize % 2 == 0) {
        windowSize++;
    }

    QVector<double> filteredData;
    filteredData.reserve(rawData.size()); // 预分配内存以提高效率

    int halfWindow = windowSize / 2;

    for (int i = 0; i < rawData.size(); ++i) {
        double sum = 0.0;
        int count = 0;
        // ** 在当前点的周围取一个窗口
        for (int j = -halfWindow; j <= halfWindow; ++j) {
            int index = i + j;
            // ** 确保窗口索引在有效范围内
            if (index >= 0 && index < rawData.size()) {
                sum += rawData[index];
                count++;
            }
        }
        // ** 计算平均值并添加到结果中
        if (count > 0) {
            filteredData.append(sum / count);
        } else {
            filteredData.append(rawData[i]);
        }
    }
    return filteredData;
}

/**
 * @brief 关闭蜂鸣器按键槽
 */
void Widget::on_beepOffButton_clicked()
{
    beepctl->stopAlert();
    if (ui->SysEdit) {
        QDate cd = QDate::currentDate(); QTime ct = QTime::currentTime();
        QString dtp = QString("[%1 %2] ").arg(cd.toString("yyyy-MM-dd")).arg(ct.toString("HH:mm:ss"));
        ui->SysEdit->appendHtml(QString("%1<font color='orange'>Alert has been closed.").arg(dtp));
        ui->SysEdit->ensureCursorVisible();
    }
}


#include "beepctl.h"
#include <QDebug>

// 平台相关的C/C++头文件
#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h> // for perror
#endif

BeepCtl::BeepCtl(QObject *parent)
    : QObject(parent),
    m_fd(-1),
    m_isBeepOn(false),
    m_currentState(State_Idle)
{
    // 初始化定时器并连接信号槽
    m_alertTimer = new QTimer(this);
    connect(m_alertTimer, &QTimer::timeout, this, &BeepCtl::onTimerTimeout);

#ifdef Q_OS_LINUX
    // 在构造时自动尝试打开设备
    if (!openDevice()) {
        qWarning() << "BeepCtl: Failed to open device on construction.";
    }
#else
    qWarning() << "BeepCtl: Running on non-Linux system. Beep functionality is disabled.";
#endif
}

BeepCtl::~BeepCtl()
{
    // 在析构时确保设备被关闭
#ifdef Q_OS_LINUX
    closeDevice();
#endif
}

// --- 公共接口的实现 ---

void BeepCtl::alertLightDamage()
{
    qDebug() << "Alert: Light Damage (Slow Pulse, 1 Hz)";
    startPulse(1000); // 1秒1个周期
}

void BeepCtl::alertMediumDamage()
{
    qDebug() << "Alert: Medium Damage (Medium Pulse, 2.5 Hz)";
    startPulse(500); // 0.4秒1个周期
}

void BeepCtl::alertSevereDamage()
{
    qDebug() << "Alert: Severe Damage (Fast Pulse, 5 Hz)";
    startPulse(100); // 0.2秒1个周期
}

void BeepCtl::notificationSuccess()
{
    stopAlert(); // 确保停止其他声音
    qDebug() << "Notification: Success ('short-long' beep)";

    m_currentState = State_Success_Beep1;
    onTimerTimeout(); // 立即启动成功提示音序列
}

void BeepCtl::stopAlert()
{
    qDebug() << "Alert: Stopping all sounds.";
    m_alertTimer->stop();
    if (m_isBeepOn) {
#ifdef Q_OS_LINUX
        sendCommand(BEEP_OFF);
#endif
        m_isBeepOn = false;
    }
    m_currentState = State_Idle;
}


// --- 私有核心逻辑的实现 ---

void BeepCtl::startPulse(int cycle_duration_ms)
{
    stopAlert();
    m_currentState = State_Pulse;

    if (cycle_duration_ms > 0) {
        m_alertTimer->start(cycle_duration_ms / 2);
    } else {
        qWarning() << "Pulse duration must be positive.";
        return;
    }

    if (!m_isBeepOn) {
#ifdef Q_OS_LINUX
        sendCommand(BEEP_ON);
#endif
        m_isBeepOn = true;
    }
}

void BeepCtl::onTimerTimeout()
{
    switch (m_currentState) {
    case State_Pulse:
        // 持续脉冲警报的逻辑
        if (m_isBeepOn) {
#ifdef Q_OS_LINUX
            sendCommand(BEEP_OFF);
#endif
            m_isBeepOn = false;
        } else {
#ifdef Q_OS_LINUX
            sendCommand(BEEP_ON);
#endif
            m_isBeepOn = true;
        }
        break;

    case State_Success_Beep1:
        // 成功提示音的第一声：“嘀” (短音)
#ifdef Q_OS_LINUX
        sendCommand(BEEP_ON);
#endif
        m_isBeepOn = true;
        m_alertTimer->start(80); // 响80ms
        m_currentState = State_Success_Beep2;
        break;

    case State_Success_Beep2:
        // 成功提示音的第二声：“嗒” (长音)
#ifdef Q_OS_LINUX
        sendCommand(BEEP_OFF); // 先关掉，制造短暂间隔
#endif
        m_isBeepOn = false;

        // 使用 singleShot 安排后续动作，不再依赖主定时器
        QTimer::singleShot(50, this, [this]() {
#ifdef Q_OS_LINUX
            sendCommand(BEEP_ON); // 50ms后，打开蜂鸣器
#endif
            m_isBeepOn = true;
            // 再安排一个定时器，在200ms后彻底关闭
            QTimer::singleShot(200, this, &BeepCtl::stopAlert);
        });

        m_currentState = State_Idle; // 主状态机恢复空闲
        m_alertTimer->stop();      // 停止主定时器
        break;

    case State_Idle:
    default:
        // 意外情况，确保一切都停止
        stopAlert();
        break;
    }
}


// --- 底层设备操作的实现 (保持不变) ---

bool BeepCtl::openDevice()
{
#ifdef Q_OS_LINUX
    QByteArray pathBytes = m_devicePath.toLocal8Bit();
    m_fd = ::open(pathBytes.constData(), O_WRONLY);
    if (m_fd < 0) {
        perror("BeepCtl: Failed to open device");
        return false;
    }
    qDebug() << "BeepCtl: Device" << m_devicePath << "opened successfully.";
    return true;
#else
    return false;
#endif
}

void BeepCtl::closeDevice()
{
#ifdef Q_OS_LINUX
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
#endif
}

bool BeepCtl::sendCommand(int cmd)
{
#ifdef Q_OS_LINUX
    if (m_fd < 0) {
        qWarning() << "BeepCtl: Cannot send command, device not open.";
        return false;
    }
    if (::ioctl(m_fd, cmd, 0) < 0) {
        perror("BeepCtl: ioctl command failed");
        return false;
    }
    return true;
#else
    Q_UNUSED(cmd);
    return true;
#endif
}



#ifndef BEEPCTL_H
#define BEEPCTL_H

#include <QObject>
#include <QString>
#include <QTimer>

// 平台相关的头文件和宏定义
#ifdef Q_OS_LINUX
#include <sys/ioctl.h>
#define BEEP_MAGIC   'B'
#define BEEP_OFF     _IO(BEEP_MAGIC, 0)
#define BEEP_ON      _IO(BEEP_MAGIC, 1)
#endif

class BeepCtl : public QObject
{
    Q_OBJECT

public:
    // 构造函数和析构函数
    explicit BeepCtl(QObject *parent = nullptr);
    ~BeepCtl();

    // --- 公共警报/通知接口 ---
    void alertLightDamage();        // 慢速脉冲 (警报)
    void alertMediumDamage();       // 中速脉冲 (警报)
    void alertSevereDamage();       // 快速脉冲 (警报)
    void notificationSuccess();     // “嘀-嗒” (成功提示音)
    void stopAlert();               // 停止所有警报和提示音

private:
    // --- 内部状态定义 ---
    enum BeepState {
        State_Idle,          // 空闲状态
        State_Pulse,         // 持续脉冲警报状态
        State_Success_Beep1, // 成功提示音的第一声
        State_Success_Beep2  // 成功提示音的第二声
    };

    // --- 私有辅助函数 ---
    bool openDevice();
    void closeDevice();
    bool sendCommand(int cmd);
    void startPulse(int cycle_duration_ms); // 启动一个指定周期的脉冲
private slots:
    // --- 私有槽函数 ---
    // 统一的状态机处理器
    void onTimerTimeout();

private:
    // --- 私有成员变量 ---
    int m_fd;                  // 设备文件描述符
    const QString m_devicePath = "/dev/crazy_beep";

    QTimer *m_alertTimer;      // 用于控制声音节奏的定时器
    bool m_isBeepOn;           // 追踪蜂鸣器当前的物理开关状态
    BeepState m_currentState;  // 当前状态机所处的状态
};

#endif // BEEPCTL_H


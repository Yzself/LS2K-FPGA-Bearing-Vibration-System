#ifndef INHIBIT_MANAGER_H
#define INHIBIT_MANAGER_H

#include <QObject>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusMessage> // 确保包含
#include <QVariant>     // 确保包含
#include <QDebug>

class InhibitManager : public QObject
{
    Q_OBJECT
public:
    explicit InhibitManager(QObject *parent = nullptr) : QObject(parent), m_cookie(0) {}
    ~InhibitManager() {
        unInhibit();
    }

public slots:
    void inhibit() {
        if (m_cookie > 0) {
            qDebug() << "Already inhibited.";
            return;
        }

        QDBusInterface interface("org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver",
                                 "org.freedesktop.ScreenSaver");

        if (!interface.isValid()) {
            qWarning() << "D-Bus interface for ScreenSaver not valid!";
            return;
        }

        // QDBusReply<uint> 这种用法在Qt4中也存在，所以这部分是安全的
        QDBusReply<uint> reply = interface.call("Inhibit", "LoongQt", "Preventing system sleep");

        if (reply.isValid()) {
            m_cookie = reply.value();
            qDebug() << "Successfully inhibited sleep. Cookie:" << m_cookie;
        } else {
            qWarning() << "Failed to inhibit sleep:" << reply.error().message();
        }
    }

    // ====================================================================
    //  ↓ 这里是关键的修改，使用了与Qt4完全兼容的方式 ↓
    // ====================================================================
    void unInhibit() {
        if (m_cookie == 0) {
            return;
        }

        QDBusInterface interface("org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver",
                                 "org.freedesktop.ScreenSaver");

        if (!interface.isValid()) {
            qWarning() << "D-Bus interface for ScreenSaver not valid!";
            return;
        }

        // 使用与Qt4兼容的方式来处理无返回值的调用
        QDBusMessage reply = interface.call("UnInhibit", QVariant::fromValue(m_cookie));

        // 通过检查返回消息的类型来判断是否出错
        if (reply.type() == QDBusMessage::ErrorMessage) {
            qWarning() << "Failed to uninhibit sleep:" << reply.errorMessage();
        } else {
            qDebug() << "Successfully uninhibited sleep.";
            m_cookie = 0;
        }
    }
    // ====================================================================

private:
    uint m_cookie;
};

#endif // INHIBIT_MANAGER_H

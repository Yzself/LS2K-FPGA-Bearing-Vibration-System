QT       += core gui network charts dbus

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets printsupport

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    beepctl.cpp \
    datareader.cpp \
    datasender.cpp \
    main.cpp \
    qcustomplot.cpp \
    widget.cpp \
    widget_2.cpp

HEADERS += \
    beepctl.h \
    datareader.h \
    datasender.h \
    inhibit_manager.h \
    qcustomplot.h \
    widget.h \
    widget_2.h

FORMS += \
    widget.ui \
    widget_2.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

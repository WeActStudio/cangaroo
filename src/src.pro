lessThan(QT_MAJOR_VERSION, 5): error("requires Qt 5")

QT += core gui
QT += widgets
QT += xml
QT += charts
QT += serialport

TARGET = cangaroo
TEMPLATE = app
CONFIG += warn_on
CONFIG += link_pkgconfig

TRANSLATIONS = i18n_zh_cn.ts
RC_ICONS = cangaroo.ico

VERSION = 0.2.4.3

QMAKE_TARGET_COMPANY = "WeAct Studio"
QMAKE_TARGET_PRODUCT = "cangaroo (WeActStudio Modified)"
QMAKE_TARGET_DESCRIPTION = "CAN bus analysis tool"
QMAKE_TARGET_COPYRIGHT = "Original Copyright (C) HubertD | Modified (C) 2026 WeActStudio, GPLv2"
DEFINES += APP_PRODUCT_NAME=\"\\\"$$QMAKE_TARGET_PRODUCT\\\"\"
win32 {
    QMAKE_TARGET_PRODUCTVERSION = $$VERSION
    QMAKE_TARGET_FILEVERSION = $$VERSION
}

DESTDIR = ../bin
MOC_DIR = ../build/moc
RCC_DIR = ../build/rcc
UI_DIR = ../build/ui
unix:OBJECTS_DIR = ../build/o/unix
win32:OBJECTS_DIR = ../build/o/win32
macx:OBJECTS_DIR = ../build/o/mac


SOURCES += main.cpp\
    mainwindow.cpp \

HEADERS  += mainwindow.h \

FORMS    += mainwindow.ui

RESOURCES = cangaroo.qrc

include($$PWD/core/core.pri)
include($$PWD/driver/driver.pri)
include($$PWD/parser/dbc/dbc.pri)
include($$PWD/window/TraceWindow/TraceWindow.pri)
include($$PWD/window/SetupDialog/SetupDialog.pri)
include($$PWD/window/LogWindow/LogWindow.pri)
include($$PWD/window/GraphWindow/GraphWindow.pri)
include($$PWD/window/CanStatusWindow/CanStatusWindow.pri)
include($$PWD/window/RawTxWindow/RawTxWindow.pri)


unix:PKGCONFIG += libnl-3.0
unix:PKGCONFIG += libnl-route-3.0
unix:include($$PWD/driver/SocketCanDriver/SocketCanDriver.pri)

include($$PWD/driver/CANBlastDriver/CANBlastDriver.pri)
include($$PWD/driver/SLCANDriver/SLCANDriver.pri)

win32:include($$PWD/driver/CandleApiDriver/CandleApiDriver.pri)

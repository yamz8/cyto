QT += core gui qml quick quickcontrols2 testlib dbus
CONFIG += c++20 testcase
TARGET = ui_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += \
    testdata.h \
    ../src/analysis.h \
    ../src/backend.h \
    ../src/fcs.h \
    ../src/filepicker.h \
    ../src/gating.h \
    ../src/plotitem.h \
    ../src/theme.h \
    ../src/translator.h

SOURCES += \
    ui_tests.cpp \
    ../src/analysis.cpp \
    ../src/backend.cpp \
    ../src/fcs.cpp \
    ../src/gating.cpp \
    ../src/plotitem.cpp \
    ../src/theme.cpp \
    ../src/translator.cpp

RESOURCES += ../src/resources.qrc

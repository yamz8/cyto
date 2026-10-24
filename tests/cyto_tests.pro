QT += core gui qml quick quickcontrols2 testlib dbus
CONFIG += c++20 testcase
TARGET = cyto_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += \
    testdata.h \
    ../src/analysis.h \
    ../src/backend.h \
    ../src/fcs.h \
    ../src/filepicker.h \
    ../src/gating.h \
    ../src/theme.h \
    ../src/translator.h

SOURCES += \
    cyto_tests.cpp \
    ../src/analysis.cpp \
    ../src/backend.cpp \
    ../src/fcs.cpp \
    ../src/gating.cpp \
    ../src/theme.cpp \
    ../src/translator.cpp

RESOURCES += ../src/resources.qrc

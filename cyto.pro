QT += core gui qml quick quickcontrols2 dbus

CONFIG += c++20 release
TARGET = cyto
TEMPLATE = app

HEADERS += \
    src/analysis.h \
    src/backend.h \
    src/cli.h \
    src/fcs.h \
    src/filepicker.h \
    src/gating.h \
    src/plotitem.h \
    src/portalfilepicker.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/analysis.cpp \
    src/backend.cpp \
    src/cli.cpp \
    src/fcs.cpp \
    src/gating.cpp \
    src/plotitem.cpp \
    src/portalfilepicker.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc

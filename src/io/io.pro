# 数据层（src/io/）—— WAV 读取、实时链路逐帧分析
#
# 依赖方向：src/io → src/core；不依赖 UI，也不依赖 src/app。
# 本目录目前**不含 Qt 依赖**（只用标准库），但按分层它属于数据/IO 层，
# 因此不放进 src/core——core 必须保持"纯数值算法"的可移植边界。
# 为把这条边界也变成构建期约束，这里同样不写 QT += core。

TEMPLATE = lib
# 固定 Release：Qt 官方安装器只装 release 库，且 debug_and_release 会让两种配置的
# 静态库输出到同一个 lib/ 目录互相覆盖（见根 .pro 的说明）。
CONFIG -= debug_and_release debug
CONFIG += release

CONFIG += staticlib
CONFIG -= qt

TARGET = pitch-io

QMAKE_CXXFLAGS += -std=c++20 -Wall -Wextra -Wpedantic -Wshadow -Wconversion

SOURCES += \
    realtime-runner.cpp \
    wav-reader.cpp

HEADERS += \
    realtime-runner.h \
    wav-reader.h

INCLUDEPATH += $$PWD/../core

LIBS += -L$$PWD/../../lib -lpitch-core -lm

DESTDIR = $$PWD/../../lib
OBJECTS_DIR = $$OUT_PWD/obj

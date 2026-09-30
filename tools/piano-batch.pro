# 钢琴 88 键素材批量分析（A1/A2 验收的执行体）
#
# 与 cross-check 一样：纯标准库实现，不依赖 Qt。
#
# 用法（本仓库根目录）：
#   bin\piano-batch.exe --dir "<素材目录>\钢琴88键独立音频文件" --csv reports\piano-88.csv --md reports\piano-88.md
#
# 判定口径（与上游一致）：众数音名正确 **且** |中位偏差| < 50 音分。

TEMPLATE = app
# 固定 Release：Qt 官方安装器只装 release 库，且 debug_and_release 会让两种配置的
# 静态库输出到同一个 lib/ 目录互相覆盖（见根 .pro 的说明）。
CONFIG -= debug_and_release debug
CONFIG += release

CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

TARGET = piano-batch

QMAKE_CXXFLAGS += -std=c++20 -Wall -Wextra -Wpedantic -Wshadow -Wconversion

SOURCES += \
    piano-batch.cpp \
    piano-batch-main.cpp

HEADERS += \
    piano-batch.h

# 自身目录必须在 INCLUDEPATH 里：头文件与本工程同目录（qmake 不会自动加）
INCLUDEPATH += $$PWD $$PWD/../src/core $$PWD/../src/io

# 路径基准：本文件位于 tools/，故 $$PWD/.. 才是仓库根（一级）
LIBS += -L$$PWD/../lib -lpitch-io -lpitch-core -lm

# 用宽字符命令行取参数（中文路径必需）：需要 shell32
win32:LIBS += -lshell32

DESTDIR = $$PWD/../bin
OBJECTS_DIR = $$OUT_PWD/obj

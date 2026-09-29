# 开发工具：跨语言一致性对拍（B 组验收的执行体）
#
# 它**不属于任何产品分层**：是验证工具，链接 core 与 io，不依赖 Qt。
# 因此这里不写 QT += core，避免它被误当作 Qt 代码。

TEMPLATE = app
# 固定 Release：Qt 官方安装器只装 release 库，且 debug_and_release 会让两种配置的
# 静态库输出到同一个 lib/ 目录互相覆盖（见根 .pro 的说明）。
CONFIG -= debug_and_release debug
CONFIG += release

CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

TARGET = cross-check

QMAKE_CXXFLAGS += -std=c++20 -Wall -Wextra -Wpedantic -Wshadow -Wconversion

SOURCES += \
    cross-check.cpp \
    cross-check-main.cpp

HEADERS += \
    cross-check.h

# 自身目录必须在 INCLUDEPATH 里：头文件与本工程同目录（qmake 不会自动加）
INCLUDEPATH += $$PWD $$PWD/../src/core $$PWD/../src/io

# 路径基准：本文件位于 tools/，故 $$PWD/.. 才是仓库根（**一级**）。
# 写多一级会解析到仓库的父目录 D:\dev_project\lib，链接时直接 `cannot find -lpitch-io`（实测踩过）。
LIBS += -L$$PWD/../lib -lpitch-io -lpitch-core -lm

DESTDIR = $$PWD/../bin
OBJECTS_DIR = $$OUT_PWD/obj

# 真值目录（tests/data）不由构建生成：它需要 Node 与上游项目（见 gen-test-fixtures.mjs）。
# 用法：node tools/gen-test-fixtures.mjs
#       bin\cross-check.exe --data tests\data --mode=analysis

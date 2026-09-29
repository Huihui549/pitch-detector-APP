# 核心算法层单测（**纯标准库，不依赖 Qt**）
#
# 为什么不用 Qt Test：
#   src/core 与 src/io 都不依赖 Qt（分层铁律）。若单测引入 Qt Test，就会出现
#   "core 本身能脱离 Qt 编译，但验证 core 需要 Qt"的矛盾——一旦 Qt 缺席，算法就完全无法验证。
#   故单测用纯标准库实现，任何 C++17 以上编译器都能跑。

TEMPLATE = app
# 固定 Release：Qt 官方安装器只装 release 库，且 debug_and_release 会让两种配置的
# 静态库输出到同一个 lib/ 目录互相覆盖（见根 .pro 的说明）。
CONFIG -= debug_and_release debug
CONFIG += release

CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

TARGET = core-tests

QMAKE_CXXFLAGS += -std=c++20 -Wall -Wextra -Wpedantic -Wshadow -Wconversion

SOURCES += \
    core/core-tests.cpp \
    support/json-reader.cpp

HEADERS += \
    support/json-reader.h

INCLUDEPATH += $$PWD/support $$PWD/../src/core $$PWD/../src/io

# 路径基准：本文件位于 tests/，故 $$PWD/.. 才是仓库根（一级）
LIBS += -L$$PWD/../lib -lpitch-io -lpitch-core -lm

DESTDIR = $$PWD/../bin
OBJECTS_DIR = $$OUT_PWD/obj

# 真值目录固定为源码树下的 tests/data（由 `node tools/gen-test-fixtures.mjs` 生成）。
# 真值缺失时 core-tests 会 FAIL 而不是跳过——这是刻意的（坑 A9：素材缺失不得静默通过）。
# 用法：bin\core-tests.exe --data tests\data

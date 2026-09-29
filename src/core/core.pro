# 算法层（src/core/）—— 纯 C++20，零 Qt 依赖
#
# 按 design/architecture.md 第二节的铁律：本目录不得包含任何 Qt 头文件，也不得依赖其它层。
# 因此它可以脱离 Qt 独立编译、独立单测（ADR-0007 的前提）。
#
# 注意：这里刻意**不写 QT += core**。若哪天有人在 core 里误加了 Qt 依赖，
# 这个工程会立刻编译失败——这是"分层铁律"在构建期的第一道闸门
# （第二道是 tools/check-layering.ps1 的文本检查）。
#
# 编码要求（坑 A20）：本文件必须存为 **UTF-8 无 BOM**。
# 带 BOM 时 qmake 会静默忽略文件开头的一批赋值——实测表现为"QMAKE_CXXFLAGS 写了但没进 Makefile"，
# 报出来的却是 std::span 不认识（缺 -std=c++20），极难定位。

TEMPLATE = lib
# 固定 Release：Qt 官方安装器只装 release 库，且 debug_and_release 会让两种配置的
# 静态库输出到同一个 lib/ 目录互相覆盖（见根 .pro 的说明）。
CONFIG -= debug_and_release debug
CONFIG += release

CONFIG += staticlib
CONFIG -= qt

TARGET = pitch-core

# 严格告警门槛：本项目要求"编译 0 warning"（AGENTS.md 硬约定）。
# 算法层是数值敏感代码，隐式转换/遮蔽这类告警往往指向真实缺陷。
#
# C++20（ADR-0008：用 std::span）与告警门槛写同一条：
#   · 必须逐个子工程写——qmake 的 subdirs 模板**不会**把顶层的 QMAKE_*FLAGS 传给子工程（实测）
#   · 只有 QMAKE_CXXFLAGS 这一条会进最终 CXXFLAGS；QMAKE_CXXFLAGS_RELEASE / _DEBUG 会被 mkspec 覆盖
QMAKE_CXXFLAGS += -std=c++20 -Wall -Wextra -Wpedantic -Wshadow -Wconversion

SOURCES += \
    analysis-runner.cpp \
    note-converter.cpp \
    octave-unifier.cpp \
    pitch-engine.cpp

HEADERS += \
    analysis-runner.h \
    note-converter.h \
    octave-unifier.h \
    pitch-engine.h \
    pitch-types.h

# 数值计算需要数学库（MinGW 下需显式链接）
LIBS += -lm

# 所有本地静态库统一输出到仓库根的 lib/，便于 app / tools / tests 用同一条相对路径链接
DESTDIR = $$PWD/../../lib
OBJECTS_DIR = $$OUT_PWD/obj

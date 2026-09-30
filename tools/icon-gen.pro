# 应用图标生成器（SVG → 多尺寸 PNG → Windows 多尺寸 ICO）
#
# 与 cross-check / piano-batch 不同，本工具**需要 Qt**（QtGui + QtSvg）：
# 渲染 SVG 与编码 PNG 都得靠它们。故这里显式写 QT += gui svg。
#
# 产物：resources/branding/app-icon.ico（由 src/app/app.pro 的 RC_ICONS 编进 exe）。
# 为什么把 ICO 一并入库：它是 Windows 构建的**输入**（RC_ICONS 在编译期读它），
# 缺了会直接构建失败；这与"生成物不入库"并不冲突——它是"决定外观的源素材的编译产物"，
# 与 app-icon.svg 同源，改动 SVG 后重跑本工具即可（见 ATTRIBUTION.md）。

TEMPLATE = app
CONFIG -= debug_and_release debug
CONFIG += release console
CONFIG -= app_bundle

# 注意：本工具**要用 Qt**（与 tools 里其它两个纯 C++ 工具不同），故不写 `CONFIG -= qt`
QT += core gui svg

TARGET = icon-gen
QMAKE_CXXFLAGS += -std=c++20 -Wall -Wextra -Wpedantic -Wshadow

SOURCES += icon-gen.cpp

# 本文件位于 tools/，故 $$PWD/.. 才是仓库根（与 cross-check.pro / piano-batch.pro 一致）
DESTDIR = $$PWD/../bin
OBJECTS_DIR = $$OUT_PWD/obj
MOC_DIR = $$OUT_PWD/moc

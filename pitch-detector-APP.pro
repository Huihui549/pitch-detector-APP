# pitch-detector-APP —— qmake 工程
#
# 构建系统：**qmake**（用户指定，不用 CMake）。
# 工具链：Qt 6.8.3 自带 MinGW 13.1.0（**必须用 Qt 自带的那个**：用其它 MinGW 会因
#         运行库堆不一致而在 QString::toStdString 处崩溃，见坑 A16）。
#
# 构建（[PC] 本目录）：
#   D:\Qt\6.8.3\mingw_64\bin\qmake.exe pitch-detector-APP.pro CONFIG+=release
#   D:\Qt\Tools\QtCreator\bin\jom\jom.exe            (或 mingw32-make.exe)
# 运行：
#   双击 run-app.bat
#
# 为什么 qmake 下 QML 用 qrc 而不是"QML 模块 + 插件"：
#   qmake 没有 qt_add_qml_module，QML 模块的 qmldir/插件都需要手工维护；
#   而把 QML 与手写 qmldir 一起编进 qrc 后，QML 直接进可执行文件，
#   运行期不依赖任何磁盘布局——反而避开了 CMake 那套在 windeployqt 部署目录下
#   "模块找不到"的问题（坑 A17）。

TEMPLATE = subdirs
CONFIG += ordered

# 强制 Release 构建，**禁用 debug_and_release**。
#
# 两条实测理由：
#   1. Qt 官方在线安装器**只装 release 库**（没有 Qt6Cored.dll 等），Debug 构建根本跑不起来；
#   2. 更隐蔽的是：qmake 的 debug_and_release 会让 Debug 与 Release 把静态库输出到**同一个**
#      `lib/` 目录，两者互相覆盖。结果可能是"Release 的可执行文件链到 Debug 的静态库"，
#      CRT 不一致会导致难以定位的崩溃（本项目此前已因堆不一致崩过一次，见坑 A16）。
#     Qt Creator 默认按 Debug 构建，正是这条路径的触发者。
# 固定 Release 后，无论从命令行还是 Qt Creator 点运行，结果一致。
CONFIG -= debug_and_release debug
CONFIG += release

# 依赖序：core → io → audio → app；tools / tests 只依赖 core 与 io
SUBDIRS = \
    src/core/core.pro \
    src/io/io.pro \
    src/audio/audio.pro \
    src/app/app.pro \
    tools/tools.pro \
    tests/core-tests.pro

# 依赖关系：保证被依赖的库先构建，且链接时已存在
src/io/io.pro.depends = src/core/core.pro
src/audio/audio.pro.depends = src/core/core.pro src/io/io.pro
src/app/app.pro.depends = src/core/core.pro src/io/io.pro src/audio/audio.pro
tools/tools.pro.depends = src/core/core.pro src/io/io.pro
tests/core-tests.pro.depends = src/core/core.pro src/io/io.pro

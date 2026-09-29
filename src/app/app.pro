# 应用层（src/app/）—— 控制器、QML 资源、程序入口
#
# 依赖方向：src/app → {src/audio, src/io, src/core}。控制器里**不含音高算法**（分层铁律）。
#
# QML 处理方式（与 CMake 方案的差异，也是这一版更省事的地方）：
#   qmake 没有 qt_add_qml_module，因此 QML 模块不生成插件，而是：
#     ① 手写 qml/qmldir（声明模块名与各类型；Theme 显式标 singleton）
#     ② 用 qml.qrc 把 qml/ 整个目录（含 qmldir）编进可执行文件
#     ③ main.cpp 里 addImportPath(":/") + loadFromModule
#   好处：QML 直接进可执行文件，运行期不依赖任何磁盘目录布局——
#   避开了 CMake 方案下 windeployqt 部署目录里"模块找不到"的问题（坑 A17）。

TEMPLATE = app
# 固定 Release：Qt 官方安装器只装 release 库，且 debug_and_release 会让两种配置的
# 静态库输出到同一个 lib/ 目录互相覆盖（见根 .pro 的说明）。
CONFIG -= debug_and_release debug
CONFIG += release

TARGET = pitch-detector-APP

# widgets：只为用 QFileDialog 打开音频文件。
# 为什么不用 QML 的 FileDialog：实测在 Windows 上 `selectedFile` 不可靠
# （用户报"选择音频导入却提示无法载入"），而 QFileDialog 是原生对话框、行为确定。
# 只引入这一个类，不引入任何 QWidget 界面。
QT += core gui widgets qml quick quickcontrols2

# Qt Multimedia 是可选依赖，与 src/audio/audio.pro 用同一判定保持一致。
# 为什么这里也要写一遍：qmake 的 subdirs 各工程相互独立，DEFINES **不会**沿库依赖传递
# （实测：audio 里写了 PITCH_HAVE_QT_MULTIMEDIA=1，app 里依然是 0）。
# 本工程的 --devices 设备罗列与实时页提示文案依赖这个宏。
qtHaveModule(multimedia) {
    QT += multimedia
    DEFINES += PITCH_HAVE_QT_MULTIMEDIA=1
} else {
    DEFINES += PITCH_HAVE_QT_MULTIMEDIA=0
}

# 保留控制台输出（--selftest / --qmlcheck 需要打印结果；GUI 窗口照常显示）
CONFIG += console

# 关闭 QML 预编译（qmlcachegen）。
# 原因（实测）：qmake 默认会对 QML 做 qmlcachegen 预编译，而本项目的 QML 模块是
# **手写 qmldir + qrc**（qmake 没有 qt_add_qml_module）。预编译产物的模块解析上下文
# 与运行期不一致时，会出现"文件都在资源里、却报 Theme is not defined"这类怪象。
# 关掉后由引擎在运行期正常解析 qmldir，事实来源唯一，也更好排查。
CONFIG -= qmlcache

# 告警门槛：本项目要求 0 warning。这里**不含 -Wconversion**——
# Qt 自身头文件（qpoint.h / qtyperevision.h 等）会触发大量 int→float/quint8 收窄告警，
# 那属 Qt 实现细节，不是本项目缺陷；混进来只会淹没真正的告警。
# 算法层（src/core、src/io）不含 Qt 头，故那两处保留 -Wconversion 的严格口径。
# 诊断开关：定义 PITCH_NO_DISK_QML_FALLBACK 可禁用"从 <exe>/qml 加载"的兜底，
# 用于判定 QML 究竟是从资源模块加载还是从磁盘加载（两者类型作用域不同）。
# 平时的构建不要打开。
# DEFINES += PITCH_NO_DISK_QML_FALLBACK

QMAKE_CXXFLAGS += -std=c++20 -Wall -Wextra -Wpedantic -Wshadow

SOURCES += \
    main.cpp \
    pitch-session-controller.cpp \
    file-analysis-controller.cpp

HEADERS += \
    pitch-session-controller.h \
    file-analysis-controller.h \
    theme.h

INCLUDEPATH += $$PWD/../core $$PWD/../io $$PWD/../audio

# QML 资源：路径前缀 /PitchDetector，于是资源内为 :/PitchDetector/Main.qml
RESOURCES += $$PWD/../../qml/qml.qrc

LIBS += -L$$PWD/../../lib -lpitch-audio -lpitch-io -lpitch-core -lm

DESTDIR = $$PWD/../../bin
OBJECTS_DIR = $$OUT_PWD/obj
MOC_DIR = $$OUT_PWD/moc
RCC_DIR = $$OUT_PWD/rcc

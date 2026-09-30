# 采集层（src/audio/）
#
# 依赖方向：src/audio → src/core（+ 可选 Qt Multimedia）；不依赖 UI，也不依赖 src/app。
#
# Qt Multimedia 是**可选**依赖：当前环境未安装该模块，故条件编译。
# 这不是"半死不活的可选依赖"——模块缺失时相关实现根本不进构建，上层以"模块缺失"为由
# 禁用实时页，而不是编译进去再在运行期失败（那会掩盖依赖问题）。

TEMPLATE = lib
# 固定 Release：Qt 官方安装器只装 release 库，且 debug_and_release 会让两种配置的
# 静态库输出到同一个 lib/ 目录互相覆盖（见根 .pro 的说明）。
CONFIG -= debug_and_release debug
CONFIG += release

CONFIG += staticlib

TARGET = pitch-audio

QT += core

# 告警门槛：不含 -Wconversion（Qt 头文件自身会触发大量收窄告警，属噪声，见 app.pro 的说明）
QMAKE_CXXFLAGS += -std=c++20 -Wall -Wextra -Wpedantic -Wshadow

SOURCES += \
    file-audio-source.cpp \
    metronome-engine.cpp

HEADERS += \
    i-audio-source.h \
    file-audio-source.h \
    metronome-engine.h

INCLUDEPATH += $$PWD/../core $$PWD/../io

LIBS += -L$$PWD/../../lib -lpitch-io -lpitch-core -lm

# Qt Multimedia 存在时，才把真实麦克风实现编进来。
# 补装方式：用 Qt 安装目录下的 MaintenanceTool 勾选 Qt Multimedia 后重新 qmake + 构建。
qtHaveModule(multimedia) {
    message("audio: Qt Multimedia available -> building QtAudioSource (microphone)")
    QT += multimedia
    SOURCES += qt-audio-source.cpp
    HEADERS += qt-audio-source.h
    DEFINES += PITCH_HAVE_QT_MULTIMEDIA=1
} else {
    # 提示保持纯 ASCII：qmake 的 message/warning 在 Windows 控制台按本地代码页输出，
    # 中文会显示成乱码（实测），反而看不清关键信息。
    warning("audio: Qt Multimedia NOT installed -> QtAudioSource skipped. Microphone disabled; FileAudioSource (WAV playback) still works. Install it via the Qt MaintenanceTool, then re-run qmake.")
    DEFINES += PITCH_HAVE_QT_MULTIMEDIA=0
}

DESTDIR = $$PWD/../../lib
OBJECTS_DIR = $$OUT_PWD/obj
MOC_DIR = $$OUT_PWD/moc

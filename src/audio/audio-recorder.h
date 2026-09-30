// 录音器（Qt Multimedia 的 QMediaRecorder）
//
// 为什么用 QMediaRecorder 而不是"自己抓样点写文件"：
//   **只有后端编码器能产出 mp3**（实测本机 Qt Multimedia 的 FFmpeg 后端支持 MP3 编码，见 `--recformats`）；
//   自己写只能出 WAV。所以"保存为 mp3"这条要求决定了必须走 QMediaRecorder。
//
// 工作方式（与用户要求的四个操作一一对应）：
//   · 开始录音 → 写入**临时文件**（不由用户指定路径）
//   · 暂停/继续 → pause() / resume()
//   · 停止     → stop() 并**删除临时文件**（用户定义：停止=清除已录内容，从头再来）
//   · 保存     → 把临时文件复制到用户选定的路径（不重新编码，故与录制时同一份数据）
//
// 与"实时分析"的关系：录制的原始样点**不经过本类**（QMediaRecorder 不暴露样点），
// 实时分析走并行的采集通道（见 src/audio/qt-audio-source.h）。二者解耦的好处是
// "录不上也能分析、分析起不来也能录"，任一条挂了不会把另一条一起拖死。
//
// 分层：src/audio → 无 UI 依赖。Qt Multimedia 缺失时整体降级为"不可用"。
//
// 编码要求（坑 A20）：本文件必须存为 UTF-8 **无 BOM**。

#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>

QT_BEGIN_NAMESPACE
class QMediaRecorder;
class QMediaCaptureSession;
class QAudioInput;
QT_END_NAMESPACE

namespace pitch {

class AudioRecorder : public QObject {
    Q_OBJECT

public:
    enum class State {
        Idle,       ///< 未在录音（可能有已录内容待保存）
        Recording,  ///< 录音中
        Paused,     ///< 已暂停（再点即继续）
        Saving,     ///< 保存中
    };
    Q_ENUM(State)

    explicit AudioRecorder(QObject* parent = nullptr);
    ~AudioRecorder() override;

    bool available() const;
    QString unavailableReason() const;

    State state() const { return m_state; }
    /// 已录时长（秒）；暂停期间不增长
    double elapsedSec() const;
    /// 是否已有可保存的录音（停止后为 false——停止即清空）
    bool hasTake() const { return m_hasTake; }
    /// 临时文件路径（调试用；不要显示给用户）
    QString tempPath() const { return m_tempPath; }
    /// 实际协商到的容器扩展名（"mp3"/"m4a"/"wav"/"flac"）。
    /// **保存时必须按它命名**，否则会得到"名字是 .wav、内容其实是 mp3"这种假文件。
    QString containerExtension() const { return m_extension; }
    /// 录制格式与设备的可读描述（"为什么保存出来是这个格式"）
    QString description() const { return m_description; }
    QString lastError() const { return m_lastError; }

    /// 开始录音。失败返回 false，原因见 lastError()。
    bool start();
    /// 暂停（再调 resume 继续）。非录音状态调用无副作用。
    void pause();
    void resume();
    /// 停止并**丢弃**本次录音（用户要求：停止 = 清除已录内容，从头开始）。
    void stopAndDiscard();
    /// 停止但**保留**本次录音（用于"录音中直接点保存"：先把文件收尾，再复制）。
    /// 与 stopAndDiscard 的区别只有一个：**不删临时文件**。
    void finish();
    /// 把本次录音保存到指定路径。扩展名决定容器（.mp3 / .wav / .m4a）。
    bool saveAs(const QString& path, QString* errorOut);

    /// 不录音，只探测"本后端协商出的录制格式"：用于在**没有麦克风的机器上**也能核对格式能力
    /// （实测本机就是这样：没有输入设备，但编码能力必须能验证）。
    static QString negotiatedFormatDescription();

signals:
    void stateChanged();
    void errorOccurred(const QString& message);

private:
    void setState(State state);

    State m_state = State::Idle;
    bool m_hasTake = false;
    QString m_tempPath;
    QString m_description;
    /// 协商到的容器扩展名（默认 wav：没装 Multimedia 时最保守的取值）
    QString m_extension = QStringLiteral("wav");
    QString m_lastError;
    QElapsedTimer m_clock;          ///< 用于 elapsedSec（暂停时按累计暂停时长扣除）
    qint64 m_pausedTotalMs = 0;
    qint64 m_pauseStartedMs = 0;

    QMediaRecorder* m_recorder = nullptr;
    QMediaCaptureSession* m_session = nullptr;
    QAudioInput* m_input = nullptr;
};

} // namespace pitch

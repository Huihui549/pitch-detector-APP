// 录音控制器（QML 单例 `Recorder`）
//
// 只是把 src/audio 的 AudioRecorder 接到界面：状态文案、计时刷新、保存对话框。
// 录音格式与容器协商的**能力探测**在 `--recformats`（实测本机后端支持 MP3 编码）。
//
// 编码要求（坑 A20）：本文件必须存为 UTF-8 **无 BOM**。

#pragma once

#include "audio-recorder.h"

#include <QObject>
#include <QString>
#include <QTimer>

namespace pitch {

class FileAnalysisController;
class PitchSessionController;

class RecorderController : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool available READ available NOTIFY stateChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY stateChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY stateChanged)
    /// 是否已有可保存的录音（**停止后为 false**：用户定义"停止=清空，从头开始"）
    Q_PROPERTY(bool hasTake READ hasTake NOTIFY stateChanged)
    /// 已录时长（"12.3 s"），暂停时不增长
    Q_PROPERTY(QString elapsedText READ elapsedText NOTIFY tickChanged)
    Q_PROPERTY(QString stateText READ stateText NOTIFY stateChanged)
    /// 不可用原因 / 最近一次错误（界面照原样显示，不改写）
    Q_PROPERTY(QString notice READ notice NOTIFY stateChanged)
    /// 录制格式说明（静态，构造后不变）
    Q_PROPERTY(QString formatDescription READ formatDescription CONSTANT)
    /// 实时分析是否随之启动（录音期间为 true；起不来时为 false，此时界面提示"仅录音"）
    Q_PROPERTY(bool liveAnalysisActive READ liveAnalysisActive NOTIFY stateChanged)

public:
    /// @param session        实时分析用的采集控制器（**复用既有实现**：它已处理权限、设备选择、格式协商）。
    ///                       录音期间由本类负责 start/stop，界面直接绑 `Session.*` 显示读数，无需再转发一遍属性。
    /// @param fileAnalysis   录音页的文件分析控制器：保存成功后用它分析刚保存的文件，
    ///                       于是"录完能在钢琴卷帘里看到这一段"（见 save()）
    explicit RecorderController(PitchSessionController* session, FileAnalysisController* fileAnalysis,
                               QObject* parent = nullptr);

    bool available() const { return m_recorder.available(); }
    bool recording() const { return m_recorder.state() == AudioRecorder::State::Recording; }
    bool paused() const { return m_recorder.state() == AudioRecorder::State::Paused; }
    bool hasTake() const { return m_recorder.hasTake(); }
    QString elapsedText() const;
    QString stateText() const;
    QString notice() const { return m_notice; }
    QString formatDescription() const { return m_recorder.description(); }

    /// 开始录音
    Q_INVOKABLE void start();
    /// 暂停 / 继续（同一个入口：界面按钮文字随状态变）
    Q_INVOKABLE void pauseOrResume();
    /// 停止并清空（从头再来）
    Q_INVOKABLE void stopAndDiscard();
    /// 保存到用户选定的路径（弹出原生保存对话框，默认 .mp3）。返回实际路径（取消为空）
    Q_INVOKABLE QString save();

signals:
    void stateChanged();
    void tickChanged();

public:
    /// 实时分析是否随之启动（录音期间为 true；起不来时为 false，此时界面提示"仅录音"）
    bool liveAnalysisActive() const { return m_liveAnalysisActive; }

private:
    AudioRecorder m_recorder{};
    PitchSessionController* m_session = nullptr;
    FileAnalysisController* m_fileAnalysis = nullptr;
    bool m_liveAnalysisActive = false;
    QTimer m_tickTimer{};
    QString m_notice{};
};

} // namespace pitch

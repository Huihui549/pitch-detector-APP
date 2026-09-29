// 音频采集抽象（IAudioSource）
//
// 为什么要有这层抽象（ADR-0005）：目标形态含"手机 APP + 桌面 + 后续嵌入式"，
// 三者的采集方式不同（Qt Multimedia / 厂商 SDK / ALSA）。把平台差异封在唯一一层，
// 上层（src/app）完全不感知音频从哪来。
//
// 首版落地的实现有两个，分工明确：
//   · QtAudioSource  —— 真实麦克风（依赖 Qt Multimedia；**该模块当前未安装**，见 AGENTS.md）
//   · FileAudioSource —— 把 WAV 当实时流按真实速率喂入（用于无麦克风环境下验证实时链路）
// 这样"实时逻辑"在没有任何音频硬件时也能被自动验证（上游最大的空白就是实时链路从未验证）。

#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include <cstddef>

namespace pitch {

/// 采集失败的原因分类。分开是为了让界面给出**可操作的**提示，而不是笼统的"采集失败"。
enum class AudioErrorKind {
    None,
    ModuleMissing,     ///< Qt Multimedia 未安装（当前环境即此情形）
    NoDevice,          ///< 无可用输入设备
    PermissionDenied,  ///< 权限被拒（手机端常见）
    FormatUnsupported, ///< 设备不支持所需格式
    FormatMismatch,    ///< 实际格式与请求不符（上层仍可按实际格式工作）
    Unknown,
};

/// 采集状态。
enum class AudioState {
    Stopped,
    Starting,
    Running,
    Error,
};

/// 音频采集接口。
///
/// 设计约束（R8：不预留用不到的接口）：只保留首版真正需要的五项——
/// 开（含格式请求）/ 停 / 查实际格式 / 样点回调 / 错误信号。
/// 嵌入式实现落地前**不新增接口方法**。
class IAudioSource : public QObject {
    Q_OBJECT

public:
    explicit IAudioSource(QObject* parent = nullptr) : QObject(parent) {}
    ~IAudioSource() override = default;

    /// 请求开始采集。
    /// @param sampleRate 期望采样率；实现应尽量接近，但**实际值以 actualSampleRate() 为准**
    /// @param channels   期望声道数
    virtual void start(int sampleRate, int channels) = 0;

    /// 停止采集。重复调用应安全。
    virtual void stop() = 0;

    /// 实际生效的采样率（未启动时为请求值）。τ 换算必须用这个值，不得假定 44.1 kHz。
    virtual int actualSampleRate() const = 0;

    /// 实际生效的声道数。
    virtual int actualChannels() const = 0;

    /// 人类可读的实现名与设备描述，用于调试界面显示。
    virtual QString description() const = 0;

    /// 该实现当前是否可用（例如 Qt AudioSource 在未安装 Multimedia 时返回 false）。
    virtual bool isAvailable() const = 0;

signals:
    /// 采集到一批单声道样点（实现负责多声道下混）。
    /// 注意：**不保证固定长度**——上层必须按环形缓冲处理，不得假设每次回调的样点数（坑 A4）。
    void samplesReady(const QVector<float>& mono, int sampleRate);

    /// 实际格式与请求不一致时发出（上层据此提示，但应继续工作）。
    void formatMismatch(int requestedRate, int actualRate, int requestedChannels, int actualChannels);

    /// 发生错误。kind 决定界面提示文案。
    void errorOccurred(pitch::AudioErrorKind kind, const QString& message);

    /// 状态变化。
    void stateChanged(pitch::AudioState state);
};

} // namespace pitch

Q_DECLARE_METATYPE(pitch::AudioErrorKind)
Q_DECLARE_METATYPE(pitch::AudioState)

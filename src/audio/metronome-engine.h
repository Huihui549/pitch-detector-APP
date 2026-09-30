// 节拍器音频输出（Qt Multimedia 的 QAudioSink）
//
// 分层位置：src/audio → src/core。时间轴与点击声的**全部逻辑**在 core（纯 C++、可单测），
// 本文件只做一件事：把 core 渲染出来的单声道样点按设备要求送进声卡。
//
// 为什么用"拉模式 + 自己造 QIODevice"而不是 QSoundEffect / QMediaPlayer：
//   · QSoundEffect 由定时器触发播放，抖动在毫秒级且会漂移——节拍器最不能忍这个（见 core 的说明）
//   · 拉模式下由 Qt 的音频线程按块来取数据，取多少、放在哪个样点上都由我们决定 ⇒ 采样级精确
//
// 设备格式的处理策略（**真值以 QAudioSink::format() 为准**，与采集侧同一原则）：
//   优先请求"单声道 + Float32 + 设备首选采样率"；设备不支持时退到设备首选格式，
//   并在渲染后做一次转换（单声道→立体声复制、Float32→Int16）。两者都不支持则明确报错，
//   而不是发不出声还装作在播放（采集侧踩过"错误码 0"的哑失败，坑 A41）。
//
// 编码要求（坑 A20）：本文件必须存为 UTF-8 **无 BOM**。

#pragma once

#include "metronome-renderer.h"   // src/core：时间轴与音色（零 Qt）

#include <QObject>
#include <QString>

#include <atomic>
#include <vector>

QT_BEGIN_NAMESPACE
class QAudioSink;
QT_END_NAMESPACE

namespace pitch {

/// 节拍器播放引擎：core 渲染器 + Qt 音频输出。
class MetronomeEngine : public QObject {
    Q_OBJECT

public:
    explicit MetronomeEngine(QObject* parent = nullptr);
    ~MetronomeEngine() override;

    /// 构建时是否包含 Qt Multimedia（未包含时本引擎不可用，界面据此禁用播放按钮）
    bool available() const;
    /// 不可用的原因（界面照原样显示，不做二次解释）
    QString unavailableReason() const;

    bool running() const { return m_running; }

    /// 开始播放。失败时返回 false，原因见 lastError()。
    bool start();
    void stop();

    // ---------------- 配置（可在播放中调用）----------------
    void setBpm(int bpm);
    void setPattern(const Pattern& p);
    void setVoice(int role, const ClickVoice& v);
    /// 试听某个角色的音色（不影响节拍时间轴）
    void preview(int role);

    /// 是否已配置过低速/拍号（供界面判断是否需要提示）
    int bpm() const;

    /// 设备实际采样率（未启动时为 0）
    int actualSampleRate() const;
    /// 输出设备与**实际格式**的可读描述（调试页显示；"以为在播其实没出数据"时靠它判断）
    QString description() const;
    /// 最近一次错误（空 = 无错误）
    QString lastError() const { return m_lastError; }

    /// 已交给声卡的样点帧数与回调次数（用于判定"设备真的在取数据"）
    long long renderedFrames() const;
    int callbackCount() const;

    /// 供界面轮询的"最近一次拍点"：读的是原子量，无需加锁（音频线程写、界面线程读）
    TickInfo pollTick() const;

signals:
    void runningChanged();

private:
    class RenderDevice;   ///< 拉模式的 QIODevice（定义在 .cpp，避免头文件依赖 Qt Multimedia）

    /// 按设备格式把单声道 float 渲染结果写进设备缓冲（返回写入字节数）
    qint64 pullAudio(char* data, qint64 maxBytes);

    void resetSink();

    MetronomeRenderer m_renderer{};
    QAudioSink* m_sink = nullptr;
    RenderDevice* m_device = nullptr;
    QString m_lastError{};
    QString m_description{};
    bool m_running = false;
    int m_actualSampleRate = 0;

    /// 设备格式（启动时确定；-1 = 未知）
    int m_deviceChannels = 1;
    int m_deviceSampleFormat = -1;   ///< QAudioFormat::SampleFormat 的整数值
    int m_bytesPerFrame = 4;

    std::vector<float> m_scratch{};   ///< 单声道渲染暂存（**预分配**：音频回调里不分配内存）
    std::vector<char> m_packBuffer{}; ///< 转换后的字节缓冲（同样预分配）

    // 音频线程 → 界面线程的单向发布（避免在音频回调里加锁）
    std::atomic<long long> m_tickCounter{-1};
    std::atomic<long long> m_tickBar{0};
    std::atomic<int> m_tickBeat{0};
    std::atomic<int> m_tickSub{0};
    std::atomic<int> m_tickRole{1};
    std::atomic<bool> m_tickAccent{false};
    std::atomic<long long> m_frames{0};
    std::atomic<int> m_callbacks{0};
};

} // namespace pitch

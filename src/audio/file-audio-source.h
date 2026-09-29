// 把 WAV 当作实时流播放的采集实现
//
// 存在的理由（不是产品功能，是测试手段）：
//   上游项目最大的空白是"实时链路从未被验证"——麦克风、权限、设备约束全都要人工。
//   本实现按**真实时间速率**把一段已知的 WAV 喂给实时链路，于是：
//     · 无需麦克风即可端到端验证实时逻辑（窗长、帧进、平滑、保持、界面刷新）
//     · 输入信号是已知的，因此"读数不对"能立刻定位到算法或界面，而不是猜是信号问题
//
// 与真实麦克风的差异（必须清楚，不得当成等价）：没有设备 AGC/降噪/频响、没有环境噪声、
// 不会丢帧。故它验证的是**链路逻辑**，不是"在真实房间里测得准不准"。

#pragma once

#include "i-audio-source.h"

#include <QByteArray>
#include <QTimer>

#include <vector>

namespace pitch {

/// 以真实时间速率回放 WAV 的采集源。
class FileAudioSource : public IAudioSource {
    Q_OBJECT

public:
    explicit FileAudioSource(QObject* parent = nullptr);
    ~FileAudioSource() override;

    /// 载入 WAV 文件（未压缩 PCM）。成功后可反复 start/stop。
    /// @return 成功与否；失败原因由 errorOccurred 发出
    bool load(const QString& path);

    void start(int sampleRate, int channels) override;
    void stop() override;
    int actualSampleRate() const override { return m_sampleRate; }
    int actualChannels() const override { return 1; }
    QString description() const override;
    bool isAvailable() const override { return !m_samples.empty(); }

    /// 回放速度倍率（1.0 = 真实时间）。用于加速测试；界面不暴露此参数。
    void setSpeedFactor(double factor) { m_speedFactor = factor > 0.0 ? factor : 1.0; }

private slots:
    void onTimerTick();

private:
    std::vector<float> m_samples;
    QString m_path;
    int m_sampleRate = 44100;
    std::size_t m_cursor = 0;
    double m_speedFactor = 1.0;
    QTimer m_timer;
    bool m_loaded = false;

    /// 每次回调的样点数：对应上游实时的 512 样点缓冲（约 11.6 ms @44.1 kHz）。
    static constexpr int kChunkSamples = 512;
};

} // namespace pitch

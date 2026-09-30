// 节拍时间轴渲染器 —— **纯逻辑，零 Qt 依赖**
//
// 职责：把"拍号 + 逐拍细分 + BPM"变成一条**按采样点精确摆放**的单声道音频流。
//
// 为什么不让界面用定时器去"到点播一声"：
//   定时器（QTimer）在桌面与手机上的抖动通常在几毫秒到几十毫秒，且会**积累漂移**；
//   节拍器最不能忍的就是"越走越偏"。本渲染器改由音频线程按样点数推进，
//   最小时间单位就是 1 个采样点（48 kHz 下约 20.8 µs），抖动为 0、无长期漂移。
//
// 时间模型（关键，决定了变速与改拍号时的行为）：
//   · 位置用**绝对拍位**（double，单位=拍）表示，而不是"还剩多少样点"。
//   · 每渲染一块，拍位游标按 `frames / samplesPerBeat` 前进。
//   · 每个点击事件的时刻在**拍空间**里是固定的（小节内 beatPosition），
//     渲染时再换算成"本块内的样点偏移"。
//   ⇒ 播放中改 BPM 只改变"拍→样点"的比例，不会丢拍也不会跳拍；
//     改拍号/细分则从当前拍位重新起一小节（结构性改动，立刻生效且可预期）。
//
// 重叠：高 BPM 下的密集细分会让两次点击在时间上重叠。渲染器用固定长度的活跃列表
//   逐块推进每条点击的游标，叠加即可——**渲染过程中不分配内存**（音频回调里不能分配）。
//
// 编码要求（坑 A20）：本文件必须存为 UTF-8 **无 BOM**。

#pragma once

#include "click-voice.h"
#include "metronome-pattern.h"

#include <cstddef>
#include <vector>

namespace pitch {

/// 最近一次发声的点击信息（界面据此指示"第几拍"）。
struct TickInfo {
    long long counter = -1;   ///< 单调递增的点击序号；-1 表示尚未发声
    long long barIndex = 0;   ///< 第几小节（0 基）
    int beatIndex = 0;        ///< 第几拍（0 基）
    int subIndex = 0;         ///< 该拍内的第几个细分点（0 基）
    int role = 1;             ///< 音色角色（ClickRole 的整数值）
    bool accent = false;      ///< 是否强拍
};

class MetronomeRenderer {
public:
    MetronomeRenderer();

    // ---------------- 配置（允许在"播放中"调用）----------------

    /// 设置输出采样率。**会重置时间轴**（采样率变化意味着设备重开，旧位置无意义）。
    void setSampleRate(double rate);
    /// 设置速度（BPM）。播放中改速度不会丢拍/跳拍（见文件头的拍空间说明）。
    void setBpm(int bpm);
    /// 设置拍号与逐拍细分。**从当前拍位重新起一小节**（结构改动，立刻生效）。
    void setPattern(const Pattern& p);
    /// 设置某个角色的音色（0=强拍 1=弱拍 2=细分）。
    void setVoice(int role, const ClickVoice& v);

    double sampleRate() const { return m_sampleRate; }
    int bpm() const { return m_bpm; }
    Pattern pattern() const { return m_pattern; }
    ClickVoice voice(int role) const;
    /// 每拍的样点数（= 60 / BPM × 采样率）
    double samplesPerBeat() const;

    // ---------------- 运行 ----------------

    /// 试听：下一次 render 的**开头**叠加一次该角色的点击，且不影响节拍时间轴。
    void preview(int role);

    /// 渲染 frames 个单声道样点（**覆盖** out，不是叠加）。返回实际写入的样点数。
    std::size_t render(float* out, std::size_t frames);

    // ---------------- 观测（供界面轮询与测试断言）----------------

    TickInfo lastTick() const { return m_lastTick; }
    long long renderedFrames() const { return m_renderedFrames; }
    /// 已发声的点击总数（按角色分别累计）
    long long tickCount(int role) const;
    /// 当前绝对拍位（单位=拍）
    double beatCursor() const { return m_beatCursor; }
    /// 下一个点击的绝对拍位（单位=拍）
    double nextEventBeat() const { return m_nextEventBeat; }

    static constexpr int kRoleCount = 3;

private:
    /// 一条正在发声的点击（游标 = 已经放掉多少个样点）
    struct ActiveClick {
        int role = 0;
        std::size_t cursor = 0;
        std::size_t offset = 0;   ///< 在本块内的起始偏移；每块渲染后清零
        bool active = false;
    };

    static constexpr std::size_t kMaxOverlap = 8;   ///< 同时最多几条点击在响

    void rebuildEvents();
    void spawn(int role, std::size_t offsetInBlock);

    double m_sampleRate = 48000.0;
    int m_bpm = kDefaultBpm;
    Pattern m_pattern = normalize(Pattern{});
    std::vector<ClickEvent> m_events{};
    ClickVoice m_voices[kRoleCount]{};

    // 时间轴状态（拍空间）
    double m_beatCursor = 0.0;       ///< 当前渲染位置的绝对拍位
    double m_nextEventBeat = 0.0;    ///< 下一个点击的绝对拍位
    std::size_t m_eventIndex = 0;    ///< 下一个点击在 m_events 里的下标
    long long m_barIndex = 0;
    long long m_renderedFrames = 0;

    ActiveClick m_active[kMaxOverlap]{};
    bool m_previewPending[kRoleCount] = {false, false, false};
    TickInfo m_lastTick{};
    long long m_tickCounters[kRoleCount] = {0, 0, 0};
    long long m_totalTicks = 0;
};

} // namespace pitch

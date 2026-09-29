// 音高检测 Qt 应用 —— 核心算法层的纯数据类型与参数
//
// 本文件属于 src/core/ 层：只依赖 C++ 标准库，禁止包含任何 Qt 头文件（设计约束，见
// dev-docs/pitch-detector-APP/design/architecture.md 第二节）。
//
// 参数来源：上游网页版唯一算法实现
//   [PC] D:\dev_project\pitch-detector\tools\pitch-engine.js（492 行，以下简称"上游引擎"）
// 每个常数的取值理由都写在上游引擎的注释里，本文件只标注对应位置，不重复叙述（R7 SSOT）。
// 改动任何常数都必须同时改动上游引擎并重跑跨语言对拍，否则两条链会分叉（上游 pitfalls #22）。

#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace pitch {

/* ============================ 引擎参数 ============================ */

/// A4 基准频率（Hz）。界面可改，算法默认值取 440。对应上游 `A4`。
inline constexpr double kDefaultA4 = 440.0;

/// 音域下限（Hz）。默认覆盖钢琴全 88 键的 A0 = 27.5 Hz，留有 0.5 Hz 余量。对应上游 `F_MIN`。
inline constexpr double kDefaultFMin = 27.0;

/// 音域上限（Hz）。C8 = 4186 Hz，留有余量。对应上游 `F_MAX`。
inline constexpr double kDefaultFMax = 4300.0;

/// 级联窗长（短 → 长）。全键盘周期跨度 152 倍，单一窗长两端必坏（上游 pitfalls #19）；
/// 判据是"τ 区间是否完整落在窗内"（tauMax < n/2）。对应上游 `FRAME_LADDER`。
inline constexpr std::array<std::size_t, 5> kFrameLadder{1024, 2048, 4096, 8192, 16384};

/// 最大窗长，用于预分配工作缓冲。对应上游 `MAX_FRAME`。
inline constexpr std::size_t kMaxFrame = 16384;

/// YIN 累积均值归一化差分的绝对阈值。上游实测网格（0.1/0.15/0.2/0.3）中只有 0.3 全局可用。
inline constexpr double kYinThreshold = 0.3;

/// 绝对静音门槛。对应上游 `RMS_MIN`。
inline constexpr double kRmsMin = 0.008;

/// 相对峰值静音门槛：钢琴高音键 0.2 s 内衰减到峰值 10% 以下，绝对门槛会把有效帧砍到个位数。
inline constexpr double kRmsRelMin = 0.02;

/// 首个谷"浅"的判定阈值，触发浅谷复核。对应上游 `SHALLOW`。
inline constexpr double kShallowThreshold = 0.15;

/// 频域精修的启用下限（Hz）：低于此值 YIN 已足够准（上游实测 ≤900 Hz 误差 0.58 音分）。
inline constexpr double kRefineMinHz = 500.0;

/// 频域精修的搜索步数。对应上游 `REFINE_STEPS`。
inline constexpr int kRefineSteps = 240;

/// 频域精修的搜索半宽（相对值，±3%）。限制在粗估附近，避免选中强泛音（上游 pitfalls #24）。
inline constexpr double kRefineSpan = 0.03;

/// 谐波一致性打分用的最高次泛音。对应上游 `HARMONIC_MAX`。
inline constexpr int kHarmonicMax = 8;

/// 谐波复核的分频频率下限（Hz）：极低音区泛音密集，改判容易越改越偏，故不启用。
inline constexpr double kSubharmonicMinHz = 40.0;

/// 谐波复核改判所需的优势倍数：分数须高于当前 15% 才改判，避免来回摇摆。
inline constexpr double kPreferFundamentalRatio = 1.15;

/// 正弦查表长度。上游用 1024 点查表替代逐点三角函数；本实现保持同一相位轨迹以对齐数值。
inline constexpr int kLutSize = 1024;

/* ============================ 纯数据类型 ============================ */

/// 十二平均律音名（不含八度）。对应上游 `NOTE_NAMES`。
inline constexpr std::array<const char*, 12> kNoteNames{
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

/// 频率 → 音名/八度/音分偏差的结果（科学音高记号，中央 C = C4）。
struct NoteInfo {
    double midi = 0.0;    ///< MIDI 音号（可为小数，含微分音信息）
    int noteIndex = 0;    ///< 0..11，对应 kNoteNames 下标
    int octave = 0;       ///< 八度（SPN：中央 C = C4）
    double cents = 0.0;   ///< 相对最近等程律音级的偏差（音分）
};

/// 单帧基频估计结果。
struct PitchResult {
    double freq = 0.0;       ///< 估计频率（Hz，已过 τ 精修、频域精修与谐波复核）
    double confidence = 0.0; ///< 置信度 ∈ [0,1]，= 1 − 谷底归一化差分
    int tau = 0;             ///< 选中并下滑到谷底后的整数周期（样点）
    std::size_t frameSize = 0; ///< 实际采用的窗长（级联窗长中命中的那一档）
};

/// 逐帧分析结果（含派生字段）。
struct Frame {
    double timeSec = 0.0;    ///< 帧起点时间（秒）
    double freqRaw = 0.0;    ///< 八度折回前的原始频率；未折回时等于 freq
    double freq = 0.0;       ///< 频率（Hz）
    int noteIndex = 0;       ///< 0..11
    int octave = 0;          ///< 八度（SPN）
    double cents = 0.0;      ///< 音分偏差
    double confidence = 0.0; ///< 置信度
    double rms = 0.0;        ///< 该帧 RMS
    std::size_t frameSize = 0; ///< 采用的窗长
    bool octaveFixed = false;  ///< 是否被八度轨迹校正折回过
};

/// 整段分析结果。对应上游 `analyzeBuffer` 的返回值。
struct AnalysisResult {
    double medianFreq = 0.0;   ///< 全段频率中位数
    double medianCents = 0.0;  ///< 中位频率对应的音分偏差
    double peakRms = 0.0;      ///< 全段峰值 RMS
    double rmsFloor = 0.0;     ///< 实际采用的静音门槛
    int octaveFixedCount = 0;  ///< 被八度校正折回的帧数
};

/// 引擎参数。默认值即上游默认值；实时链路与文件分析共用同一套参数（仅帧进与窗长策略不同）。
struct EngineConfig {
    double a4 = kDefaultA4;
    double fMin = kDefaultFMin;
    double fMax = kDefaultFMax;
    /// 谐波一致性复核总开关。它是唯一会改变音名的后处理，关掉即回到纯 YIN 结果（上游 A/B 用）。
    bool harmonicCorrect = true;
    /// 级联窗长候选（升序）。默认即上游 kFrameLadder。
    /// **实时链路未按上游口径使用级联**：上传的实时检测循环是对每个 4096 样点窗直接单帧检测，
    /// 未走级联（上游为低延迟如此设计，ADR-0004）。故实时对拍须把此项设为单个 4096，
    /// 或直接调用 RealtimeRunner，否则会引入一个**已知的口径差异**而非移植错误。
    std::vector<std::size_t> frameLadder{kFrameLadder.begin(), kFrameLadder.end()};
};

/// 引擎工作缓冲。由调用方持有并复用，避免逐帧动态分配（上游 pitfalls：热路径禁分配）。
/// 字段与上游模块级缓冲对应：yinD ↔ yinD、cmnd ↔ cmndBuf。
struct EngineBuffers {
    std::array<double, kMaxFrame> yinD{};   ///< 差分函数 d(τ)
    std::array<double, kMaxFrame> cmnd{};   ///< 累积均值归一化结果
};

} // namespace pitch

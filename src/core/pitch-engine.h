// 音高检测引擎 —— YIN 基频估计（唯一实现）
//
// 本类是上游 `tools/pitch-engine.js` 的 C++ 移植，函数与判据逐行对齐，不允许"按理解重写"：
// 上游每个常数都是从失败方案里实测筛出来的，凭理解改动会静默降低准确度。
//
// 与上游的差异（有意为之，共两处）：
//   1. 可重入：上游用模块级静态缓冲与"最近一次中间量"（lastCmnd 等）；本实现把缓冲作为参数传入，
//      语义等价但可并发/可复用。调用方持有 EngineBuffers，避免逐帧分配。
//   2. 无状态：本类不持有跨帧状态；整段分析（含八度轨迹校正）由 OctaveUnifier 负责。

#pragma once

#include "pitch-types.h"

#include <cstddef>
#include <optional>
#include <span>

namespace pitch {

/// YIN 单帧基频估计引擎。
class PitchEngine {
public:
    /// 单帧检测。返回 std::nullopt 表示该帧无有效音高（置信度不足、超音域、τ 区间不成立）。
    ///
    /// @param buf        单声道样点，长度即窗长；窗太短时 τ 上限被夹住，低音自然返回 nullopt
    /// @param sampleRate 实际采样率（Hz）；τ 换算按实际值，不假定 44.1 kHz
    /// @param cfg        引擎参数
    /// @param buffers    可复用工作缓冲（长度须 ≥ kMaxFrame）
    static std::optional<PitchResult> detect(std::span<const float> buf,
                                             double sampleRate,
                                             const EngineConfig& cfg,
                                             EngineBuffers& buffers);

    /// 单帧检测（附调试曲线）。额外通过出参给出归一化差分曲线，供调试界面画谷值。
    ///
    /// 界面不得自行重算 cmnd——那等于在界面里再维护一份算法细节，与"算法只有一份实现"冲突
    /// （上游 pitfalls #22；上游为此专门加了 detectWithCurve）。
    ///
    /// @param outCurve 输出：归一化差分曲线视图，**有效期至同一 buffers 上的下一次检测调用**
    /// @param outTauMin 输出：曲线起点 τ
    /// @param outTauMax 输出：曲线终点 τ
    static std::optional<PitchResult> detect(std::span<const float> buf,
                                             double sampleRate,
                                             const EngineConfig& cfg,
                                             EngineBuffers& buffers,
                                             std::span<const double>* outCurve,
                                             int* outTauMin,
                                             int* outTauMax);

    /// 级联窗长检测（附调试曲线）：从 cfg.frameLadder 的最短窗试起，第一个有效结果即采用。
    /// 判据由 detect() 内部的 τ 区间自然给出——短窗对低音会因 tauMax 被 n/2 夹住而返回 nullopt。
    ///
    /// 本重载是级联检测的**唯一入口**：调试页需要曲线（R11/上游 pitfalls #22 要求界面不重算），
    /// 不需要曲线的调用方传 nullptr 即可——不为省三个实参维护第二份实现。
    ///
    /// @param data       完整单声道数据
    /// @param offset     当前帧起点在 data 中的下标
    /// @param sampleRate 实际采样率
    /// @param rmsFloor   静音门槛（由 AnalysisRunner 按峰值算得）
    /// @param cfg        引擎参数
    /// @param buffers    可复用工作缓冲
    /// @param outCurve   输出：归一化差分曲线（可传 nullptr）
    /// @param outTauMin  输出：曲线起点 τ（可传 nullptr）
    /// @param outTauMax  输出：曲线终点 τ（可传 nullptr）
    static std::optional<PitchResult> detectWithLadder(std::span<const float> data,
                                                       std::size_t offset,
                                                       double sampleRate,
                                                       double rmsFloor,
                                                       const EngineConfig& cfg,
                                                       EngineBuffers& buffers,
                                                       std::span<const double>* outCurve,
                                                       int* outTauMin,
                                                       int* outTauMax);

    /// 按调用方给定的窗长候选逐个尝试（升序），返回第一个有效结果。
    ///
    /// 存在的理由：**实时链路按上游口径不使用级联窗长**（上游为低延迟直接对 4096 样点窗单帧检测）。
    /// 跨语言对拍需要能复刻那一条路径，故把"候选窗长"提出来做参数，而不是靠改 cfg.frameLadder。
    ///
    /// @param frameSizes 窗长候选（升序）；空表示只试默认最大窗
    static std::optional<PitchResult> detectWithLadderSizes(std::span<const float> data,
                                                            std::size_t offset,
                                                            double sampleRate,
                                                            double rmsFloor,
                                                            const EngineConfig& cfg,
                                                            EngineBuffers& buffers,
                                                            std::span<const std::size_t> frameSizes);

    /// 在粗估 τ 附近直接最小化差分函数（τ 可为小数，取样用线性插值）。
    ///
    /// 为什么不用抛物线插值：抛物线只用谷底三点，周期只有 10–25 样点时跨度过大、曲率估计偏差明显。
    /// 搜索范围 ±0.5 样点、步长 1/64，代价可忽略。对应上游 `refineTau`。
    static double refineTau(std::span<const float> buf, double tauGuess);

    /// 频域相关精修：在粗估附近 ±kRefineSpan 内找幅值峰，修正 τ 整数化在高音区的固定偏差。
    ///
    /// 只做局部微调——八度归属必须由 YIN 的 τ 判据决定（上游实测：取全谱最强峰会选中强泛音）。
    /// 对应上游 `refineFreqByCorrelation`。
    static double refineFreqByCorrelation(std::span<const float> buf,
                                          double sampleRate,
                                          double freqGuess);

    /// 给"候选基频"打谐波一致性分数：信号若真是 f 的谐波列，2f、3f、4f… 处都应有能量。
    /// 打分 = 整数倍处幅值之和（低次权重 1/k）− 半整数倍处幅值之和（背景水平）。
    /// 对应上游 `harmonicScore`。
    static double harmonicScore(std::span<const float> buf, double sampleRate, double freq);

    /// 在候选频率与其整数分频（f/2、f/3）之间挑"更像真基频"的那个。
    /// 分频候选对应"当前选的其实是泛音"的情形——这正是八度摇摆的来源。对应上游 `preferFundamental`。
    static double preferFundamental(std::span<const float> buf, double sampleRate, double freq);

    /// RMS（均方根）。
    static double rms(std::span<const float> buf);

private:
    /// 正弦/余弦查表（1024 点）。上游用查表替代逐点三角函数，本实现保持同一相位轨迹：
    /// 相位增量取整（`idx += step`、`idx & 1023`），以对齐两端的数值。
    struct SineLut {
        double cosTable[kLutSize];
        double sinTable[kLutSize];
        SineLut();
    };
    static const SineLut& sineLut();

    /// 用查表实现的单频点相关幅值（返回幅值/长度，与上游一致）。对应上游 `magAtLut`。
    static double magAtLut(std::span<const float> buf, double sampleRate, double freq);
};

} // namespace pitch

// 整段音频的逐帧分析调度器
//
// 对应上游 tools/pitch-engine.js 的 `analyzeBuffer`（L438-483）。
//
// 职责边界（与上游一致）：
//   1. 先扫一遍求峰值 RMS —— 相对静音门槛依赖它，必须先有峰值再逐帧判定
//   2. 逐帧走级联窗长检测
//   3. 八度轨迹校正（OctaveUnifier）
//   4. **重算音名与音分** —— 校正改了 freq 就必须重算派生字段（上游 pitfalls #28）
//   5. 统计中位频率与其中位音分
// 本类不读文件、不碰界面：WAV 解码在 src/io/，进度回调由上层传入。

#pragma once

#include "pitch-types.h"

#include <functional>
#include <span>
#include <vector>

namespace pitch {

/// 整段分析结果：逐帧明细 + 汇总统计。
struct Analysis {
    std::vector<Frame> frames;  ///< 逐帧结果（已含八度校正与重算后的派生字段）
    AnalysisResult summary;     ///< 汇总统计
};

/// 逐帧分析调度器。
class AnalysisRunner {
public:
    /// 分析进度回调：(已处理比例 0..1) → void。
    using ProgressFn = std::function<void(double)>;

    /// 逐帧分析整段单声道数据。
    ///
    /// **输入长度约束（与上游一致，重要）**：与上游相同，主循环要求 `offset + kMaxFrame ≤ data.size()`，
    /// 即短于 16384 样点（44.1 kHz 下约 371 ms）的音频**不会产生任何帧**。太短的片段本就装不下
    /// 低音的多个周期，属预期行为；上层需在界面上对此给出提示，而不是显示"未检测到有效音高"了事。
    ///
    /// @param data       单声道样点（float，范围约 ±1）
    /// @param sampleRate 实际采样率（Hz）
    /// @param hop        帧进（样点）。文件分析取 441（10 ms，精度优先）
    /// @param cfg        引擎参数
    /// @param onProgress 可选进度回调（可为空 std::function）
    /// @param buffers    可复用工作缓冲；传 nullptr 时内部自建一份（一次性分析场景）
    /// @return 逐帧结果与汇总统计；数据为空或过短时 frames 为空
    static Analysis analyze(std::span<const float> data,
                            double sampleRate,
                            std::size_t hop,
                            const EngineConfig& cfg,
                            const ProgressFn& onProgress = {},
                            EngineBuffers* buffers = nullptr);
};

} // namespace pitch

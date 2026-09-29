// 实时链路逐帧分析
//
// 上游对照：`pitch.html` 的实时循环（固定收窗 4096、帧进 512、《不》走级联窗长）。
//
// 为什么需要单独一条路径：上游的实时链路为了低延迟**不用级联窗长**，音域因此窄于文件分析。
// 本项目首版沿用该策略（ADR-0004），故实时与文件分析必须能分别跑、分别对拍。
//
// 为什么它存在对拍工具里：跨语言对拍要能覆盖实时链路；而实时链路的端到端验收需要麦克风。
// 把"逐帧检测"这一步抽成纯函数后，无需麦克风即可验证它与上游 JS 一致。

#pragma once

#include "pitch-types.h"

#include <cstddef>
#include <span>
#include <vector>

namespace pitch {

/// 实时链路的一帧结果。
struct RealtimeFrame {
    std::size_t offsetSamples = 0;  ///< 该帧起点在数据中的样点下标
    double timeSec = 0.0;           ///< 帧起点时间（秒）
    double freq = 0.0;              ///< 估计频率（Hz）
    int noteIndex = 0;              ///< 0..11
    int octave = 0;                 ///< 八度（SPN）
    double cents = 0.0;             ///< 音分偏差
    double confidence = 0.0;        ///< 置信度
    int tau = 0;                    ///< 选中的整数周期（样点）
};

/// 实时链路逐帧分析器。
class RealtimeRunner {
public:
    /// 以固定窗长与固定帧进逐帧检测（与上游实时循环同构）。
    ///
    /// @param data       单声道样点
    /// @param sampleRate 实际采样率（Hz）
    /// @param frame      窗长（样点）。上游实时为 4096；
    ///                   调用方可传 kFrameLadder 的各档做窗长对比实验
    /// @param hop        帧进（样点）。上游实时为 512（约 11.6 ms @44.1 kHz）
    /// @param cfg        引擎参数
    /// @param buffers    可复用工作缓冲；传 nullptr 时内部自建
    /// @return 逐帧结果（跳过无有效音高的帧）
    static std::vector<RealtimeFrame> run(std::span<const float> data,
                                          double sampleRate,
                                          std::size_t frame,
                                          std::size_t hop,
                                          const EngineConfig& cfg,
                                          EngineBuffers* buffers = nullptr);
};

} // namespace pitch

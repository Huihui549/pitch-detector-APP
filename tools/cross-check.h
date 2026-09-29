// 跨语言一致性对拍（HRN-1）
//
// 目的：证明 C++ 移植与上游 JS 引擎在同一段音频上给出**同一结果**（ADR-0003、verify.md B 组）。
// 这是本项目唯一能发现"看起来一样、数字不一样"的手段——上游每个常数都是从失败方案里实测
// 筛出来的，凭理解重写会静默降低准确度，只有逐帧比对能暴露。
//
// 真值来源（tools/gen-test-fixtures.mjs 用上游引擎生成）：
//   tests/data/reference.txt                  清单（key=value，纯 ASCII）
//   tests/data/<stem>.analysis.f64            文件分析逐帧真值，8 字段 × 8 字节 = 64 B/帧
//   tests/data/<stem>.analysis.note           文件分析逐帧音名，每行一个（ASCII）
//   tests/data/<stem>.realtime.f64            实时链路逐帧真值，6 字段 × 8 字节 = 48 B/帧
//   tests/data/<stem>.realtime.note           实时链路逐帧音名
//
// 字段顺序（与 gen-test-fixtures.mjs 的 FRAME_FIELDS / REALTIME_FIELDS 严格一致）：
//   analysis: timeSec, freqRaw, freq, cents, confidence, rms, frameSize, octaveFixed
//   realtime: offsetSamples, timeSec, freq, cents, confidence, tau
//
// 判定：数值在容差内 **且** 音名逐帧相同（verify.md B1/B2）。音名单独存文本列，
// 因为"标签碰巧对、频率差一个八度"这类假命中只能靠音名比对抓出来（上游 pitfalls #26）。

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace pitch {
namespace tools {

/// 对拍模式。
enum class CheckMode {
    Analysis,  ///< 文件分析：C++ 走级联窗长（与上游文件分析口径一致）
    Realtime,  ///< 实时链路：C++ 走级联窗长，另报与"上游固定 4096"的已知口径差异
};

/// 对拍选项。
struct CrossCheckOptions {
    std::string dataDir;                                        ///< 真值目录（必填）
    CheckMode mode = CheckMode::Analysis;                       ///< 模式
    std::vector<std::string> onlyFixtures;                      ///< 为空表示全部；否则只跑这些 stem
};

/// 单个指标的比对结果。
struct MetricReport {
    std::string name;          ///< 指标名
    bool relative = true;      ///< 是否按相对差判定
    double tol = 0.0;          ///< 容差
    double worst = 0.0;        ///< 最大差异（相对或绝对，取决于 relative）
    bool exceeded = false;     ///< 最大差异是否超容差
};

/// 单个素材的对拍结果。
struct FixtureReport {
    std::string stem;
    std::string wavFile;
    std::string expectNote;      ///< 期望音名（"none" 表示无有效音高）
    std::size_t cppFrames = 0;
    std::size_t refFrames = 0;
    std::size_t noteMismatch = 0;///< 音名不一致的帧数
    bool pass = false;
    std::string failReason;      ///< 非空表示结构性失败（读文件、帧数不符等）
    std::vector<MetricReport> metrics;
    /// 仅实时模式：C++ 级联窗长与上游固定 4096 的音名差异帧数（已知口径差异，非移植错误）
    std::size_t ladderVsFixedNoteDiff = 0;
    std::size_t ladderVsFixedTotal = 0;
};

/// 整次对拍的汇总。
struct CrossCheckSummary {
    std::size_t fixtures = 0;
    std::size_t passed = 0;
    bool ok = false;
};

/// 执行对拍并输出报告到 stdout。
CrossCheckSummary runCrossCheck(const CrossCheckOptions& options);

} // namespace tools
} // namespace pitch

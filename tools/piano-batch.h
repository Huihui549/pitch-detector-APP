// 钢琴 88 键素材批量分析（验收工具）—— 接口
//
// 目的：用**本项目的 C++ 引擎**逐个分析真实钢琴素材，给出可核对的命中报告。
// 这是上游 `tools/test-single-note.mjs` + `audio-test-report.md` 的 C++ 等价物——
// 上游的 84/88 基线就是这么量出来的；本项目要能复现同一口径。
//
// 判定口径（与上游一致，**双条件**，见 pitfalls #26）：
//   命中 = 众数音名正确 **且** |中位偏差| < 50 音分
//   只查音名会被"标签碰巧对、频率却差一个八度"的假命中混过去。
//
// 期望音名取自**文件名**（`tone (40) - C4.wav` → C4）：素材已由 tools/rename-piano.mjs
// 按"编号连续半音"补后缀，文件名即标准答案。
//
// 实现只用 C++ 标准库（与 cross-check / core-tests 一致，不依赖 Qt）。

#pragma once

#include <string>

namespace pitch {
namespace tools {

/// 批量分析选项。
struct PianoBatchOptions {
    std::string dir;      ///< 素材目录（必填）
    std::string csvPath;  ///< 可选：逐文件结果 CSV
    std::string mdPath;   ///< 可选：Markdown 报告
};

/// 执行批量分析并打印汇总。
/// @return 命中数；参数或路径错误时返回 -1
int runPianoBatch(const PianoBatchOptions& options);

} // namespace tools
} // namespace pitch

// 整段音高轨迹的八度一致性校正（后处理）
//
// 对应上游 tools/pitch-engine.js 的 `unifyOctaves`（L364-398）。
//
// 为什么需要：逐帧判据再准也会有"个别帧跳八度"。上游实测钢琴 C2 的延音段里，基频谷偶尔不够深，
// 该帧就选中半周期（报 C3，+1200 音分），于是一段单音被统计成"C2×473 帧 + C3×388 帧"。
// 单看每帧都有理，问题是**帧间不一致**：同一个持续音不该在八度之间来回跳。
//
// 本类无状态，只做纯函数式后处理（不持有任何跨段数据）。

#pragma once

#include "pitch-types.h"

#include <span>

namespace pitch {

/// 八度轨迹校正器。
class OctaveUnifier {
public:
    /// 就地校正 frames 中与整体参考相差整数倍的帧。
    ///
    /// 做法（两遍，与上游一致）：
    ///   ① 取"置信度加权中位数频率"作为参考——朴素中位在多数帧已跳错八度时会锁定错误值
    ///      （上游实测：正确帧置信度 0.97+、八度错帧多在 0.78–0.85，故按置信度加权）
    ///   ② 逐帧比较，凡与参考相差"接近整数倍频率"（±1200 / ±2400 音分，容差 ±120 音分）的帧，
    ///      按倍数折回到参考附近，并标记 octaveFixed
    ///
    /// 只动整数倍关系的帧，不动其他音高关系——真实滑音/换音不受影响。
    ///
    /// **调用方责任**：折回改的是 freq，之后必须重算音名与音分等派生字段，
    /// 否则统计会出现"同一段既有 C2 又有 C3"的假混合（上游 pitfalls #28，实测 42 vs 36 个混合文件）。
    ///
    /// @param frames 就地修改的帧序列（写入 freqRaw / freq / octaveFixed）
    /// @return 被折回的帧数
    static int apply(std::span<Frame> frames);

private:
    /// 置信度权重下限：低于此值仍按 0.05 计，避免零权重帧被完全忽略。
    static constexpr double kMinWeight = 0.05;
    /// 默认置信度（帧未给出置信度时使用）。
    static constexpr double kDefaultConfidence = 0.5;
    /// 整数倍判定容差（音分）。取 ±120：只吸收"明显是整数倍"的离群帧，接近的音高关系不碰。
    static constexpr double kOctaveToleranceCents = 120.0;
};

} // namespace pitch

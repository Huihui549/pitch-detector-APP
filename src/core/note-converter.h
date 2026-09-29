// 频率 ↔ 十二平均律音名/音分 换算
//
// 对应上游 tools/pitch-engine.js 的 `freqToNote`（L57-67）与 `NOTE_NAMES`（L29）。
// 约定（不可改，属上游冻结项）：科学音高记号 SPN，中央 C = C4；界面不提供唱名切换。
//
// 换算只用双精度数学，不查表、不近似——两端（C++/JS）必须给出同一音名与同一音分。

#pragma once

#include "pitch-types.h"

namespace pitch {

/// 音乐换算工具集。
class NoteConverter {
public:
    /// 频率 → 音名/八度/音分偏差（十二平均律）。
    ///
    /// 实现与上游 L57-67 逐行一致：
    ///   n = 69 + 12·log2(freq / ref)   （MIDI 音号，A4 = 69）
    ///   最近音级 = round(n)；音名 = NOTE_NAMES[最近音级 mod 12]；八度 = floor(最近音级/12) − 1
    ///   音分偏差 = (n − 最近音级) × 100
    ///
    /// @param freq  频率（Hz），须 > 0
    /// @param refA4 A4 基准（默认 440）；乐器按 442 定音时由界面传入
    static NoteInfo fromFrequency(double freq, double refA4 = kDefaultA4);

    /// 音名 + 八度 → 显示用字符串（如 "A4"、"C#3"）。
    /// 使用函数内静态缓冲轮转，**返回的指针在下一次调用后可能被覆盖**，
    /// 需同时持有多于 4 个音名时请立即复制成 std::string。
    static const char* format(int noteIndex, int octave);

private:
    /// MIDI 音号（可为小数）→ 音名/八度/音分偏差。供 fromFrequency 复用，不对外暴露
    /// （外部没有"已知 MIDI 音号"的输入场景，属 YAGNI）。
    static NoteInfo fromMidi(double midi);
};

} // namespace pitch

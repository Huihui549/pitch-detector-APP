// 点击音生成与混音 —— **纯逻辑，零 Qt 依赖**
//
// 两种来源，同一套接口：
//   · 内置合成音：由若干参数（主频/衰减/噪声/第二分音）定义，不依赖任何素材文件，
//     因此程序"开箱即有节拍声"，也不会引入素材授权问题。
//   · 自定义样本：用户上传的音频（已解码为单声道 float）。此时忽略合成参数，直接放样本。
//
// 为什么把"合成"也做成可测的纯函数：
//   点击声好不好听无法自动判断，但**它有没有按预期出现、幅度多大、长度多长、是否削顶**
//   都可以数值核对（见 tests/core/core-tests.cpp 与 `--metrocheck`）。
//   噪声用固定种子的 xorshift 生成：保证同样的输入永远渲染出同样的样点（可复现，才能写断言）。
//
// 编码要求（坑 A20）：本文件必须存为 UTF-8 **无 BOM**。

#pragma once

#include <cstddef>
#include <vector>

namespace pitch {

/// 内置合成音的参数字集合。
struct ClickTone {
    double freqHz = 1046.5;      ///< 主频（A6 附近，短促清脆）
    double decayMs = 28.0;       ///< 指数衰减时间常数 τ
    double noise = 0.12;         ///< 噪声占比（0=纯音；一点噪声让它像"敲击"而不是"电子滴"）
    double gain = 0.8;           ///< 线性增益
    double partialRatio = 2.4;   ///< 第二分音相对主频的倍数（给点击加"木质"感）
    double partialGain = 0.35;   ///< 第二分音增益
};

/// 一种音色：默认用内置合成音；加载了自定义样本后改用样本。
struct ClickVoice {
    ClickTone tone{};
    /// 自定义样本：单声道、取值约在 [-1, 1]；**为空表示使用内置合成音**
    std::vector<float> sample{};
    double sampleRate = 0.0;     ///< 自定义样本的采样率（与输出采样率不同则线性重采样）
    double gain = 1.0;           ///< 该角色的额外增益（强拍 1.0 / 弱拍 0.8 / 细分 0.5）
    /// 自定义样本的播放上限（秒）。节拍器只需要短促的一声，长音频必须截断，否则会糊成一片。
    double maxSeconds = 1.0;

    bool usesSample() const { return !sample.empty() && sampleRate > 0.0; }
};

/// 三种角色的**目标峰值**（强拍 > 弱拍 > 细分）。
///
/// 为什么要显式定义"单次点击的峰值"：基音与第二分音叠加后峰值可达 (1+partialGain) 倍，
/// 若把 gain 直接给 1.0，单次点击就会超过 1.0，被输出端的硬夹紧削顶（实测峰值 1.16 → 可听的失真）。
/// 归一化的对象是**单次点击**；多次点击重叠后的和超出 1.0 属正常，由输出端的总增益与夹紧处理。
/// role 越界按"弱拍"处理。
double rolePeakTarget(int role);

/// 三种角色的内置音色（0=强拍 1=弱拍 2=细分）。role 越界按"弱拍"处理。
/// 返回值已按 rolePeakTarget() 归一化（voice.gain = 1.0，峰值即目标值）。
ClickVoice builtinVoice(int role);

/// 一次点击的总长度（样点数）。合成音由衰减时间决定；样本由样本长度与 maxSeconds 取小。
std::size_t clickFrames(const ClickVoice& v, double outRate);

/// 把点击的第 [cursor, cursor+frames) 段**叠加**进 out。
///
/// 用"按游标切片"而不是"整段渲染"：节拍间隔可能小于点击长度（例如 300 BPM 下的十六分细分），
/// 于是多次点击会在同一条输出流里重叠——渲染器只需逐块推进游标，重叠自然靠叠加实现。
///
/// @param out        输出缓冲（单声道）
/// @param frames     out 的可用样点数
/// @param cursor     本次要从点击的第几个样点开始（0 = 点击起点）
/// @return 实际写入的样点数（越界部分被忽略，返回 0 表示这次点击已经放完）
std::size_t mixClickSlice(const ClickVoice& v, double outRate, std::size_t cursor, float* out,
                          std::size_t frames);

} // namespace pitch

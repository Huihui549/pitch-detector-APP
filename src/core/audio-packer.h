// 单声道渲染结果 → 设备缓冲的打包 —— **纯逻辑，零 Qt 依赖**
//
// 为什么把这段从音频层搬到 core：
//   它是"最终输出波形"的最后一环（总增益、声道复制、格式转换、**硬夹紧**），
//   而"会不会削顶/会不会交错错位/格式对不对"恰恰是**必须能被自动验证**的属性。
//   放在 Qt 侧就只能靠听，放在 core 里 `--metrocheck` 可以直接喂越界信号去测。
//
// 编码要求（坑 A20）：本文件必须存为 UTF-8 **无 BOM**。

#pragma once

#include <cstddef>

namespace pitch {

/// 设备样点格式（只支持这两类：覆盖 Windows/Android 上会遇到的默认输出格式）
enum class PackFormat {
    Float32,
    Int16,
};

/// 总增益：多路点击重叠后仍要给硬夹紧留余量（节拍器要"准且干净"，不需要"响"）
constexpr float kOutputMasterGain = 0.8f;

/// 把单声道 float 渲染结果打包为**交错**的设备缓冲。
///
/// 行为（逐条可测）：
///   · 每个样点先乘 gain，再**硬夹紧**到 [-1, 1]（防止叠加过载后回绕成刺耳的爆音）
///   · channels = 1 直接写；channels = 2 复制成左右两声道（本引擎不产生立体声差异）
///   · outBytes 不足时**不写任何数据**并返回 0（宁可静音，也不写半截缓冲）
///
/// @return 实际写入的字节数；0 表示参数非法或缓冲不足
std::size_t packMono(const float* mono, std::size_t frames, int channels, PackFormat format,
                     float gain, char* out, std::size_t outBytes);

} // namespace pitch

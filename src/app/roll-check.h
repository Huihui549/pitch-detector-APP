// 钢琴卷帘离线自检（`--rollcheck`）
//
// 为什么要它：卷帘是"画得对不对"的问题，肉眼看界面之外没有别的判据，
// 而我又读不了图——所以必须把"能不能画对"变成**可断言的像素事实**：
//   · 音频 → 帧 → 几何 → 渲染 → PNG 这条链路能跑通（用的就是界面与导出走的同一份实现）
//   · 键盘列里白键行确实白、黑键行确实黑（否则说明键位素材没贴上或贴错了行）
//   · 绘图区里确实出现了强调色（曲线画出来了）
//   · 时间轴上确实有文字（横坐标标注出来了）
//   · 整图不是一片纯色（防"渲染成功但其实是空图"这种最隐蔽的失败）
//
// 编码要求（坑 A20）：本文件必须存为 UTF-8 **无 BOM**。

#pragma once

#include <QString>

namespace pitch {

/// 运行钢琴卷帘自检：解码 → 分析 → 渲染 → 断言 → 保存 PNG。
/// @param audioPath 输入音频（任意主流格式）
/// @param pngOut    输出 PNG 路径（空则写到当前目录下的 piano-roll-check.png）
/// @return 0 = 全部通过；非 0 = 有断言失败
int runRollCheck(const QString& audioPath, const QString& pngOut);

} // namespace pitch

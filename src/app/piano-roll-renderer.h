// 钢琴卷帘渲染（左侧键盘图 + 右侧音高曲线 + 底部时间轴）
//
// 为什么要 **C++ 侧画**而不是 QML 里画：
//   ① 同一张图既要给界面看、又要能导出成"长图片"，两套绘制逻辑必然会漂移；
//      这里只留一份实现，界面通过 QQuickImageProvider 取图、导出直接把同一张图存 PNG（所见即所得）。
//   ② 导出图可能很长（几万像素宽），QML 的 Canvas/grabToImage 在这种尺寸下既慢又有纹理尺寸上限。
//   ③ 纯 QImage + QPainter 可以在**无界面**环境下渲染，于是导出与卷帘本身都能被自动验证（像素核对）。
//
// 键盘列用**真实矢量素材**（`resources/piano/key-white.svg` / `key-black.svg`）逐行贴，
// 不用符号或纯色块拼——那样在放大后会明显"假"。
//
// 分层：src/app（用 QtGui；不依赖 QML，便于单测/自检调用）。帧数据来自 src/core 的 `Frame`。
//
// 编码要求（坑 A20）：本文件必须存为 UTF-8 **无 BOM**。

#pragma once

#include "pitch-types.h"

#include <QColor>
#include <QImage>
#include <QString>

#include <vector>

namespace pitch {

/// 卷帘配色。渲染器**不认识主题**，只认颜色——由调用方从 ThemeProvider 折算后传入，
/// 这样主题令牌仍是唯一来源（QML 侧的颜色规则同样适用于这里）。
struct RollPalette {
    QColor background;      ///< 卷帘底
    QColor rowAlt;          ///< 黑键行底纹（让黑白键行一眼可分）
    QColor gridLine;        ///< 半音网格
    QColor gridStrong;      ///< 八度线（C 行）
    QColor axisText;        ///< 坐标文字
    QColor axisLine;        ///< 坐标轴与分隔线
    QColor curve;           ///< 音高曲线
    QColor curveGlow;       ///< 曲线外发光（弱化，只为在深底上读得清）
    QColor surface;         ///< 键盘列底色（键与键之间的缝）
    QColor text;            ///< 键盘上的音名文字（白键行）
    /// 黑键上的音名文字。键面素材是**固定深色**（与主题无关），故这里的文字恒为浅色。
    QColor keyLabelOnDark;
};

// 说明：这里**不提供** dark()/light() 的写死配色——那会成为"第二处颜色定义"，
/// 与"颜色只能来自 src/app/theme.h 令牌"的铁律冲突（check-theme 门禁管的是 QML，但原则相同）。
/// 调用方一律用下面的 rollPaletteFromTheme() 现算。

/// 从主题令牌折算出一套卷帘配色（唯一入口）。声明放在这里、定义在 .cpp 里，
/// 使渲染器本身不依赖 theme.h（便于单独复用/测试）。
class ThemeProvider;   ///< 前置声明即可（实现文件里才需要完整定义）
RollPalette rollPaletteFromTheme(const ThemeProvider& theme);

/// 卷帘几何。界面显示与导出**共用同一套计算**，避免"屏幕上和导出图不一样"。
struct RollGeometry {
    int widthPx = 0;            ///< 整图宽
    int heightPx = 0;           ///< 整图高（含底部时间轴）
    int keyboardWidthPx = 0;    ///< 左侧键盘列宽
    double pxPerSemitone = 0.0; ///< 每个半音的行高
    double pxPerSecond = 0.0;   ///< 时间方向的比例
    int lowestMidi = 21;        ///< 覆盖的最低 MIDI（A0 = 21）
    int highestMidi = 108;      ///< 覆盖的最高 MIDI（C8 = 108）
    double durationSec = 0.0;   ///< 时长（秒）
    int axisHeightPx = 20;      ///< 底部时间轴高度
};

/// 计算几何。
///
/// @param maxWidthPx 整图宽度上限：超长录音会被自动压缩（降低 pxPerSecond），而不是画出一张
///                   几万像素宽、内存上百 MB 的图（长图导出最容易在这里踩坑）。
RollGeometry computeRollGeometry(int lowestMidi, int highestMidi, double durationSec, int maxWidthPx,
                                 double preferredPxPerSecond, int pxPerSemitone, int keyboardWidthPx,
                                 int axisHeightPx);

/// 帧序列覆盖的 MIDI 范围（含上下各留 1 个半音；空数据时退到 A0..C8）。
void midiRangeOf(const std::vector<Frame>& frames, double refA4, int* lowestMidi, int* highestMidi);

/// 帧序列的时长（秒）。帧是**稀疏**的（只在检测到音高时才有帧），
/// 故末尾再补一个帧进（10 ms），否则最后半帧的曲线会被切掉。
double durationOf(const std::vector<Frame>& frames);

/// 渲染整张卷帘。
QImage renderPianoRoll(const std::vector<Frame>& frames, const RollGeometry& geom,
                       const RollPalette& palette, double refA4 = 440.0);

/// 存成 PNG。失败时把原因写进 errorOut。
bool saveRollPng(const QImage& image, const QString& path, QString* errorOut);

} // namespace pitch

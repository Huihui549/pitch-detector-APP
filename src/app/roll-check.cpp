#include "roll-check.h"

#include "analysis-runner.h"
#include "audio-file-decoder.h"
#include "note-converter.h"
#include "piano-roll-renderer.h"
#include "theme.h"

#include <QFileInfo>
#include <QImage>
#include <QTextStream>

#include <cmath>

namespace pitch {
namespace {

int g_checks = 0;
int g_failures = 0;

void check(QTextStream& out, bool ok, const QString& what) {
    ++g_checks;
    if (ok) {
        return;
    }
    ++g_failures;
    out << "  [FAIL] " << what << "\n";
}

/// 像素亮度（0..255，按 Rec.601 近似）
int luminance(QColor c) {
    return (299 * c.red() + 587 * c.green() + 114 * c.blue()) / 1000;
}

/// 统计与目标色接近的像素数（容差按通道给，避免抗锯齿边缘被算进去）
int countNear(const QImage& image, const QRect& area, QColor target, int tol) {
    int hit = 0;
    for (int y = area.top(); y <= area.bottom(); ++y) {
        for (int x = area.left(); x <= area.right(); ++x) {
            if (x < 0 || y < 0 || x >= image.width() || y >= image.height()) {
                continue;
            }
            const QColor c = image.pixelColor(x, y);
            if (std::abs(c.red() - target.red()) <= tol && std::abs(c.green() - target.green()) <= tol &&
                std::abs(c.blue() - target.blue()) <= tol) {
                ++hit;
            }
        }
    }
    return hit;
}

} // namespace

int runRollCheck(const QString& audioPath, const QString& pngOut) {
    QTextStream out(stdout);
    g_checks = 0;
    g_failures = 0;

    out << "===== 钢琴卷帘离线自检 =====\n";
    if (audioPath.isEmpty() || !QFileInfo::exists(audioPath)) {
        out << "  [FAIL] 输入音频不存在：" << audioPath << "\n";
        return 1;
    }

    // ---------- 1. 解码 ----------
    const DecodedAudio decoded = decodeAudioFile(audioPath);
    out << "解码：" << (decoded.ok ? "成功" : "失败") << " ｜ 路径=" << decoded.route
        << " ｜ 样点=" << static_cast<qlonglong>(decoded.samples.size())
        << " ｜ 采样率=" << decoded.sampleRate << " Hz\n";
    check(out, decoded.ok, QStringLiteral("音频应能解码：%1").arg(decoded.error));
    if (!decoded.ok) {
        return 1;
    }

    // ---------- 2. 分析（与文件分析页同一条链路：帧进 441 样点）----------
    const EngineConfig cfg;
    const Analysis analysis = AnalysisRunner::analyze(
        std::span<const float>(decoded.samples.data(), decoded.samples.size()), decoded.sampleRate,
        441, cfg, nullptr, nullptr);
    out << "分析：帧数=" << analysis.frames.size() << "（帧进 10 ms）｜ 峰值 RMS="
        << analysis.summary.peakRms << " ｜ 静音门槛=" << analysis.summary.rmsFloor << "\n";
    check(out, !analysis.frames.empty(), QStringLiteral("应产生至少一帧有效音高"));
    if (analysis.frames.empty()) {
        return 1;
    }

    // ---------- 3. 几何 ----------
    int lowest = 0;
    int highest = 0;
    midiRangeOf(analysis.frames, kDefaultA4, &lowest, &highest);
    const double duration = durationOf(analysis.frames);
    const RollGeometry geom = computeRollGeometry(lowest, highest, duration, 12000, 130.0, 15, 52, 22);
    const int expectedHeight = (highest - lowest + 1) * 15 + 22;
    out << "几何：音域 MIDI " << lowest << "–" << highest << "（" << (highest - lowest + 1)
        << " 个半音）｜ 时长 " << duration << " s ｜ 图 " << geom.widthPx << "×" << geom.heightPx
        << " ｜ 键盘列 " << geom.keyboardWidthPx << " px ｜ 时间比例 " << geom.pxPerSecond << " px/s\n";
    check(out, geom.heightPx == expectedHeight,
          QStringLiteral("高度应为「半音数 × 行高 + 时间轴高」：实测 %1，期望 %2")
              .arg(geom.heightPx)
              .arg(expectedHeight));
    check(out, geom.widthPx > geom.keyboardWidthPx,
          QStringLiteral("宽度应大于键盘列宽（绘图区非空）"));
    check(out, geom.widthPx <= 12000, QStringLiteral("宽度不得超过上限（防超长录音撑爆内存）"));

    // ---------- 4. 渲染 ----------
    ThemeProvider theme;   // 自检用默认（深色）主题；界面用的是同一个 ThemeProvider 实例
    const RollPalette palette = rollPaletteFromTheme(theme);
    const QImage image = renderPianoRoll(analysis.frames, geom, palette, kDefaultA4);
    out << "渲染：" << image.width() << "×" << image.height()
        << " ｜ 图像格式=" << image.format() << "\n";
    check(out, !image.isNull(), QStringLiteral("图像应渲染成功"));
    if (image.isNull()) {
        return 1;
    }

    // ---------- 5. 像素断言 ----------
    // 5a. 键盘列：找一个白键行与一个黑键行，比较其亮度（白键素材亮、黑键素材暗）
    int whiteRowY = -1;
    int blackRowY = -1;
    for (int midi = lowest; midi <= highest; ++midi) {
        const int row = highest - midi;
        const int y = row * 15 + 7;
        const int semitone = ((midi % 12) + 12) % 12;
        const bool black = (semitone == 1 || semitone == 3 || semitone == 6 || semitone == 8 || semitone == 10);
        if (black && blackRowY < 0) {
            blackRowY = y;
        }
        if (!black && whiteRowY < 0) {
            whiteRowY = y;
        }
    }
    const int whiteLum = whiteRowY >= 0 ? luminance(image.pixelColor(geom.keyboardWidthPx - 6, whiteRowY)) : -1;
    const int blackLum = blackRowY >= 0 ? luminance(image.pixelColor(geom.keyboardWidthPx - 6, blackRowY)) : -1;
    out << "键盘列亮度：白键行=" << whiteLum << " ｜ 黑键行=" << blackLum << "（取样点 x="
        << (geom.keyboardWidthPx - 6) << "）\n";
    check(out, whiteLum > 180, QStringLiteral("白键行应接近白色（实测亮度 %1）").arg(whiteLum));
    check(out, blackLum >= 0 && blackLum < 90, QStringLiteral("黑键行应接近黑色（实测亮度 %1）").arg(blackLum));
    check(out, whiteLum > blackLum + 60, QStringLiteral("白键行必须明显亮于黑键行"));

    // 5b. 绘图区里应出现强调色（曲线）
    const QRect plotArea(geom.keyboardWidthPx, 0, geom.widthPx - geom.keyboardWidthPx,
                         geom.heightPx - geom.axisHeightPx);
    const int curvePixels = countNear(image, plotArea, palette.curve, 40);
    out << "绘图区里接近强调色的像素：" << curvePixels << "\n";
    check(out, curvePixels > 50, QStringLiteral("音高曲线应画出来（强调色像素 %1）").arg(curvePixels));

    // 5c. 时间轴上应有文字（与轴底色不同的像素）
    const QRect axisArea(geom.keyboardWidthPx, geom.heightPx - geom.axisHeightPx,
                         geom.widthPx - geom.keyboardWidthPx, geom.axisHeightPx);
    const int axisTextPixels = countNear(image, axisArea, palette.axisText, 40);
    out << "时间轴里接近文字色的像素：" << axisTextPixels << "\n";
    check(out, axisTextPixels > 20, QStringLiteral("时间轴刻度文字应画出来（像素 %1）").arg(axisTextPixels));

    // 5d. 整图不得是一片纯色（防"渲染成功但其实是空图"）
    QSet<QRgb> colors;
    for (int y = 0; y < image.height(); y += 3) {
        for (int x = 0; x < image.width(); x += 3) {
            colors.insert(image.pixel(x, y));
        }
    }
    out << "抽样颜色数：" << colors.size() << "\n";
    check(out, colors.size() > 20, QStringLiteral("图像内容应有变化（颜色数 %1）").arg(colors.size()));

    // ---------- 6. 保存 PNG（与"导出长图"走同一条保存实现）----------
    const QString path = pngOut.isEmpty() ? QStringLiteral("piano-roll-check.png") : pngOut;
    QString error;
    const bool saved = saveRollPng(image, path, &error);
    out << "保存 PNG：" << (saved ? path : error) << "\n";
    check(out, saved, QStringLiteral("PNG 应能保存"));

    out << "\n自检项：" << (g_checks - g_failures) << " / " << g_checks << " 通过\n";
    out << (g_failures == 0 ? "[PASS] 钢琴卷帘自检全部通过\n" : "[FAIL] 钢琴卷帘自检有失败项\n");
    out.flush();
    return g_failures == 0 ? 0 : 1;
}

} // namespace pitch

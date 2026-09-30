#include "piano-roll-renderer.h"

#include "note-converter.h"
#include "theme.h"

#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>

#include <algorithm>
#include <cmath>

namespace pitch {
namespace {

/// 曲线断开阈值（秒）：帧是稀疏的（只在检测到音高时才有帧），间隔超过它说明中间**没有音高**，
/// 必须断开而不是连成一段滑音——否则静音处会画出一条不存在的"滑音"（会让读数看起来像真的）。
constexpr double kGapBreakSec = 0.12;

/// 键盘列里贴的键位素材（相对 qrc 的路径）
constexpr const char* kWhiteKeySvg = ":/resources/piano/key-white.svg";
constexpr const char* kBlackKeySvg = ":/resources/piano/key-black.svg";

bool isBlackKey(int midi) {
    switch (((midi % 12) + 12) % 12) {
    case 1:
    case 3:
    case 6:
    case 8:
    case 10:
        return true;
    default:
        return false;
    }
}

/// 半音对应的行中心 y（MIDI 越大越靠上）。曲线按**连续 MIDI**定位，故这里只在需要取整时使用。
/// （当前实现直接在曲线里用 double 计算，这个函数保留给"画音符块"的后续需求——若长期不用可删。）
#if 0
double rowCenterY(int midi, const RollGeometry& geom) {
    return (static_cast<double>(geom.highestMidi - midi) + 0.5) * geom.pxPerSemitone;
}
#endif

/// 选一个"好看"的时间刻度间隔（秒），使相邻标签不至于挤在一起
double niceTickSeconds(double pxPerSecond, double minLabelPx) {
    const double candidates[] = {0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0};
    for (const double c : candidates) {
        if (c * pxPerSecond >= minLabelPx) {
            return c;
        }
    }
    return 1200.0;
}

/// 键位图缓存：同一尺寸的键只渲染一次（逐行贴图时才不会重复解析 SVG）
class KeyAssets {
public:
    void ensure(int widthPx, int heightPx, bool black) {
        if (m_width == widthPx && m_height == heightPx && m_black == black && !m_image.isNull()) {
            return;
        }
        m_width = widthPx;
        m_height = heightPx;
        m_black = black;
        m_image = QImage(widthPx, heightPx, QImage::Format_ARGB32_Premultiplied);
        m_image.fill(Qt::transparent);
        QSvgRenderer renderer(QString::fromLatin1(black ? kBlackKeySvg : kWhiteKeySvg));
        QPainter p(&m_image);
        p.setRenderHint(QPainter::Antialiasing, true);
        if (renderer.isValid()) {
            renderer.render(&p, QRectF(0, 0, widthPx, heightPx));
        } else {
            // 素材缺失时的兜底：画出可辨认的键形，而不是留空（同时不影响主流程）
            p.fillRect(QRectF(0, 0, widthPx, heightPx), black ? QColor(30, 34, 40) : QColor(240, 242, 245));
        }
        m_image.setDevicePixelRatio(1.0);
    }

    const QImage& image() const { return m_image; }

private:
    QImage m_image{};
    int m_width = -1;
    int m_height = -1;
    bool m_black = false;
};

} // namespace

RollPalette rollPaletteFromTheme(const ThemeProvider& theme) {
    RollPalette p;
    p.background = theme.background();
    p.rowAlt = theme.surfaceAlt();
    p.gridLine = theme.border();
    p.gridStrong = theme.textDim();
    p.axisText = theme.textDim();
    p.axisLine = theme.border();
    p.curve = theme.accent();
    p.curveGlow = theme.accentDim();
    p.surface = theme.surface();
    p.text = theme.text();
    // 键面素材是固定深色（不随主题变），故黑键上的文字恒为浅色——这是**唯一**一处与主题无关的颜色，
    // 且只在这里定义一次（不在别处再写一个浅色常量）。
    p.keyLabelOnDark = QColor(QStringLiteral("#eef2f6"));
    return p;
}

RollGeometry computeRollGeometry(int lowestMidi, int highestMidi, double durationSec, int maxWidthPx,
                                 double preferredPxPerSecond, int pxPerSemitone, int keyboardWidthPx,
                                 int axisHeightPx) {
    RollGeometry g;
    g.lowestMidi = std::clamp(lowestMidi, 0, 127);
    g.highestMidi = std::clamp(highestMidi, g.lowestMidi, 127);
    g.pxPerSemitone = std::max(1, pxPerSemitone);
    g.keyboardWidthPx = std::max(1, keyboardWidthPx);
    g.axisHeightPx = std::max(1, axisHeightPx);
    g.durationSec = std::max(0.1, durationSec);

    // 行高固定，高度由音域决定；宽度先按"理想比例"算，再按上限压缩
    const int rows = g.highestMidi - g.lowestMidi + 1;
    g.heightPx = rows * static_cast<int>(g.pxPerSemitone) + g.axisHeightPx;

    const int plotBudget = std::max(1, maxWidthPx - g.keyboardWidthPx);
    double pxPerSecond = preferredPxPerSecond;
    if (g.durationSec * pxPerSecond > static_cast<double>(plotBudget)) {
        pxPerSecond = static_cast<double>(plotBudget) / g.durationSec;
    }
    g.pxPerSecond = std::max(0.01, pxPerSecond);
    g.widthPx = g.keyboardWidthPx +
                static_cast<int>(std::ceil(g.durationSec * g.pxPerSecond)) + 1;   // +1：末尾刻线不被切
    return g;
}

void midiRangeOf(const std::vector<Frame>& frames, double refA4, int* lowestMidi, int* highestMidi) {
    int lo = 127;
    int hi = 0;
    for (const Frame& f : frames) {
        if (!(f.freq > 0.0)) {
            continue;
        }
        const double midi = NoteConverter::fromFrequency(f.freq, refA4).midi;
        lo = std::min(lo, static_cast<int>(std::floor(midi)));
        hi = std::max(hi, static_cast<int>(std::ceil(midi)));
    }
    if (lo > hi) {
        lo = 21;    // A0
        hi = 108;   // C8
    }
    // 上下各留一个半音，曲线不会贴边
    *lowestMidi = std::max(0, lo - 1);
    *highestMidi = std::min(127, hi + 1);
}

double durationOf(const std::vector<Frame>& frames) {
    double last = 0.0;
    for (const Frame& f : frames) {
        last = std::max(last, f.timeSec);
    }
    return last + 0.01;   // 帧进 10 ms：补上最后一帧自身的长度
}

QImage renderPianoRoll(const std::vector<Frame>& frames, const RollGeometry& geom,
                       const RollPalette& palette, double refA4) {
    if (geom.widthPx <= 0 || geom.heightPx <= 0) {
        return {};
    }
    QImage image(geom.widthPx, geom.heightPx, QImage::Format_ARGB32_Premultiplied);
    image.fill(palette.background);
    image.setDevicePixelRatio(1.0);

    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const double plotTop = 0.0;
    const double plotBottom = geom.heightPx - geom.axisHeightPx;
    const double plotLeft = geom.keyboardWidthPx;
    const double plotRight = geom.widthPx;

    // ---------- 1. 行底纹（黑键行加深）----------
    for (int midi = geom.lowestMidi; midi <= geom.highestMidi; ++midi) {
        if (!isBlackKey(midi)) {
            continue;
        }
        const double y = (static_cast<double>(geom.highestMidi - midi)) * geom.pxPerSemitone;
        p.fillRect(QRectF(plotLeft, y, plotRight - plotLeft, geom.pxPerSemitone), palette.rowAlt);
    }

    // ---------- 2. 半音网格 + 八度线（C 行加粗）----------
    for (int midi = geom.lowestMidi; midi <= geom.highestMidi + 1; ++midi) {
        const double y = (static_cast<double>(geom.highestMidi - midi + 1)) * geom.pxPerSemitone;
        const bool octaveLine = (midi % 12) == 0;
        QPen pen(octaveLine ? palette.gridStrong : palette.gridLine);
        pen.setWidthF(octaveLine ? 1.2 : 0.6);
        if (!octaveLine) {
            pen.setStyle(Qt::DotLine);
        }
        p.setPen(pen);
        p.drawLine(QPointF(plotLeft, y), QPointF(plotRight, y));
    }

    // ---------- 3. 时间刻度（横轴=时间，单位秒）----------
    const double tick = niceTickSeconds(geom.pxPerSecond, 46.0);
    QFont axisFont = p.font();
    axisFont.setPixelSize(std::clamp(geom.axisHeightPx - 8, 8, 12));
    p.setFont(axisFont);
    const QFontMetrics axisMetrics(axisFont);

    p.fillRect(QRectF(plotLeft, plotBottom, plotRight - plotLeft, geom.axisHeightPx), palette.surface);
    for (double t = 0.0; t <= geom.durationSec + 1e-9; t += tick) {
        const double x = plotLeft + t * geom.pxPerSecond;
        QPen pen(palette.gridLine);
        pen.setStyle(Qt::DashLine);
        pen.setWidthF(0.6);
        p.setPen(pen);
        p.drawLine(QPointF(x, plotTop), QPointF(x, plotBottom));

        p.setPen(palette.axisText);
        const QString label = QStringLiteral("%1s").arg(t >= 100 ? QString::number(std::lround(t))
                                                                 : QString::number(t, 'g', 4));
        p.drawText(QRectF(x + 2, plotBottom, 60, geom.axisHeightPx),
                   Qt::AlignLeft | Qt::AlignVCenter, label);
    }
    p.setPen(QPen(palette.axisLine, 1.0));
    p.drawLine(QPointF(plotLeft, plotBottom), QPointF(plotRight, plotBottom));

    // ---------- 4. 音高曲线（间隔大处断开，不画假滑音）----------
    QPainterPath glow;
    QPainterPath curve;
    bool started = false;
    double lastTime = -1e9;
    for (const Frame& f : frames) {
        if (!(f.freq > 0.0)) {
            continue;
        }
        const double midi = NoteConverter::fromFrequency(f.freq, refA4).midi;
        const double x = plotLeft + f.timeSec * geom.pxPerSecond;
        const double y = (static_cast<double>(geom.highestMidi) - midi + 0.5) * geom.pxPerSemitone;
        if (!started || (f.timeSec - lastTime) > kGapBreakSec) {
            curve.moveTo(x, y);
            glow.moveTo(x, y);
            started = true;
        } else {
            curve.lineTo(x, y);
            glow.lineTo(x, y);
        }
        lastTime = f.timeSec;
    }
    p.setBrush(Qt::NoBrush);
    QPen glowPen(palette.curveGlow);
    glowPen.setWidthF(std::max(3.0, geom.pxPerSemitone * 0.55));
    glowPen.setCapStyle(Qt::RoundCap);
    glowPen.setJoinStyle(Qt::RoundJoin);
    p.setPen(glowPen);
    p.drawPath(glow);

    QPen curvePen(palette.curve);
    curvePen.setWidthF(std::max(1.4, geom.pxPerSemitone * 0.28));
    curvePen.setCapStyle(Qt::RoundCap);
    curvePen.setJoinStyle(Qt::RoundJoin);
    p.setPen(curvePen);
    p.drawPath(curve);

    // ---------- 5. 左侧键盘列（真实键位素材逐行贴）----------
    KeyAssets whiteKeys;
    KeyAssets blackKeys;
    const int whiteH = std::max(2, static_cast<int>(std::lround(geom.pxPerSemitone)));
    const int blackH = std::max(2, static_cast<int>(std::lround(geom.pxPerSemitone * 0.78)));
    const int blackW = std::max(4, static_cast<int>(std::lround(geom.keyboardWidthPx * 0.62)));

    p.fillRect(QRectF(0, 0, geom.keyboardWidthPx, plotBottom), palette.surface);
    for (int midi = geom.lowestMidi; midi <= geom.highestMidi; ++midi) {
        const double y = (static_cast<double>(geom.highestMidi - midi)) * geom.pxPerSemitone;
        if (isBlackKey(midi)) {
            continue;   // 黑键在白键之后统一贴，保证压在白键之上
        }
        whiteKeys.ensure(geom.keyboardWidthPx, whiteH, false);
        p.drawImage(QRectF(0, y, geom.keyboardWidthPx, geom.pxPerSemitone), whiteKeys.image());
    }
    for (int midi = geom.lowestMidi; midi <= geom.highestMidi; ++midi) {
        if (!isBlackKey(midi)) {
            continue;
        }
        const double y = (static_cast<double>(geom.highestMidi - midi)) * geom.pxPerSemitone;
        // 黑键偏上贴：视觉上更接近真实琴键（黑键夹在两个白键之间）
        const double offset = (geom.pxPerSemitone - blackH) * 0.5;
        blackKeys.ensure(blackW, blackH, true);
        p.drawImage(QRectF(geom.keyboardWidthPx - blackW, y + offset, blackW, blackH),
                    blackKeys.image());
    }

    // ---------- 6. 键盘上的音名（C 行必标；行高够大时每行都标）----------
    QFont keyFont = p.font();
    keyFont.setPixelSize(std::clamp(static_cast<int>(geom.pxPerSemitone * 0.58), 8, 11));
    p.setFont(keyFont);
    const bool labelAll = geom.pxPerSemitone >= 18.0;
    for (int midi = geom.lowestMidi; midi <= geom.highestMidi; ++midi) {
        const bool isC = (midi % 12) == 0;
        if (!isC && !labelAll) {
            continue;
        }
        const double y = (static_cast<double>(geom.highestMidi - midi)) * geom.pxPerSemitone;
        // SPN 记号：八度 = floor(midi/12) − 1（与 NoteConverter 的约定一致，直接算比反推频率更稳）
        const int noteIndex = ((midi % 12) + 12) % 12;
        const int octave = static_cast<int>(std::floor(static_cast<double>(midi) / 12.0)) - 1;
        p.setPen(isBlackKey(midi) ? palette.keyLabelOnDark : palette.text);
        p.drawText(QRectF(3, y, geom.keyboardWidthPx - 6, geom.pxPerSemitone),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QString::fromLatin1(NoteConverter::format(noteIndex, octave)));
    }

    // 键盘列与绘图区的分隔线
    p.setPen(QPen(palette.axisLine, 1.0));
    p.drawLine(QPointF(geom.keyboardWidthPx, 0), QPointF(geom.keyboardWidthPx, plotBottom));

    p.end();
    return image;
}

bool saveRollPng(const QImage& image, const QString& path, QString* errorOut) {
    if (image.isNull()) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("没有可导出的图像（先分析或录制一段音频）");
        }
        return false;
    }
    // 目标目录不存在时先建（用户在保存对话框里手打新目录是常见操作）
    const QFileInfo info(path);
    if (!info.absolutePath().isEmpty()) {
        QDir().mkpath(info.absolutePath());
    }
    if (!image.save(path, "PNG")) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("写入失败：%1（目录不可写或磁盘空间不足）").arg(path);
        }
        return false;
    }
    if (errorOut != nullptr) {
        errorOut->clear();
    }
    return true;
}

} // namespace pitch

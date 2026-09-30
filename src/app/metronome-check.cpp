#include "metronome-check.h"

#include "audio-packer.h"
#include "click-voice.h"
#include "metronome-pattern.h"
#include "metronome-renderer.h"
#include "tap-tempo.h"
#include "wav-reader.h"

#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace pitch {
namespace {

constexpr double kRate = 48000.0;
constexpr int kBlockA = 512;
constexpr int kBlockB = 4096;

/// 起点检出的上升沿门限（包络在 2 ms 内的增量）。
/// 取值依据：任意角色的点击在 0.4 ms 攻击段内就能涨到 0.45–0.95，故 0.15 足够稳；
/// 而前一次点击的拖尾是**缓慢衰减**的，2 ms 内增量远小于它 → 不会误判。
constexpr float kRiseDelta = 0.15f;
/// 包络底噪门限：低于它的"上升"不算点击（避免把数值噪声当事件）
constexpr float kOnsetLevel = 0.02f;
/// 起点去抖（毫秒）：比它更近的两个起点无法分辨；测试里最近的事件间隔是 62.5 ms
constexpr double kRefractoryMs = 5.0;
/// 起点相对期望位置的**绝对**偏差上限（毫秒）：攻击段 + 重叠污染都远小于它
constexpr double kOnsetToleranceMs = 3.0;
/// **同角色**起点偏差的极差上限（样点）：判定"采样级精确"的核心指标。
///
/// 为什么按角色分组：不同音色的频率与峰值不同，检出的"起点"会有几样点的固定差
/// （攻击段越过阈值的时间不同，实测 3–9 样点）；把不同角色混在一起比，量到的是**音色差异**，
/// 而不是调度误差。同一角色波形完全一致，其偏差必须几乎相同——这才是"有没有抖动/漂移"的证据。
constexpr double kJitterSamples = 2.0;

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

void checkNear(QTextStream& out, double actual, double expected, double tol, const QString& what) {
    const bool ok = std::fabs(actual - expected) <= tol;
    ++g_checks;
    if (ok) {
        return;
    }
    ++g_failures;
    out << "  [FAIL] " << what << "：实测 " << actual << "，期望 " << expected << "（容差 " << tol
        << "）\n";
}

/// 渲染一段并返回样点。
std::vector<float> renderPattern(const Pattern& pattern, int bpm, int blockSize, int bars,
                                 MetronomeRenderer* rendererOut = nullptr) {
    MetronomeRenderer renderer;
    renderer.setSampleRate(kRate);
    renderer.setBpm(bpm);
    renderer.setPattern(pattern);
    const double perBeat = renderer.samplesPerBeat();
    const double total =
        perBeat * static_cast<double>(normalize(pattern).meter.beats) * static_cast<double>(bars);
    const std::size_t frames =
        static_cast<std::size_t>(std::ceil(total)) + static_cast<std::size_t>(kRate);
    std::vector<float> out(frames, 0.0f);
    std::size_t done = 0;
    while (done < frames) {
        const std::size_t take = std::min(static_cast<std::size_t>(blockSize), frames - done);
        renderer.render(out.data() + done, take);
        done += take;
    }
    if (rendererOut != nullptr) {
        *rendererOut = renderer;
    }
    return out;
}

/// "渲染了 frames 个样点"这一前提下的**完整**期望：起始样点与各角色次数。
///
/// 为什么要按 frames 反推而不是按"渲染几小节"写死：渲染长度总会多留一段尾巴用于收尾，
/// 那段时间里下一小节的头几个点击**已经发声**（实测：2 小节 + 1 秒尾巴 → 每个角色各多 1 次），
/// 写死"2 小节"的期望就会把这些真实发生的点击判成错误。
struct Expectation {
    std::vector<std::size_t> onsets;
    std::vector<int> roles;   ///< 与 onsets 一一对应（ClickRole 的整数值）
    int roleCount[MetronomeRenderer::kRoleCount] = {0, 0, 0};
};

Expectation expectationWithin(const Pattern& pattern, double samplesPerBeat, std::size_t frames) {
    const Pattern n = normalize(pattern);
    const std::vector<ClickEvent> events = buildEvents(n);
    const double barBeats = static_cast<double>(n.meter.beats);

    Expectation e;
    for (int bar = 0; bar < 4096; ++bar) {
        bool any = false;
        for (const ClickEvent& ev : events) {
            const double beatPos = barBeats * static_cast<double>(bar) + ev.beatPosition;
            const auto onset = static_cast<std::size_t>(std::llround(beatPos * samplesPerBeat));
            if (onset >= frames) {
                continue;
            }
            any = true;
            const int role = static_cast<int>(eventRole(ev));
            e.onsets.push_back(onset);
            e.roles.push_back(role);
            if (role >= 0 && role < MetronomeRenderer::kRoleCount) {
                ++e.roleCount[role];
            }
        }
        if (!any) {
            break;   // 本小节一个都没落进来 → 之后更不会有
        }
    }
    return e;
}

/// 滑动窗最大值包络。
/// 为什么必须先取包络：点击是正弦振荡，对瞬时值设阈值会把**每个过零点**都当成新起点——
/// 实测 10 个点击被误判成 486 个（自检自己先踩了这个坑）。
std::vector<float> envelope(const std::vector<float>& buf, std::size_t window) {
    std::vector<float> env(buf.size(), 0.0f);
    for (std::size_t i = 0; i < buf.size(); ++i) {
        const std::size_t begin = (i >= window) ? (i - window + 1) : 0;
        float peak = 0.0f;
        for (std::size_t j = begin; j <= i; ++j) {
            peak = std::max(peak, std::fabs(buf[j]));
        }
        env[i] = peak;
    }
    return env;
}

/// 起点检出：包络在 window 内上升超过 kRiseDelta 且已超过底噪门限，并做去抖。
///
/// **必须在前面补一段静音再算包络**：包络是"过去 window 内的最大值"，若信号从第 0 个样点
/// 就开始响，则 i 与 i-window 落在同一个点击内部，增量为 0 → **首个点击永远检不出来**
/// （实测踩过：0 号样点的点击被漏掉，导致"偏差"里出现一个 1e9 的哨兵值）。
/// 补 window 个零样点后，任何点击都有完整的"之前是静音"参照。
std::vector<std::size_t> detectOnsets(const std::vector<float>& buf, double rate) {
    const std::size_t window = static_cast<std::size_t>(rate * 0.002);   // 2 ms
    const std::size_t refractory = static_cast<std::size_t>(rate * kRefractoryMs / 1000.0);

    std::vector<float> padded(window, 0.0f);
    padded.insert(padded.end(), buf.begin(), buf.end());
    const std::vector<float> env = envelope(padded, window);

    std::vector<std::size_t> onsets;
    std::size_t last = 0;
    for (std::size_t i = window; i < env.size(); ++i) {
        const float rise = env[i] - env[i - window];
        if (rise < kRiseDelta || env[i] < kOnsetLevel) {
            continue;
        }
        if (!onsets.empty() && i - last < refractory) {
            continue;
        }
        onsets.push_back(i - window);   // 换回原缓冲的坐标
        last = i;
    }
    return onsets;
}

/// 把"期望位置"与"检出位置"配对：每个期望位置附近（±tolerance）找最近的检出起点。
/// 返回每对的偏差（检出 − 期望）；配不上的期望位置记为 -1e9 参与失败判定。
std::vector<double> matchOnsets(const std::vector<std::size_t>& expected,
                                const std::vector<std::size_t>& detected, double tolerance) {
    std::vector<double> deviations;
    deviations.reserve(expected.size());
    for (const std::size_t want : expected) {
        double best = 1e18;
        for (const std::size_t got : detected) {
            const double diff = static_cast<double>(got) - static_cast<double>(want);
            if (std::fabs(diff) <= tolerance && std::fabs(diff) < std::fabs(best)) {
                best = diff;
            }
        }
        deviations.push_back(best > 1e17 ? -1e9 : best);
    }
    return deviations;
}

/// 采样级精度判定（对外的核心结论）：
///   ① 每个期望位置近旁都必须检出起点（漏拍是最严重的问题）
///   ② **同一角色**的偏差极差 ≤ 2 样点（无抖动、无漂移）
///   ③ 绝对偏差 ≤ 3 ms（只允许攻击段与重叠带来的小量固定延迟）
///
/// @param enforceJitter 重叠严重的用例里包络检测**原理上**无法分辨相邻点击
///        （前一次的拖尾把上升沿的基准抬高了），此时精确性由"独立重建"那条检查负责，
///        这里只查漏拍与绝对偏差——不把检测器的局限记成渲染器的缺陷。
void checkTimingByRole(QTextStream& out, const Expectation& exp,
                       const std::vector<std::size_t>& detected, const QString& what,
                       bool enforceJitter = true) {
    const double tolerance = kRate / 1000.0 * kOnsetToleranceMs;
    const std::vector<double> dev = matchOnsets(exp.onsets, detected, tolerance);

    std::size_t unmatched = 0;
    double absWorst = 0.0;
    for (const double d : dev) {
        if (d <= -1e17) {
            ++unmatched;
            continue;
        }
        absWorst = std::max(absWorst, std::fabs(d));
    }
    check(out, unmatched == 0,
          QStringLiteral("%1：有 %2 个期望位置附近没有检出起点（漏拍）").arg(what).arg(unmatched));
    if (unmatched > 0) {
        return;
    }

    // 按角色分组统计（角色名与 core 的 ClickRole 对应）
    const char* roleName[MetronomeRenderer::kRoleCount] = {"强拍", "弱拍", "细分"};
    double worstJitter = 0.0;
    int worstRole = -1;
    for (int role = 0; role < MetronomeRenderer::kRoleCount; ++role) {
        double lo = 1e18;
        double hi = -1e18;
        std::size_t count = 0;
        for (std::size_t i = 0; i < dev.size(); ++i) {
            if (exp.roles[i] != role) {
                continue;
            }
            lo = std::min(lo, dev[i]);
            hi = std::max(hi, dev[i]);
            ++count;
        }
        if (count == 0) {
            continue;
        }
        const double jitter = hi - lo;
        out << "  " << what << " ｜" << roleName[role] << "：n=" << count << "，偏差 " << lo << "–"
            << hi << "（抖动 " << jitter << " 样点 ≈ " << (jitter / kRate * 1e6) << " µs）\n";
        if (jitter > worstJitter) {
            worstJitter = jitter;
            worstRole = role;
        }
    }
    out << "  " << what << " ｜最大绝对偏差 " << absWorst << " 样点（" << (absWorst / kRate * 1e6)
        << " µs）\n";

    check(out, worstJitter <= kJitterSamples || !enforceJitter,
          QStringLiteral("%1：同角色偏差极差应 ≤ %2 样点，实测 %3（角色 %4）")
              .arg(what)
              .arg(kJitterSamples)
              .arg(worstJitter)
              .arg(worstRole >= 0 ? QString::fromUtf8(roleName[worstRole]) : QStringLiteral("—")));
    check(out, absWorst <= tolerance,
          QStringLiteral("%1：绝对偏差应 ≤ %2 样点（%3 ms）").arg(what).arg(tolerance).arg(kOnsetToleranceMs));
}

/// 写一个最小的 16 位单声道 WAV（仅自检用；正式的音频导出不在本功能范围内）。
bool writeWav(const QString& path, const std::vector<float>& samples, int rate) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(samples.size()) * 2u;
    auto put32 = [&file](std::uint32_t v) {
        char b[4] = {static_cast<char>(v & 0xFFu), static_cast<char>((v >> 8) & 0xFFu),
                     static_cast<char>((v >> 16) & 0xFFu), static_cast<char>((v >> 24) & 0xFFu)};
        file.write(b, 4);
    };
    auto put16 = [&file](std::uint16_t v) {
        char b[2] = {static_cast<char>(v & 0xFFu), static_cast<char>((v >> 8) & 0xFFu)};
        file.write(b, 2);
    };
    file.write("RIFF", 4);
    put32(36u + dataBytes);
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    put32(16u);
    put16(1u);
    put16(1u);
    put32(static_cast<std::uint32_t>(rate));
    put32(static_cast<std::uint32_t>(rate) * 2u);
    put16(2u);
    put16(16u);
    file.write("data", 4);
    put32(dataBytes);
    for (const float v : samples) {
        const float clamped = std::clamp(v, -1.0f, 1.0f);
        put16(static_cast<std::uint16_t>(
            static_cast<qint16>(std::lround(static_cast<double>(clamped) * 32767.0))));
    }
    return true;
}

/// 某个起点附近的峰值（用于比较三种角色的响度关系）
float peakAround(const std::vector<float>& buf, std::size_t onset, std::size_t span) {
    const std::size_t end = std::min(buf.size(), onset + span);
    float peak = 0.0f;
    for (std::size_t i = onset; i < end; ++i) {
        peak = std::max(peak, std::fabs(buf[i]));
    }
    return peak;
}

/// **独立重建**整段渲染：按期望的 (起始样点, 角色) 把点击波形逐个叠加，返回与实测输出的最大差。
///
/// 这条检查不依赖任何信号分析（不用阈值、不用包络、不用去抖），因此在**点击严重重叠**时同样有效：
/// 只要渲染器把某次点击放错位置、用错角色、漏掉一次，或重叠时丢了尾巴，重建结果就对不上。
/// 它与包络检测是互补的：包络证明"听起来确实在那里响了"，重建证明"逐样点就是这么排的"。
double reconstructAndCompare(const std::vector<float>& buf, const Expectation& exp, double rate,
                             std::size_t compareFrames) {
    std::vector<float> rebuilt(buf.size(), 0.0f);
    std::vector<float> slice;
    for (std::size_t i = 0; i < exp.onsets.size(); ++i) {
        if (exp.onsets[i] >= rebuilt.size()) {
            continue;
        }
        const ClickVoice voice = builtinVoice(exp.roles[i]);
        const std::size_t total = clickFrames(voice, rate);
        if (total == 0) {
            continue;
        }
        slice.assign(total, 0.0f);
        mixClickSlice(voice, rate, 0, slice.data(), total);   // 噪声是无状态哈希 → 与渲染器逐位同源
        const std::size_t count = std::min(total, rebuilt.size() - exp.onsets[i]);
        for (std::size_t c = 0; c < count; ++c) {
            rebuilt[exp.onsets[i] + c] += slice[c];
        }
    }
    double worst = 0.0;
    const std::size_t limit = std::min(compareFrames, buf.size());
    for (std::size_t i = 0; i < limit; ++i) {
        worst = std::max(worst, std::fabs(static_cast<double>(buf[i]) - static_cast<double>(rebuilt[i])));
    }
    return worst;
}

} // namespace

int runMetronomeCheck(const QString& wavOut) {
    QTextStream out(stdout);
    g_checks = 0;
    g_failures = 0;

    out << "===== 节拍器离线自检（不碰声卡）=====\n";

    // ---------------- 用例 1：4/4，第 1 拍拆成两个八分，其余整拍（用户给的典型用法）----------------
    {
        Pattern p;
        p.meter = Meter{4, 4};
        p.subdivisions = {2, 1, 1, 1};
        const Pattern n = normalize(p);
        out << "\n== 用例 1：4/4 ｜ 每拍 2,1,1,1 @ 120 BPM ==\n";
        out << "  每小节点击事件：" << eventsPerBar(n) << "（期望 5）\n";
        check(out, eventsPerBar(n) == 5, QStringLiteral("每小节事件数应为 5"));

        MetronomeRenderer renderer;
        const std::vector<float> buf = renderPattern(n, 120, kBlockA, 2, &renderer);
        const double perBeat = renderer.samplesPerBeat();
        checkNear(out, perBeat, 24000.0, 1e-9, QStringLiteral("每拍样点数（120 BPM / 48 kHz）"));

        // 期望只算到"末尾 100 ms 之前"：末尾那段里的事件是否落在最后一块内取决于块边界，
        // 拿它做断言会变成"测块边界"而不是"测时间轴"（多检出的末尾点击不参与判定）
        const std::size_t usable = buf.size() - static_cast<std::size_t>(kRate * 0.1);
        const Expectation exp = expectationWithin(n, perBeat, usable);
        const std::vector<std::size_t> got = detectOnsets(buf, kRate);
        out << "  期望起始样点（前 5 个）：";
        for (std::size_t i = 0; i < std::min<std::size_t>(5, exp.onsets.size()); ++i) {
            out << exp.onsets[i] << (i + 1 < exp.onsets.size() ? ", " : "");
        }
        out << "\n  检出起点数：" << got.size() << "（其中落在判定范围内的期望 "
            << exp.onsets.size() << " 个）\n";

        checkTimingByRole(out, exp, got, QStringLiteral("采样级精度"));

        // 第 1 拍内两个八分 → 间隔半拍；第 1 拍末点到第 2 拍 → 也是半拍
        checkNear(out, static_cast<double>(exp.onsets[1] - exp.onsets[0]), perBeat * 0.5, 1.0,
                  QStringLiteral("第 1 拍内两个八分的间隔 = 半拍"));

        // 角色次数：与"由拍号独立算出的事件表"逐项比对（等于核对了渲染器的内部计数）
        for (int role = 0; role < MetronomeRenderer::kRoleCount; ++role) {
            check(out, renderer.tickCount(role) == exp.roleCount[role],
                  QStringLiteral("角色 %1 的发声次数：实测 %2，期望 %3")
                      .arg(role)
                      .arg(renderer.tickCount(role))
                      .arg(exp.roleCount[role]));
        }
        out << "  角色次数（强/弱/细分）：" << renderer.tickCount(0) << " / " << renderer.tickCount(1)
            << " / " << renderer.tickCount(2) << "\n";

        // 单次点击的峰值应**等于**角色目标峰值（归一化生效），并保持强 > 弱 > 细分的次序
        const float accentPeak = peakAround(buf, exp.onsets[0], 4800);
        const float beatPeak = peakAround(buf, exp.onsets[2], 4800);
        const float subPeak = peakAround(buf, exp.onsets[1], 4800);
        out << "  单次点击峰值：强拍 " << accentPeak << "（目标 " << rolePeakTarget(0) << "）｜ 弱拍 "
            << beatPeak << "（目标 " << rolePeakTarget(1) << "）｜ 细分 " << subPeak << "（目标 "
            << rolePeakTarget(2) << "）\n";
        checkNear(out, accentPeak, rolePeakTarget(0), 0.01, QStringLiteral("强拍峰值应被归一化到位"));
        checkNear(out, beatPeak, rolePeakTarget(1), 0.01, QStringLiteral("弱拍峰值应被归一化到位"));
        checkNear(out, subPeak, rolePeakTarget(2), 0.01, QStringLiteral("细分峰值应被归一化到位"));
        check(out, accentPeak > beatPeak, QStringLiteral("强拍应响于弱拍"));
        check(out, beatPeak > subPeak, QStringLiteral("弱拍应响于细分"));
        check(out, accentPeak <= 1.0f, QStringLiteral("单次点击不得削顶（峰值 ≤ 1.0）"));
    }

    // ---------------- 用例 2：块长无关性（可复现性）----------------
    {
        Pattern p;
        p.meter = Meter{6, 8};
        p.subdivisions = {3, 1, 2, 1, 1, 3};
        const Pattern n = normalize(p);
        out << "\n== 用例 2：6/8 ｜ 每拍 3,1,2,1,1,3 @ 168 BPM ｜ 块长无关性 ==\n";
        MetronomeRenderer r1;
        MetronomeRenderer r2;
        const std::vector<float> a = renderPattern(n, 168, kBlockA, 1, &r1);
        const std::vector<float> b = renderPattern(n, 168, kBlockB, 1, &r2);
        check(out, a.size() == b.size(), QStringLiteral("两段长度应相同"));
        std::size_t diffCount = 0;
        double worst = 0.0;
        const std::size_t n1 = std::min(a.size(), b.size());
        for (std::size_t i = 0; i < n1; ++i) {
            if (a[i] != b[i]) {
                ++diffCount;
                worst = std::max(worst, std::fabs(static_cast<double>(a[i] - b[i])));
            }
        }
        out << "  512 vs 4096 块长的差异样点：" << diffCount << "（最大差 " << worst << "）\n";
        check(out, diffCount == 0,
              QStringLiteral("不同块长必须渲染出逐位相同的结果（噪声为无状态哈希）"));

        const Expectation exp =
            expectationWithin(n, r1.samplesPerBeat(), a.size() - static_cast<std::size_t>(kRate * 0.1));
        const std::vector<std::size_t> got = detectOnsets(a, kRate);
        out << "  一小节点击事件数 " << eventsPerBar(n) << "（期望 11）｜ 检出起点 " << got.size()
            << " 个（判定范围内期望 " << exp.onsets.size() << " 个）\n";
        check(out, eventsPerBar(n) == 11,
              QStringLiteral("每小节事件数：实测 %1，期望 11").arg(eventsPerBar(n)));
        checkTimingByRole(out, exp, got, QStringLiteral("6/8 采样级精度"));
        // 三连音：同一拍内相邻点击间隔 = 1/3 拍
        checkNear(out, static_cast<double>(exp.onsets[1] - exp.onsets[0]), r1.samplesPerBeat() / 3.0,
                  1.0, QStringLiteral("三连音间隔 = 1/3 拍"));
    }

    // ---------------- 用例 3：密集细分（真实重叠）----------------
    {
        Pattern p;
        p.meter = Meter{4, 4};
        p.subdivisions = {4, 4, 4, 4};
        const Pattern n = normalize(p);
        out << "\n== 用例 3：4/4 全十六分 @ 240 BPM（点击互相重叠）==\n";
        MetronomeRenderer renderer;
        const std::vector<float> buf = renderPattern(n, 240, kBlockA, 2, &renderer);

        float peak = 0.0f;
        bool finite = true;
        for (const float v : buf) {
            if (!std::isfinite(v)) {
                finite = false;
            }
            peak = std::max(peak, std::fabs(v));
        }
        const Expectation exp = expectationWithin(
            n, renderer.samplesPerBeat(), buf.size() - static_cast<std::size_t>(kRate * 0.1));
        const std::size_t usableForCase3 = buf.size() - static_cast<std::size_t>(kRate * 0.1);
        const std::vector<std::size_t> got = detectOnsets(buf, kRate);
        const double perBeat = renderer.samplesPerBeat();
        out << "  事件间隔 " << (perBeat / 4.0) << " 样点（" << (perBeat / 4.0 / kRate * 1000.0)
            << " ms）；细分点击长度 " << clickFrames(builtinVoice(2), kRate) << " 样点 → 必然重叠\n";
        out << "  检出起点 " << got.size() << " 个（判定范围内期望 " << exp.onsets.size()
            << " 个）｜ 叠加后峰值 " << peak << "\n";
        check(out, finite, QStringLiteral("渲染结果不得出现 NaN/Inf"));
        checkTimingByRole(out, exp, got, QStringLiteral("密集细分下的采样级精度"), false);
        // 重叠时精确性由"独立重建"负责（包络检测在重叠下无法分辨相邻点击，见函数说明）
        const Expectation full = expectationWithin(n, renderer.samplesPerBeat(), buf.size());
        const double worst = reconstructAndCompare(buf, full, kRate, usableForCase3);
        out << "  独立重建比对：期望事件 " << full.onsets.size() << " 个 ｜ 与实测输出的最大差 "
            << worst << "\n";
        check(out, worst < 1e-5,
              QStringLiteral("按期望(位置,角色)重建的波形应与渲染输出逐样点一致（最大差 %1）").arg(worst));
        check(out, peak > 0.0f, QStringLiteral("密集细分下仍应有声音"));
        check(out, peak <= static_cast<float>(MetronomeRenderer::kRoleCount) * 1.0f,
              QStringLiteral("叠加峰值应有界（不超过「角色数 × 满量程」）"));
    }

    // ---------------- 用例 4：自定义音频作为强拍音色 ----------------
    {
        out << "\n== 用例 4：自定义音频作为强拍音色 ==\n";
        // 造一个"能一眼认出来"的素材：前 240 个样点为 +0.5、随后 240 个样点为 −0.5
        std::vector<float> material(480, 0.0f);
        for (std::size_t i = 0; i < 240; ++i) {
            material[i] = 0.5f;
        }
        for (std::size_t i = 240; i < 480; ++i) {
            material[i] = -0.5f;
        }
        const QString tmp = QDir::tempPath() + QStringLiteral("/pitch-metro-material.wav");
        check(out, writeWav(tmp, material, 48000), QStringLiteral("测试素材应能写出：%1").arg(tmp));

        const WavData loaded = readWavMono(tmp.toUtf8().toStdString());
        check(out, loaded.ok && loaded.samples.size() == material.size(),
              QStringLiteral("素材应能被 readWavMono 读回（实测 %1 样点）")
                  .arg(static_cast<qlonglong>(loaded.samples.size())));

        Pattern p;
        p.meter = Meter{4, 4};
        p.subdivisions = {1, 1, 1, 1};
        const Pattern n = normalize(p);
        MetronomeRenderer renderer;
        renderer.setSampleRate(kRate);
        renderer.setBpm(120);
        renderer.setPattern(n);
        ClickVoice voice;
        voice.sample = loaded.samples;   // 装载口径与控制器一致：归一化到满量程 + 角色增益
        voice.sampleRate = loaded.sampleRate;
        voice.gain = static_cast<float>(rolePeakTarget(0));
        voice.maxSeconds = 1.0;
        renderer.setVoice(0, voice);

        const std::size_t frames = static_cast<std::size_t>(renderer.samplesPerBeat() * 4.0) + 4800;
        std::vector<float> buf(frames, 0.0f);
        std::size_t done = 0;
        while (done < frames) {
            const std::size_t take = std::min<std::size_t>(kBlockA, frames - done);
            renderer.render(buf.data() + done, take);
            done += take;
        }
        const Expectation exp = expectationWithin(
            n, renderer.samplesPerBeat(), buf.size() - static_cast<std::size_t>(kRate * 0.1));
        const std::vector<std::size_t> got = detectOnsets(buf, kRate);
        checkTimingByRole(out, exp, got, QStringLiteral("样本点击的采样级精度"));
        {
            // 与**期望位置**（独立算出）逐样点比较，而不是与"检出的起点"比较：
            // 检出起点带有攻击段造成的几样点延迟，用它当基准会把正常延迟当成波形不符。
            const std::size_t onset = exp.onsets.front();
            double worst = 0.0;
            for (std::size_t i = 0; i < loaded.samples.size() && onset + i < buf.size(); ++i) {
                const double expectedValue =
                    static_cast<double>(loaded.samples[i]) * static_cast<double>(voice.gain);
                worst = std::max(worst, std::fabs(static_cast<double>(buf[onset + i]) - expectedValue));
            }
            out << "  样本点击位于 " << onset << " ｜与「素材 × 增益」的最大差 " << worst << "\n";
            // 注意：素材是 ±0.5 的方波，读回后是 ±16383/32767，故这里与**读回的素材**比较，
            // 而不是与写入前的 0.5 比较（早先那样写会让负半周整体差 1.0，属断言写错）
            check(out, worst < 1e-4, QStringLiteral("样本点击应逐样点等于「素材 × 增益」"));
        }
        QFile::remove(tmp);
    }

    // ---------------- 用例 5：播放中变速不丢拍 ----------------
    {
        out << "\n== 用例 5：播放中改 BPM（120 → 60）不丢拍 ==\n";
        Pattern p;
        p.meter = Meter{4, 4};
        p.subdivisions = {1, 1, 1, 1};
        MetronomeRenderer renderer;
        renderer.setSampleRate(kRate);
        renderer.setBpm(120);
        renderer.setPattern(normalize(p));

        const std::size_t frames = static_cast<std::size_t>(kRate * 6.0);
        std::vector<float> buf(frames, 0.0f);
        std::size_t done = 0;
        bool changed = false;
        while (done < frames) {
            const std::size_t take = std::min<std::size_t>(kBlockA, frames - done);
            renderer.render(buf.data() + done, take);
            done += take;
            if (!changed && done > frames / 3) {
                renderer.setBpm(60);
                changed = true;
            }
        }
        const std::vector<std::size_t> got = detectOnsets(buf, kRate);
        out << "  检出起点 " << got.size() << " 个 ｜ 引擎统计：强拍 " << renderer.tickCount(0)
            << "、弱拍 " << renderer.tickCount(1) << "\n";
        // 6 秒里跑了"120 BPM 的小节 + 改速后 60 BPM 的小节"，故强拍数取决于切点，
        // 不做写死的次数断言，而是断言两条更本质的性质（见下）：
        check(out, renderer.tickCount(0) >= 2,
              QStringLiteral("改速后仍应持续产生强拍：实测 %1").arg(renderer.tickCount(0)));
        check(out, renderer.tickCount(1) >= 6,
              QStringLiteral("改速后仍应持续产生弱拍：实测 %1").arg(renderer.tickCount(1)));

        // 关键性质 1：改速（变慢）不得让点击挤在一起——最小间隔不应小于**最快那段**的一拍
        const double fastBeat = 60.0 / 120.0 * kRate;   // 120 BPM = 24000 样点
        const double slowBeat = 60.0 / 60.0 * kRate;    // 60 BPM = 48000 样点
        double minGap = 1e18;
        double maxGap = 0.0;
        for (std::size_t i = 1; i < got.size(); ++i) {
            const double gap = static_cast<double>(got[i] - got[i - 1]);
            minGap = std::min(minGap, gap);
            maxGap = std::max(maxGap, gap);
        }
        out << "  间隔：最小 " << minGap << "（120 BPM 一拍 = " << fastBeat << "）｜ 最大 " << maxGap
            << "（60 BPM 一拍 = " << slowBeat << "）\n";
        check(out, minGap >= fastBeat * 0.9,
              QStringLiteral("改速后不得出现点击挤在一起：最小间隔 %1，下限 %2")
                  .arg(minGap)
                  .arg(fastBeat * 0.9));
        // 关键性质 2：确实出现了一段"按 60 BPM 走"的空档 → 证明变速真的生效了
        check(out, maxGap >= slowBeat * 0.9,
              QStringLiteral("应出现约 60 BPM 的间隔（证明变速生效）：最大间隔 %1，期望 ≥ %2")
                  .arg(maxGap)
                  .arg(slowBeat * 0.9));
    }

    // ---------------- 用例 6：输出打包（总增益 / 声道复制 / 硬夹紧 / 缓冲不足）----------------
    {
        out << "\n== 用例 6：输出打包（packMono）==\n";
        const std::vector<float> mono{0.0f, 0.5f, -0.5f, 2.0f, -2.0f, 1.0f};
        const std::size_t frames = mono.size();

        // Float32 单声道：夹紧到 [-1,1] 后按增益缩放
        std::vector<float> f32(frames, 0.0f);
        const std::size_t bytes = packMono(mono.data(), frames, 1, PackFormat::Float32,
                                           kOutputMasterGain,
                                           reinterpret_cast<char*>(f32.data()),
                                           f32.size() * sizeof(float));
        check(out, bytes == frames * sizeof(float), QStringLiteral("Float32 单声道应写满"));
        for (std::size_t i = 0; i < frames; ++i) {
            const float expected = std::clamp(mono[i] * kOutputMasterGain, -1.0f, 1.0f);
            checkNear(out, f32[i], expected, 1e-6, QStringLiteral("Float32 单声道第 %1 个样点").arg(i));
        }

        // Float32 立体声：左右相同
        std::vector<float> st(frames * 2, 0.0f);
        packMono(mono.data(), frames, 2, PackFormat::Float32, kOutputMasterGain,
                 reinterpret_cast<char*>(st.data()), st.size() * sizeof(float));
        bool sameChannels = true;
        for (std::size_t i = 0; i < frames; ++i) {
            if (st[i * 2] != st[i * 2 + 1]) {
                sameChannels = false;
            }
        }
        check(out, sameChannels, QStringLiteral("立体声应左右一致（本引擎不产生声道差异）"));

        // Int16 单声道：量化正确且越界输入被夹紧（不得回绕成正弦爆音）
        std::vector<std::int16_t> i16(frames, 0);
        packMono(mono.data(), frames, 1, PackFormat::Int16, 1.0f,
                 reinterpret_cast<char*>(i16.data()), i16.size() * sizeof(std::int16_t));
        check(out, i16[0] == 0, QStringLiteral("Int16 0.0 → 0"));
        check(out, i16[5] == 32767, QStringLiteral("Int16 1.0 → 32767"));
        check(out, i16[3] == 32767 && i16[4] == -32767,
              QStringLiteral("Int16 越界输入应夹紧到 ±32767（实测 %1 / %2）").arg(i16[3]).arg(i16[4]));

        // 缓冲不足：不写任何数据并返回 0
        std::vector<std::int16_t> tiny(2, 7);
        const std::size_t shortBytes = packMono(mono.data(), frames, 1, PackFormat::Int16, 1.0f,
                                               reinterpret_cast<char*>(tiny.data()),
                                               tiny.size() * sizeof(std::int16_t));
        check(out, shortBytes == 0 && tiny[0] == 7 && tiny[1] == 7,
              QStringLiteral("缓冲不足时应返回 0 且一个字节都不写"));
    }

    // ---------------- 用例 7：拍号/细分模型与配置串往返 ----------------
    {
        out << "\n== 用例 7：拍号与细分模型 ==\n";

        // 往返：QSettings 里存的就是这个字符串，读写不一致会让用户的设置**静默丢失**
        const QStringList specs{QStringLiteral("4/4:2,1,1,1"), QStringLiteral("6/8:3,1,2,1,1,3"),
                                QStringLiteral("12/8:1,1,1,1,1,1,1,1,1,1,1,1"),
                                QStringLiteral("2/2:1,1")};
        for (const QString& spec : specs) {
            const Pattern parsed = fromString(spec.toStdString());
            const QString again = QString::fromStdString(toString(parsed));
            check(out, again == spec,
                  QStringLiteral("配置串往返应稳定：%1 → %2").arg(spec, again));
        }

        // 容错：看不懂的输入退回"4/4 全整拍"，而不是抛异常或产生半截状态
        const Pattern fallback = fromString("完全不是拍号");
        check(out, fallback.meter.beats == 4 && fallback.meter.unit == 4
                       && fallback.subdivisions.size() == 4 && fallback.subdivisions[0] == 1,
              QStringLiteral("无法解析的配置串应退回 4/4 全整拍"));

        // 夹紧：越界值不得进入模型
        Pattern wild;
        wild.meter.beats = 99;
        wild.meter.unit = 17;
        wild.subdivisions = {9, -3};
        const Pattern clamped = normalize(wild);
        check(out, clamped.meter.beats == kMaxBeats,
              QStringLiteral("拍数应夹到上限 %1：实测 %2").arg(kMaxBeats).arg(clamped.meter.beats));
        check(out, clamped.meter.unit == 16, QStringLiteral("分母应吸附到最近的合法值 16：实测 %1").arg(clamped.meter.unit));
        check(out, clamped.subdivisions.size() == static_cast<std::size_t>(kMaxBeats),
              QStringLiteral("细分数数组长度应对齐拍数"));
        check(out, clamped.subdivisions[0] == kMaxSubdivision && clamped.subdivisions[1] == kMinSubdivision,
              QStringLiteral("越界细分数应被夹紧到 [%1, %2]").arg(kMinSubdivision).arg(kMaxSubdivision));

        // 事件构造：第 1 拍两个八分 → 位置 0、0.5；强拍只在每小节第一拍的第一个点
        Pattern p;
        p.meter = Meter{4, 4};
        p.subdivisions = {2, 1, 1, 1};
        const std::vector<ClickEvent> events = buildEvents(p);
        check(out, events.size() == 5, QStringLiteral("事件数应等于各拍细分之和（5）"));
        checkNear(out, events[1].beatPosition, 0.5, 1e-12, QStringLiteral("第 1 拍内第二个点的位置 = 0.5 拍"));
        int accents = 0;
        for (const ClickEvent& e : events) {
            if (e.accent) {
                ++accents;
            }
        }
        check(out, accents == 1, QStringLiteral("一小节内强拍应恰好 1 个（实测 %1）").arg(accents));
        check(out, static_cast<int>(eventRole(events[0])) == 0
                       && static_cast<int>(eventRole(events[1])) == 2
                       && static_cast<int>(eventRole(events[2])) == 1,
              QStringLiteral("角色判定应为 强拍/细分/弱拍"));

        // 预置拍号：不应有重复，且都合法；覆盖市面常见清单（含 1/4 与 5/8）
        const std::vector<Meter> presets = presetMeters();
        bool unique = true;
        for (std::size_t i = 0; i < presets.size(); ++i) {
            for (std::size_t j = i + 1; j < presets.size(); ++j) {
                if (presets[i].beats == presets[j].beats && presets[i].unit == presets[j].unit) {
                    unique = false;
                }
            }
        }
        check(out, unique, QStringLiteral("预置拍号不应重复（共 %1 个）").arg(presets.size()));
        bool hasOneFour = false;
        bool hasFiveEight = false;
        for (const Meter& m : presets) {
            hasOneFour = hasOneFour || (m.beats == 1 && m.unit == 4);
            hasFiveEight = hasFiveEight || (m.beats == 5 && m.unit == 8);
        }
        check(out, hasOneFour && hasFiveEight,
              QStringLiteral("预置应含 1/4 与 5/8（市面常见清单口径）"));

        // 重音分组：复合拍号按 3 个八分一组（组首与第一拍同样加重音，6/8 的"二拍感"来源）
        auto accentBeats = [](const Meter& m) {
            QStringList accented;
            const Pattern pat = normalize(Pattern{m, {}});
            for (int b = 0; b < pat.meter.beats; ++b) {
                if (isAccentBeat(pat, b)) {
                    accented << QString::number(b + 1);
                }
            }
            return accented.join(QStringLiteral(","));
        };
        out << "  重音拍位：4/4 → [" << accentBeats(Meter{4, 4}) << "]｜6/8 → [" << accentBeats(Meter{6, 8})
            << "]｜9/8 → [" << accentBeats(Meter{9, 8}) << "]｜12/8 → [" << accentBeats(Meter{12, 8})
            << "]｜7/8 → [" << accentBeats(Meter{7, 8}) << "]\n";
        check(out, accentBeats(Meter{4, 4}) == QStringLiteral("1"),
              QStringLiteral("4/4 只有第 1 拍是重音"));
        check(out, accentBeats(Meter{6, 8}) == QStringLiteral("1,4"),
              QStringLiteral("6/8 重音应落在第 1、4 个八分音符（实测 %1）").arg(accentBeats(Meter{6, 8})));
        check(out, accentBeats(Meter{9, 8}) == QStringLiteral("1,4,7"),
              QStringLiteral("9/8 重音应为 1,4,7（实测 %1）").arg(accentBeats(Meter{9, 8})));
        check(out, accentBeats(Meter{12, 8}) == QStringLiteral("1,4,7,10"),
              QStringLiteral("12/8 重音应为 1,4,7,10（实测 %1）").arg(accentBeats(Meter{12, 8})));
        check(out, accentBeats(Meter{7, 8}) == QStringLiteral("1"),
              QStringLiteral("7/8 分组不可从拍号推断，故只保留第 1 拍为重音"));
        // 组首的重音只落在"该拍的首个点"上（细分点不得被当作重音）
        Pattern sixEight;
        sixEight.meter = Meter{6, 8};
        sixEight.subdivisions = {2, 1, 1, 1, 1, 1};
        int accentEvents = 0;
        for (const ClickEvent& e : buildEvents(sixEight)) {
            if (e.accent) {
                ++accentEvents;
                check(out, e.subIndex == 0,
                      QStringLiteral("重音必须落在拍的首个点上（实测 subIndex=%1）").arg(e.subIndex));
            }
        }
        check(out, accentEvents == 2,
              QStringLiteral("6/8 一小节应恰好 2 个重音事件（第 1 与第 4 拍）：实测 %1").arg(accentEvents));
    }

    // ---------------- 用例 8：点击测速（tap tempo）----------------
    {
        out << "\n== 用例 8：点击测速 ==\n";
        TapTempo tap;
        check(out, tap.tap(0) == 0, QStringLiteral("只点 1 下时不应给出速度（样本不足）"));
        check(out, tap.tap(500) == 120, QStringLiteral("两下间隔 500 ms 应给出 120 BPM（实测 %1）").arg(tap.bpm()));
        tap.reset();
        check(out, tap.bpm() == 0 && tap.tapCount() == 0, QStringLiteral("reset() 应清空历史"));

        // 连点 5 下（每 500 ms）→ 稳定 120
        for (int i = 0; i < 5; ++i) {
            tap.tap(static_cast<long long>(i) * 500);
        }
        check(out, tap.bpm() == 120, QStringLiteral("等间隔连点应给出 120 BPM（实测 %1）").arg(tap.bpm()));
        check(out, tap.tapCount() == 5, QStringLiteral("点击次数应为 5（实测 %1）").arg(tap.tapCount()));

        // 只保留最近若干个间隔：第 6 下开始不再累积
        tap.tap(2500);
        tap.tap(3000);
        check(out, tap.tapCount() <= TapTempo::kMaxIntervals + 1,
              QStringLiteral("时间戳只保留最近 %1 个（实测 %2）")
                  .arg(TapTempo::kMaxIntervals + 1)
                  .arg(tap.tapCount()));

        // 停顿 > 2 秒 → 视为重新开始
        tap.tap(9000);
        check(out, tap.bpm() == 0 && tap.tapCount() == 1,
              QStringLiteral("停顿超过 2 秒应重新开始（实测 bpm=%1，次数=%2）").arg(tap.bpm()).arg(tap.tapCount()));

        // 不均匀点击取平均：间隔 480 / 510 / 480 → 均值 490 → 122 BPM
        TapTempo uneven;
        uneven.tap(0);
        uneven.tap(480);
        uneven.tap(990);
        uneven.tap(1470);
        check(out, uneven.bpm() == 122,
              QStringLiteral("不均匀间隔（480/510/480）应取平均给 122 BPM（实测 %1）").arg(uneven.bpm()));

        // 过快的间隔被忽略：0 → 100（太快）→ 600，有效间隔只有 500
        TapTempo skipping;
        skipping.tap(0);
        skipping.tap(100);
        skipping.tap(600);
        check(out, skipping.bpm() == 120,
              QStringLiteral("过快的间隔应被忽略，按剩余间隔给 120 BPM（实测 %1）").arg(skipping.bpm()));

        // 极慢的间隔取到下限：2000 ms → 30 BPM（= kMinBpm）
        TapTempo slow;
        slow.tap(0);
        slow.tap(2000);
        check(out, slow.bpm() == kMinBpm,
              QStringLiteral("间隔 2000 ms 应给出下限 %1 BPM（实测 %2）").arg(kMinBpm).arg(slow.bpm()));

        // 非单调时间戳（时钟回拨/误用）不得产生负间隔
        TapTempo backwards;
        backwards.tap(1000);
        backwards.tap(500);
        check(out, backwards.bpm() == 0, QStringLiteral("时间戳倒退应按重新开始处理（不给速度）"));
    }

    // ---------------- 可选：导出 WAV 供人耳试听 ----------------
    if (!wavOut.isEmpty()) {
        Pattern p;
        p.meter = Meter{4, 4};
        p.subdivisions = {2, 1, 1, 1};
        const std::vector<float> buf = renderPattern(normalize(p), 120, kBlockB, 4);
        // 导出时把总增益与夹紧也算进去：导出文件与真正送去声卡的波形一致
        std::vector<float> packed(buf.size(), 0.0f);
        packMono(buf.data(), buf.size(), 1, PackFormat::Float32, kOutputMasterGain,
                 reinterpret_cast<char*>(packed.data()), packed.size() * sizeof(float));
        const bool ok = writeWav(wavOut, packed, static_cast<int>(kRate));
        out << "\n导出试听文件：" << wavOut << (ok ? "（成功）" : "（失败）")
            << " ｜ 4 小节 4/4 @120 BPM，第 1 拍两个八分\n";
        check(out, ok, QStringLiteral("试听 WAV 应能写出"));
    }

    out << "\n自检项：" << (g_checks - g_failures) << " / " << g_checks << " 通过\n";
    out << (g_failures == 0 ? "[PASS] 节拍器离线自检全部通过\n"
                            : "[FAIL] 节拍器离线自检有失败项\n");
    out.flush();
    return g_failures == 0 ? 0 : 1;
}

} // namespace pitch

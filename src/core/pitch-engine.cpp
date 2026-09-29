// 音高检测引擎实现 —— 逐行对齐上游 tools/pitch-engine.js
//
// 对照关系（行号指上游文件）：
//   freqToNote                  ← L57-67     （在 note-converter.cpp）
//   rmsOf                       ← L69-73
//   refineTau                   ← L81-107
//   refineFreqByCorrelation     ← L127-155
//   magAtLut / harmonicScore    ← L183-211
//   preferFundamental           ← L221-232
//   detectPitch                 ← L239-330
//   detectWithLadder            ← L336-346
//   detectWithCurve             ← L405-413
//   detectWithLadderCurve       ← L418-428
//
// 算法注释只写"为什么"（R11）；"是什么"由上游引擎的注释承担，不在此重复。

#include "pitch-engine.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace pitch {

namespace {

/// 圆周率。不用 M_PI：它是 POSIX 扩展，MSVC 上需先定义 _USE_MATH_DEFINES，属可移植性陷阱。
constexpr double kPi = 3.14159265358979323846;

/// 差分函数 d(τ)：Σ (x[i] − x[i+τ])²。
double differenceAtTau(std::span<const float> buf, double tau) {
    const std::size_t n = buf.size();
    double sum = 0.0;
    for (std::size_t i = 0; static_cast<double>(i) + tau + 1.0 < static_cast<double>(n); ++i) {
        const double j = static_cast<double>(i) + tau;
        const std::size_t j0 = static_cast<std::size_t>(j);
        if (j0 + 1 >= n) {
            break;
        }
        const double frac = j - static_cast<double>(j0);
        const double interpolated =
            static_cast<double>(buf[j0]) * (1.0 - frac) + static_cast<double>(buf[j0 + 1]) * frac;
        const double d = static_cast<double>(buf[i]) - interpolated;
        sum += d * d;
    }
    return sum;
}

} // namespace

/* ============================ 查表 ============================ */

PitchEngine::SineLut::SineLut() {
    for (int i = 0; i < kLutSize; ++i) {
        // 取模后转弧度，保证与上游 Math.cos(2π·i/1024) 的相位定义一致
        const double phase = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(kLutSize);
        cosTable[i] = std::cos(phase);
        sinTable[i] = std::sin(phase);
    }
}

const PitchEngine::SineLut& PitchEngine::sineLut() {
    // 函数内静态：首次调用时构造，之后零成本复用（等价于上游的模块级查表）
    static const SineLut lut;
    return lut;
}

/* ============================ 基础量 ============================ */

double PitchEngine::rms(std::span<const float> buf) {
    double sum = 0.0;
    for (const float v : buf) {
        const double d = static_cast<double>(v);
        sum += d * d;
    }
    const double n = static_cast<double>(buf.size());
    return n > 0.0 ? std::sqrt(sum / n) : 0.0;
}

/* ============================ τ 精修 ============================ */

double PitchEngine::refineTau(std::span<const float> buf, double tauGuess) {
    double best = tauGuess;
    double bestCost = std::numeric_limits<double>::infinity();
    constexpr double step = 1.0 / 64.0;
    for (double t = tauGuess - 0.5; t <= tauGuess + 0.5; t += step) {
        if (t <= 1.0) {
            continue;
        }
        const double cost = differenceAtTau(buf, t);
        if (cost < bestCost) {
            bestCost = cost;
            best = t;
        }
    }
    return best;
}

/* ============================ 频域精修 ============================ */

double PitchEngine::refineFreqByCorrelation(std::span<const float> buf,
                                            double sampleRate,
                                            double freqGuess) {
    if (freqGuess < kRefineMinHz) {
        return freqGuess;   // 低频段 YIN 已足够准（上游实测 ≤900 Hz 误差 0.58 音分），跳过以省算力
    }
    const std::size_t n = buf.size();
    const auto mag = [&](double f) {
        const double w = 2.0 * kPi * f / sampleRate;
        double re = 0.0;
        double im = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const double x = static_cast<double>(buf[i]);
            const double p = w * static_cast<double>(i);
            re += x * std::cos(p);
            im -= x * std::sin(p);
        }
        return re * re + im * im;
    };
    const double lo = freqGuess * (1.0 - kRefineSpan);
    const double hi = freqGuess * (1.0 + kRefineSpan);
    const double step = (hi - lo) / static_cast<double>(kRefineSteps);
    double bestF = freqGuess;
    double bestM = -1.0;
    for (double f = lo; f <= hi; f += step) {
        const double m = mag(f);
        if (m > bestM) {
            bestM = m;
            bestF = f;
        }
    }
    const double m1 = mag(bestF - step);
    const double m2 = bestM;
    const double m3 = mag(bestF + step);
    const double den = 2.0 * (2.0 * m2 - m1 - m3);
    if (std::abs(den) > 1e-12) {
        const double delta = (m3 - m1) / den;
        if (std::abs(delta) <= 1.0) {
            bestF += delta * step;   // 抛物线细化，落在峰值附近一个步长内
        }
    }
    return bestF;
}

/* ============================ 谐波一致性 ============================ */

double PitchEngine::magAtLut(std::span<const float> buf, double sampleRate, double freq) {
    const double ratio = freq / sampleRate * static_cast<double>(kLutSize);
    // 相位增量取整，和上游 `Math.round(...) || 1` 一致：0 会退化为常量直流，故兜底为 1
    int step = static_cast<int>(std::llround(ratio));
    if (step == 0) {
        step = 1;
    }
    const SineLut& lut = sineLut();
    double re = 0.0;
    double im = 0.0;
    int idx = 0;
    for (const float v : buf) {
        const double x = static_cast<double>(v);
        re += x * lut.cosTable[idx & (kLutSize - 1)];
        im -= x * lut.sinTable[idx & (kLutSize - 1)];
        idx += step;
    }
    const double len = static_cast<double>(buf.size());
    return len > 0.0 ? std::sqrt(re * re + im * im) / len : 0.0;
}

double PitchEngine::harmonicScore(std::span<const float> buf, double sampleRate, double freq) {
    if (freq * static_cast<double>(kHarmonicMax) > sampleRate / 2.0 * 0.95) {
        // 顶部放不下多次泛音，退化为单点幅值
        return magAtLut(buf, sampleRate, freq);
    }
    // 算术加权（低次泛音权重 1/k）而非几何平均：几何平均对缺失泛音过敏感，
    // 实测会把高音区折低一个八度（84→76，上游 pitfalls #29），故保留算术加权。
    double onSum = 0.0;
    double offSum = 0.0;
    for (int k = 1; k <= kHarmonicMax; ++k) {
        const double w = 1.0 / static_cast<double>(k);
        onSum += w * magAtLut(buf, sampleRate, freq * static_cast<double>(k));
        // 背景取半音间隔处（非整数倍）的幅值，代表"平均水平"，避免点越多分越高
        offSum += w * magAtLut(buf, sampleRate, freq * (static_cast<double>(k) + 0.5));
    }
    return onSum - offSum;
}

double PitchEngine::preferFundamental(std::span<const float> buf, double sampleRate, double freq) {
    if (freq < kSubharmonicMinHz) {
        return freq;   // 极低音区泛音密集且基频极弱，改判容易越改越偏（上游实测 A0/B0）
    }
    double best = freq;
    double bestScore = harmonicScore(buf, sampleRate, freq);
    for (int d = 2; d <= 3; ++d) {
        const double cand = freq / static_cast<double>(d);
        // 与上游一致：这里用**固定的默认下限**（上游的模块常量 F_MIN），不使用调用方传入的 cfg.fMin。
        // 保持原样是为了与上游数值严格一致；若将来要为窄音域调用方收紧此判据，
        // 必须同步改动上游引擎并重跑跨语言对拍，否则两条链会分叉（ADR-0003）。
        if (cand < kDefaultFMin) {
            break;
        }
        const double s = harmonicScore(buf, sampleRate, cand);
        if (s > bestScore * kPreferFundamentalRatio) {
            best = cand;
            bestScore = s;
        }
    }
    return best;
}

/* ============================ YIN 主判据 ============================ */

std::optional<PitchResult> PitchEngine::detect(std::span<const float> buf,
                                               double sampleRate,
                                               const EngineConfig& cfg,
                                               EngineBuffers& buffers) {
    return detect(buf, sampleRate, cfg, buffers, nullptr, nullptr, nullptr);
}

std::optional<PitchResult> PitchEngine::detect(std::span<const float> buf,
                                               double sampleRate,
                                               const EngineConfig& cfg,
                                               EngineBuffers& buffers,
                                               std::span<const double>* outCurve,
                                               int* outTauMin,
                                               int* outTauMax) {
    const double sr = sampleRate;
    const double lo = cfg.fMin;
    const double hi = cfg.fMax;
    const std::size_t n = buf.size();
    if (n < 4 || n > kMaxFrame || sr <= 0.0) {
        return std::nullopt;
    }

    // τ 起点再除以 2：起点若等于最高音对应周期，高音区会先命到倍周期（上游实测 905–1046 Hz 全中招）
    const int tauMin = std::max(2, static_cast<int>(std::floor(sr / hi / 2.0)));
    const int tauMax = std::min(static_cast<int>(std::floor(static_cast<double>(n) / 2.0)) - 1,
                                static_cast<int>(std::ceil(sr / lo)));
    if (tauMax <= tauMin + 2) {
        return std::nullopt;
    }

    // 差分函数 d(τ)
    for (int tau = tauMin; tau <= tauMax; ++tau) {
        double sum = 0.0;
        const std::size_t limit = n - static_cast<std::size_t>(tau);
        for (std::size_t i = 0; i < limit; ++i) {
            const double diff = static_cast<double>(buf[i]) - static_cast<double>(buf[i + static_cast<std::size_t>(tau)]);
            sum += diff * diff;
        }
        buffers.yinD[static_cast<std::size_t>(tau)] = sum;
    }

    // 累积均值归一化差分 cmnd(τ)
    double running = 0.0;
    for (int tau = tauMin; tau <= tauMax; ++tau) {
        running += buffers.yinD[static_cast<std::size_t>(tau)];
        const double denom = running <= 0.0 ? 1.0 : running;
        buffers.cmnd[static_cast<std::size_t>(tau)] =
            buffers.yinD[static_cast<std::size_t>(tau)] * static_cast<double>(tau - tauMin + 1) / denom;
    }

    // 对外暴露曲线（调试界面画谷值用）；数据有效期至下一次 detect() 调用
    if (outCurve != nullptr) {
        *outCurve = std::span<const double>(buffers.cmnd.data(),
                                            static_cast<std::size_t>(tauMax) + 1);
    }
    if (outTauMin != nullptr) {
        *outTauMin = tauMin;
    }
    if (outTauMax != nullptr) {
        *outTauMax = tauMax;
    }

    // 选周期：取**第一个**低于阈值的合格谷，再沿谷下滑到谷底（规范 YIN 判据）。
    // 上游实测过的三种替代判据（全局最小 / 前 N 个谷中最深者 / 全局最小附近最早的小 τ）都会
    // 被更深的下属谷带偏（88 键分别掉到 80 / 77 / 68），故不用。
    int tauEst = -1;
    for (int tau = tauMin + 1; tau < tauMax - 1; ++tau) {
        const double c = buffers.cmnd[static_cast<std::size_t>(tau)];
        if (c < kYinThreshold &&
            c <= buffers.cmnd[static_cast<std::size_t>(tau - 1)] &&
            c <= buffers.cmnd[static_cast<std::size_t>(tau + 1)]) {
            tauEst = tau;
            break;
        }
    }
    if (tauEst < 0 || tauEst <= tauMin || tauEst >= tauMax - 1) {
        return std::nullopt;
    }
    // 合格判据允许平台，谷底可能还在下一两个样点
    while (tauEst + 1 < tauMax - 1 &&
           buffers.cmnd[static_cast<std::size_t>(tauEst + 1)] < buffers.cmnd[static_cast<std::size_t>(tauEst)]) {
        ++tauEst;
    }

    // 浅谷复核：首个谷若"浅"，而同相位处（2 倍）存在深得多的谷，说明命中的是半周期（泛音）。
    // 触发条件限定得很死：实测钢琴 G3 的半周期 τ=111 处 cmnd 0.205 < 阈值 0.3，
    // 不加这条会把 698 帧里的多数判成高八度（G3→G4，+1208 音分，上游 pitfalls #23）。
    if (buffers.cmnd[static_cast<std::size_t>(tauEst)] > kShallowThreshold) {
        const int target = static_cast<int>(std::llround(static_cast<double>(tauEst) * 2.0));
        if (target < tauMax - 1) {
            int bestT = -1;
            double bestC = std::numeric_limits<double>::infinity();
            for (int d = -2; d <= 2; ++d) {
                const int t = target + d;
                if (t <= tauMin || t >= tauMax - 1) {
                    continue;
                }
                const double c = buffers.cmnd[static_cast<std::size_t>(t)];
                if (c < bestC) {
                    bestC = c;
                    bestT = t;
                }
            }
            double globalMin = std::numeric_limits<double>::infinity();
            for (int tau = tauMin; tau <= tauMax; ++tau) {
                globalMin = std::min(globalMin, buffers.cmnd[static_cast<std::size_t>(tau)]);
            }
            if (bestT > 0 && bestC < buffers.cmnd[static_cast<std::size_t>(tauEst)] * 0.5 &&
                bestC - globalMin < 0.05) {
                tauEst = bestT;
            }
        }
    }

    // 抛物线插值给出亚样点 τ
    const double a = buffers.cmnd[static_cast<std::size_t>(tauEst - 1)];
    const double b = buffers.cmnd[static_cast<std::size_t>(tauEst)];
    const double c = buffers.cmnd[static_cast<std::size_t>(tauEst + 1)];
    double refined = static_cast<double>(tauEst);
    const double denom = 2.0 * (2.0 * b - a - c);
    if (std::abs(denom) > 1e-12) {
        const double delta = (c - a) / denom;
        if (std::abs(delta) < 1.0) {
            refined = static_cast<double>(tauEst) + delta;
        }
    }
    refined = refineTau(buf, refined);
    double freq = sr / refined;
    // 时域 τ 精修后再过频域：修正 τ 整数化在高音区的固定偏差（上游实测 A#7 各窗长均 +48 音分）
    freq = refineFreqByCorrelation(buf, sr, freq);

    // 谐波一致性复核：YIN 可能选中半周期/三分之一周期（即泛音），用"整条谐波列"判一次
    if (cfg.harmonicCorrect) {
        freq = preferFundamental(buf, sr, freq);
    }

    if (freq < lo || freq > hi) {
        return std::nullopt;
    }
    PitchResult result;
    result.freq = freq;
    result.confidence = std::max(0.0, std::min(1.0, 1.0 - buffers.cmnd[static_cast<std::size_t>(tauEst)]));
    result.tau = tauEst;
    result.frameSize = n;
    return result;
}

/* ============================ 级联窗长 ============================ */

std::optional<PitchResult> PitchEngine::detectWithLadder(std::span<const float> data,
                                                         std::size_t offset,
                                                         double sampleRate,
                                                         double rmsFloor,
                                                         const EngineConfig& cfg,
                                                         EngineBuffers& buffers,
                                                         std::span<const double>* outCurve,
                                                         int* outTauMin,
                                                         int* outTauMax) {
    for (const std::size_t n : cfg.frameLadder) {
        if (offset + n > data.size()) {
            continue;
        }
        const std::span<const float> frame = data.subspan(offset, n);
        if (rms(frame) < rmsFloor) {
            return std::nullopt;
        }
        const auto r = detect(frame, sampleRate, cfg, buffers, outCurve, outTauMin, outTauMax);
        if (r.has_value()) {
            return r;
        }
    }
    return std::nullopt;
}

std::optional<PitchResult> PitchEngine::detectWithLadderSizes(std::span<const float> data,
                                                              std::size_t offset,
                                                              double sampleRate,
                                                              double rmsFloor,
                                                              const EngineConfig& cfg,
                                                              EngineBuffers& buffers,
                                                              std::span<const std::size_t> frameSizes) {
    if (frameSizes.empty()) {
        return std::nullopt;
    }
    for (const std::size_t n : frameSizes) {
        if (offset + n > data.size()) {
            continue;
        }
        const std::span<const float> frame = data.subspan(offset, n);
        // 与上游同构：信号太弱即整体返回，不再试更长的窗（长窗装的是同一段弱信号，试了也没用）
        if (rms(frame) < rmsFloor) {
            return std::nullopt;
        }
        const auto r = detect(frame, sampleRate, cfg, buffers);
        if (r.has_value()) {
            return r;
        }
    }
    return std::nullopt;
}

} // namespace pitch

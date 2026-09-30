#include "click-voice.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pitch {
namespace {

constexpr double kPi = 3.14159265358979323846;

/// 攻击段长度（秒）：极短的线性淡入，避免起始点的直流跳变造成"噗"声。
/// 取值远小于点击本身（约 0.4 ms），听感上仍是一个瞬态。
constexpr double kAttackSeconds = 0.0004;

/// 合成音长度 = 衰减时间常数的多少倍。4τ 时包络已降到 e⁻⁴ ≈ 1.8%，
/// 再往后拖只会让相邻点击更容易重叠（高 BPM 下细分间隔可短至几十毫秒）。
constexpr double kDecayTailFactor = 4.0;

/// 尾部线性淡出的比例：保证最后一个样点一定回到 0，避免"突然截断"的咔哒声。
constexpr double kTailFadeRatio = 0.1;

/// **无状态**噪声：只取决于点击内的绝对样点下标。
///
/// 为什么不用带状态的 RNG（踩过的坑）：渲染是按块进行的，带状态 RNG 会让"同一段点击"
/// 在不同块长下得到不同样点——即"换块长就等于换声音"，既不可复现，也没法写数值断言。
/// 无状态哈希则保证：无论一次渲染 512 个样点还是 4096 个样点，结果逐位相同。
/// 副作用是每次点击的噪声完全一样——对节拍器而言这反而更好（每次拍点音色一致）。
double whiteNoise(std::size_t index) {
    std::uint64_t x = static_cast<std::uint64_t>(index) + 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    x = x ^ (x >> 31);
    return static_cast<double>(x >> 40) / 8388608.0 - 1.0;
}

/// 合成音在第 i 个样点处的取值（纯函数：同样的 i 永远给同样的值）。
double synthSample(const ClickTone& tone, double outRate, std::size_t i, std::size_t frames) {
    const double t = static_cast<double>(i) / outRate;
    const double tau = std::max(1e-4, tone.decayMs / 1000.0);
    const double env = std::exp(-t / tau);

    const double phase = 2.0 * kPi * tone.freqHz * t;
    const double fundamental = std::sin(phase);
    // 第二分音衰减更快（τ/2）：敲击类声音的高频成分本就先消失
    const double partialEnv = std::exp(-t / (tau * 0.5));
    const double partial = tone.partialGain * partialEnv * std::sin(phase * tone.partialRatio);

    const double mix =
        (1.0 - tone.noise) * (fundamental + partial) + tone.noise * whiteNoise(i);

    double value = env * mix;

    // 攻击段淡入
    if (t < kAttackSeconds) {
        value *= t / kAttackSeconds;
    }
    // 尾部淡出
    const std::size_t fadeStart =
        static_cast<std::size_t>(static_cast<double>(frames) * (1.0 - kTailFadeRatio));
    if (i >= fadeStart && frames > fadeStart) {
        const double k = static_cast<double>(frames - 1 - i) /
                         static_cast<double>(frames - fadeStart);
        value *= std::clamp(k, 0.0, 1.0);
    }
    return value * tone.gain;
}

/// 归一化时使用的参考采样率。设备采样率通常就是 48 kHz，且峰值对采样率不敏感
/// （连续波形的峰值在 44.1k/48k 下差异远小于 1%），故没必要按设备采样率各算一次。
constexpr double kNormalizeRate = 48000.0;

} // namespace

double rolePeakTarget(int role) {
    switch (role) {
    case 0:
        return 0.95;   // 强拍
    case 2:
        return 0.45;   // 细分：明显轻于拍点，避免盖住拍
    default:
        return 0.80;   // 弱拍
    }
}

ClickVoice builtinVoice(int role) {
    ClickVoice v;
    switch (role) {
    case 0:   // 强拍：更高更亮
        v.tone.freqHz = 1568.0;
        v.tone.decayMs = 34.0;
        v.tone.noise = 0.10;
        break;
    case 2:   // 细分：更闷更轻
        v.tone.freqHz = 784.0;
        v.tone.decayMs = 18.0;
        v.tone.noise = 0.16;
        break;
    default:  // 弱拍
        v.tone.freqHz = 1046.5;
        v.tone.decayMs = 28.0;
        v.tone.noise = 0.12;
        break;
    }
    v.tone.gain = 1.0;
    v.gain = 1.0;

    // 合成一遍测出峰值，再缩放 gain 使单次点击峰值恰为角色目标值（见头文件说明）
    const std::size_t frames = clickFrames(v, kNormalizeRate);
    double peak = 0.0;
    for (std::size_t i = 0; i < frames; ++i) {
        peak = std::max(peak, std::fabs(synthSample(v.tone, kNormalizeRate, i, frames)));
    }
    if (peak > 1e-9) {
        v.tone.gain = rolePeakTarget(role) / peak;
    }
    return v;
}

std::size_t clickFrames(const ClickVoice& v, double outRate) {
    if (outRate <= 0.0) {
        return 0;
    }
    if (v.usesSample()) {
        const double sampleSeconds = static_cast<double>(v.sample.size()) / v.sampleRate;
        const double seconds = std::min(sampleSeconds, std::max(0.0, v.maxSeconds));
        return static_cast<std::size_t>(std::llround(seconds * outRate));
    }
    const double tau = std::max(1e-4, v.tone.decayMs / 1000.0);
    // 至少有 8 个样点：即使衰减被设得极短，也要有一段完整包络
    return std::max<std::size_t>(
        8, static_cast<std::size_t>(std::llround(kDecayTailFactor * tau * outRate)));
}

std::size_t mixClickSlice(const ClickVoice& v, double outRate, std::size_t cursor, float* out,
                          std::size_t frames) {
    if (out == nullptr || frames == 0 || outRate <= 0.0) {
        return 0;
    }
    const std::size_t total = clickFrames(v, outRate);
    if (cursor >= total) {
        return 0;
    }
    const std::size_t count = std::min(frames, total - cursor);

    if (v.usesSample()) {
        // 线性重采样：把"点击内的样点游标"换算到样本自身的坐标
        const double ratio = v.sampleRate / outRate;
        const double lastIndex = static_cast<double>(v.sample.size() - 1);
        for (std::size_t i = 0; i < count; ++i) {
            const double pos = static_cast<double>(cursor + i) * ratio;
            const double clamped = std::min(pos, lastIndex);
            const std::size_t i0 = static_cast<std::size_t>(clamped);
            const std::size_t i1 = std::min(i0 + 1, v.sample.size() - 1);
            const double frac = clamped - static_cast<double>(i0);
            const double a = static_cast<double>(v.sample[i0]);
            const double b = static_cast<double>(v.sample[i1]);
            const double mixed = (a + (b - a) * frac) * v.gain;
            out[i] += static_cast<float>(mixed);
        }
        return count;
    }

    for (std::size_t i = 0; i < count; ++i) {
        const double value = synthSample(v.tone, outRate, cursor + i, total) * v.gain;
        out[i] += static_cast<float>(value);
    }
    return count;
}

} // namespace pitch

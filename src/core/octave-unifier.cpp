// 八度轨迹校正实现
//
// 上游对照：tools/pitch-engine.js L364-398（unifyOctaves）。
//
// 实现细节与上游的两点对齐要求：
//   1. 参考值用"置信度加权中位数"，不是朴素中位数
//   2. 折回后只改 freq（与 freqRaw 记录），**不在此处重算音名**——由 AnalysisRunner 统一重算，
//      保证"改了频率就重算派生字段"这条不变量只有一处执行（上游 pitfalls #28）

#include "octave-unifier.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace pitch {

int OctaveUnifier::apply(std::span<Frame> frames) {
    if (frames.size() < 3) {
        return 0;   // 帧太少，参考值不可靠，不做校正（与上游 `frames.length < 3` 一致）
    }

    // ① 置信度加权中位数
    struct Weighted {
        double freq;
        double weight;
    };
    std::vector<Weighted> weighted;
    weighted.reserve(frames.size());
    for (const Frame& f : frames) {
        const double w = std::max(kMinWeight, f.confidence > 0.0 ? f.confidence : kDefaultConfidence);
        weighted.push_back(Weighted{f.freq, w});
    }
    std::sort(weighted.begin(), weighted.end(),
              [](const Weighted& a, const Weighted& b) { return a.freq < b.freq; });

    double totalWeight = 0.0;
    for (const Weighted& x : weighted) {
        totalWeight += x.weight;
    }
    double median = weighted[weighted.size() >> 1].freq;   // 与上游同构：先取中位下标作兜底
    double acc = 0.0;
    for (const Weighted& x : weighted) {
        acc += x.weight;
        if (acc >= totalWeight / 2.0) {
            median = x.freq;
            break;
        }
    }
    if (!(median > 0.0)) {
        return 0;
    }

    // ② 逐帧折回
    int fixed = 0;
    for (Frame& fr : frames) {
        const double cents = 1200.0 * std::log2(fr.freq / median);
        for (int k = 1; k <= 2; ++k) {
            const double target = 1200.0 * static_cast<double>(k);
            if (std::abs(std::abs(cents) - target) < kOctaveToleranceCents) {
                // 朝参考方向折 k 个八度：低于参考则升，高于参考则降
                const double shifted = fr.freq * std::pow(2.0, (cents < 0.0 ? 1.0 : -1.0) * static_cast<double>(k));
                if (std::abs(1200.0 * std::log2(shifted / median)) < std::abs(cents)) {
                    fr.freqRaw = fr.freq;
                    fr.freq = shifted;
                    fr.octaveFixed = true;
                    ++fixed;
                }
                break;   // 与上游一致：命中一档即不再试下一档
            }
        }
    }
    return fixed;
}

} // namespace pitch

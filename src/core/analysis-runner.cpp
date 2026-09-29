// 整段音频逐帧分析实现
//
// 上游对照：tools/pitch-engine.js L438-483（analyzeBuffer）。
// 与上游的唯一结构性差异：帧集合用 std::vector<Frame> 返回，而非上游的 JS 对象数组。

#include "analysis-runner.h"

#include "note-converter.h"
#include "octave-unifier.h"
#include "pitch-engine.h"

#include <algorithm>
#include <cmath>

namespace pitch {

Analysis AnalysisRunner::analyze(std::span<const float> data,
                                 double sampleRate,
                                 std::size_t hop,
                                 const EngineConfig& cfg,
                                 const ProgressFn& onProgress,
                                 EngineBuffers* buffers) {
    Analysis out;
    if (hop == 0 || data.size() < kMaxFrame || sampleRate <= 0.0) {
        // 与上游同构：主循环要求 offset + kMaxFrame ≤ n，故不足一整个最大窗时无帧可产
        return out;
    }

    EngineBuffers local;
    EngineBuffers& work = (buffers != nullptr) ? *buffers : local;

    // 第一步：扫峰值 RMS。相对静音门槛依赖它，必须先有峰值再逐帧判定（上游 L442-449）
    double peakRms = 0.0;
    for (std::size_t pos = 0; pos + 1024 <= data.size(); pos += hop) {
        const double r = PitchEngine::rms(data.subspan(pos, 1024));
        if (r > peakRms) {
            peakRms = r;
        }
    }
    const double rmsFloor = std::max(kRmsMin, peakRms * kRmsRelMin);

    out.frames.reserve(data.size() / hop + 1);

    // 第二步：逐帧检测（窗长候选取自 cfg.frameLadder，实时链路会把它设为单个窗长）
    for (std::size_t pos = 0; pos + kMaxFrame <= data.size(); pos += hop) {
        const auto r = PitchEngine::detectWithLadderSizes(data, pos, sampleRate, rmsFloor, cfg, work,
                                                          cfg.frameLadder);
        if (!r.has_value()) {
            continue;
        }
        const NoteInfo note = NoteConverter::fromFrequency(r->freq, cfg.a4);
        Frame frame;
        frame.timeSec = static_cast<double>(pos) / sampleRate;
        frame.freqRaw = r->freq;
        frame.freq = r->freq;
        frame.noteIndex = note.noteIndex;
        frame.octave = note.octave;
        frame.cents = note.cents;
        frame.confidence = r->confidence;
        frame.rms = PitchEngine::rms(data.subspan(pos, r->frameSize));
        frame.frameSize = r->frameSize;
        frame.octaveFixed = false;
        out.frames.push_back(frame);

        if (onProgress && hop != 0 && (pos % (hop * 200) == 0)) {
            onProgress(static_cast<double>(pos) / static_cast<double>(data.size()));
        }
    }
    if (onProgress) {
        onProgress(1.0);
    }

    // 第三步：八度轨迹校正（就地改 freq）
    out.summary.octaveFixedCount = OctaveUnifier::apply(out.frames);

    // 第四步：折回后**必须**重算音名与音分。八度校正改的是 freq，若不重算，帧上仍挂着旧音名，
    // 统计时就会出现"同一段既有 C2 又有 C3"的假混合（上游 pitfalls #28，实测 42 个混合文件 vs 实际 36）。
    for (Frame& fr : out.frames) {
        const NoteInfo note = NoteConverter::fromFrequency(fr.freq, cfg.a4);
        fr.noteIndex = note.noteIndex;
        fr.octave = note.octave;
        fr.cents = note.cents;
    }

    // 第五步：汇总。整段参考音高用中位数——持续音的中位频率即其音高，比逐帧众数更抗离群
    out.summary.peakRms = peakRms;
    out.summary.rmsFloor = rmsFloor;
    if (!out.frames.empty()) {
        std::vector<double> freqs;
        freqs.reserve(out.frames.size());
        for (const Frame& fr : out.frames) {
            freqs.push_back(fr.freq);
        }
        std::sort(freqs.begin(), freqs.end());
        out.summary.medianFreq = freqs[freqs.size() >> 1];
        out.summary.medianCents = NoteConverter::fromFrequency(out.summary.medianFreq, cfg.a4).cents;
    }
    return out;
}

} // namespace pitch

// 实时链路逐帧分析实现
//
// 上游对照：
//   [上游] ../pitch-detector-web/pitch.html 的实时循环 —— 固定窗 4096、帧进 512、
//   直接单帧调 detectPitch（不走级联窗长）。
// 本实现只复刻"逐帧检测 + 音名换算"这一段；采集、平滑、保持、界面刷新属控制器职责（src/app/）。

#include "realtime-runner.h"

#include "note-converter.h"
#include "pitch-engine.h"

namespace pitch {

std::vector<RealtimeFrame> RealtimeRunner::run(std::span<const float> data,
                                               double sampleRate,
                                               std::size_t frame,
                                               std::size_t hop,
                                               const EngineConfig& cfg,
                                               EngineBuffers* buffers) {
    std::vector<RealtimeFrame> out;
    if (frame == 0 || hop == 0 || data.size() < frame || sampleRate <= 0.0) {
        return out;
    }

    EngineBuffers local;
    EngineBuffers& work = (buffers != nullptr) ? *buffers : local;
    out.reserve(data.size() / hop + 1);

    for (std::size_t pos = 0; pos + frame <= data.size(); pos += hop) {
        const auto r = PitchEngine::detect(data.subspan(pos, frame), sampleRate, cfg, work);
        if (!r.has_value()) {
            continue;   // 上游实时循环同样跳过无效帧，不产生残留读数
        }
        const NoteInfo note = NoteConverter::fromFrequency(r->freq, cfg.a4);
        RealtimeFrame f;
        f.offsetSamples = pos;
        f.timeSec = static_cast<double>(pos) / sampleRate;
        f.freq = r->freq;
        f.noteIndex = note.noteIndex;
        f.octave = note.octave;
        f.cents = note.cents;
        f.confidence = r->confidence;
        f.tau = r->tau;
        out.push_back(f);
    }
    return out;
}

} // namespace pitch

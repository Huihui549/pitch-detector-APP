#include "audio-packer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace pitch {

std::size_t packMono(const float* mono, std::size_t frames, int channels, PackFormat format,
                     float gain, char* out, std::size_t outBytes) {
    if (mono == nullptr || out == nullptr || frames == 0) {
        return 0;
    }
    if (channels != 1 && channels != 2) {
        return 0;   // 只做单声道与立体声：其它布局不猜（猜错就是声道错位，比不出声更难查）
    }

    const std::size_t bytesPerSample = (format == PackFormat::Float32) ? 4u : 2u;
    const std::size_t need = frames * static_cast<std::size_t>(channels) * bytesPerSample;
    if (outBytes < need) {
        return 0;
    }

    const std::size_t chCount = static_cast<std::size_t>(channels);
    if (format == PackFormat::Float32) {
        auto* dst = reinterpret_cast<float*>(out);
        for (std::size_t i = 0; i < frames; ++i) {
            const float v = std::clamp(mono[i] * gain, -1.0f, 1.0f);
            for (std::size_t c = 0; c < chCount; ++c) {
                dst[i * chCount + c] = v;
            }
        }
        return need;
    }

    auto* dst = reinterpret_cast<std::int16_t*>(out);
    for (std::size_t i = 0; i < frames; ++i) {
        const float v = std::clamp(mono[i] * gain, -1.0f, 1.0f);
        const auto q = static_cast<std::int16_t>(std::lround(static_cast<double>(v) * 32767.0));
        for (std::size_t c = 0; c < chCount; ++c) {
            dst[i * chCount + c] = q;
        }
    }
    return need;
}

} // namespace pitch

#include "tap-tempo.h"

#include "metronome-pattern.h"   // clampBpm / kMinBpm / kMaxBpm

#include <algorithm>
#include <cmath>

namespace pitch {

int TapTempo::tap(long long nowMs) {
    if (!m_stamps.empty()) {
        const long long gap = nowMs - m_stamps.back();
        // 停顿或时间戳异常（非单调）→ 当作重新开始：清掉历史，只留这一次
        if (gap > kResetMs || gap <= 0) {
            m_stamps.clear();
        }
    }
    m_stamps.push_back(nowMs);
    // 只保留最近 (kMaxIntervals + 1) 个时间戳：正好够算 kMaxIntervals 个间隔
    while (m_stamps.size() > static_cast<std::size_t>(kMaxIntervals + 1)) {
        m_stamps.pop_front();
    }
    recompute();
    return m_bpm;
}

void TapTempo::reset() {
    m_stamps.clear();
    m_bpm = 0;
}

void TapTempo::recompute() {
    if (m_stamps.size() < 2) {
        m_bpm = 0;
        return;
    }
    double sum = 0.0;
    int count = 0;
    for (std::size_t i = 1; i < m_stamps.size(); ++i) {
        const long long gap = m_stamps[i] - m_stamps[i - 1];
        if (gap < kMinIntervalMs || gap > kMaxIntervalMs) {
            continue;   // 太快/太慢的间隔不参与平均（多半是手抖或误触）
        }
        sum += static_cast<double>(gap);
        ++count;
    }
    if (count == 0) {
        m_bpm = 0;
        return;
    }
    const double meanMs = sum / static_cast<double>(count);
    const double bpm = 60000.0 / meanMs;
    m_bpm = clampBpm(static_cast<int>(std::lround(bpm)));
}

} // namespace pitch

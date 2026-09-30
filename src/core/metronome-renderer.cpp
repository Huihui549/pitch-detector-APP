#include "metronome-renderer.h"

#include <algorithm>
#include <cmath>

namespace pitch {

MetronomeRenderer::MetronomeRenderer() {
    for (int role = 0; role < kRoleCount; ++role) {
        m_voices[role] = builtinVoice(role);
    }
    rebuildEvents();
}

ClickVoice MetronomeRenderer::voice(int role) const {
    if (role < 0 || role >= kRoleCount) {
        return m_voices[1];
    }
    return m_voices[role];
}

void MetronomeRenderer::setVoice(int role, const ClickVoice& v) {
    if (role < 0 || role >= kRoleCount) {
        return;
    }
    m_voices[role] = v;
}

double MetronomeRenderer::samplesPerBeat() const {
    if (m_bpm <= 0 || m_sampleRate <= 0.0) {
        return 0.0;
    }
    return 60.0 / static_cast<double>(m_bpm) * m_sampleRate;
}

void MetronomeRenderer::setSampleRate(double rate) {
    m_sampleRate = (rate > 0.0) ? rate : 0.0;
    // 采样率变化 = 设备重开：旧位置与旧游标都作废
    m_beatCursor = 0.0;
    m_nextEventBeat = 0.0;
    m_eventIndex = 0;
    m_barIndex = 0;
    for (ActiveClick& a : m_active) {
        a.active = false;
    }
    m_lastTick = TickInfo{};
}

void MetronomeRenderer::setBpm(int bpm) {
    m_bpm = clampBpm(bpm);
    // 拍空间里的位置不受影响：不需要重置任何游标（这正是用拍位而非样点计数的收益）
}

void MetronomeRenderer::setPattern(const Pattern& p) {
    m_pattern = normalize(p);
    rebuildEvents();
    // 结构性改动：从**当前拍位**立即起一小节，让用户马上听到新拍号
    m_eventIndex = 0;
    m_nextEventBeat = m_beatCursor;
}

void MetronomeRenderer::rebuildEvents() {
    m_events = buildEvents(m_pattern);
}

void MetronomeRenderer::preview(int role) {
    if (role < 0 || role >= kRoleCount) {
        return;
    }
    m_previewPending[role] = true;
}

void MetronomeRenderer::spawn(int role, std::size_t offsetInBlock) {
    if (role < 0 || role >= kRoleCount) {
        return;
    }
    // 找一条空槽；都满时**覆盖最早的一条**（听感上 = 新点击盖掉拖尾，比丢弃新点击好）
    std::size_t slot = 0;
    bool found = false;
    for (std::size_t i = 0; i < kMaxOverlap; ++i) {
        if (!m_active[i].active) {
            slot = i;
            found = true;
            break;
        }
    }
    if (!found) {
        std::size_t best = 0;
        std::size_t bestCursor = 0;
        for (std::size_t i = 0; i < kMaxOverlap; ++i) {
            if (m_active[i].cursor >= bestCursor) {
                bestCursor = m_active[i].cursor;
                best = i;
            }
        }
        slot = best;
    }
    ActiveClick& a = m_active[slot];
    a.active = true;
    a.role = role;
    a.cursor = 0;
    a.offset = offsetInBlock;
}

std::size_t MetronomeRenderer::render(float* out, std::size_t frames) {
    if (out == nullptr || frames == 0) {
        return 0;
    }
    std::fill(out, out + frames, 0.0f);

    const double perBeat = samplesPerBeat();
    if (m_sampleRate <= 0.0) {
        // 没有采样率就无从摆位置：输出静音，只推进计数（调用方仍能拿到确定的行为）
        m_renderedFrames += static_cast<long long>(frames);
        return frames;
    }

    // 1) 试听（不影响时间轴，也不改 lastTick —— 界面上的拍点指示不应被试听打乱）
    for (int role = 0; role < kRoleCount; ++role) {
        if (m_previewPending[role]) {
            m_previewPending[role] = false;
            spawn(role, 0);
        }
    }

    // 2) 节拍事件：在拍空间里判断"本块是否跨过了下一个点击"
    if (!m_events.empty() && perBeat > 0.0) {
        const double beatsThisBlock = static_cast<double>(frames) / perBeat;
        const double blockEndBeat = m_beatCursor + beatsThisBlock;

        int guard = 0;
        while (m_nextEventBeat < blockEndBeat && guard++ < 4096) {
            double relBeats = m_nextEventBeat - m_beatCursor;
            if (relBeats < 0.0) {
                relBeats = 0.0;   // 理论不可达；保险起见不让它变成负偏移
            }
            std::size_t offset =
                static_cast<std::size_t>(relBeats * perBeat + 0.5);
            if (offset >= frames) {
                offset = frames - 1;
            }

            const ClickEvent& e = m_events[m_eventIndex];
            const int role = static_cast<int>(eventRole(e));
            spawn(role, offset);

            ++m_totalTicks;
            if (role >= 0 && role < kRoleCount) {
                ++m_tickCounters[role];
            }
            m_lastTick.counter = m_totalTicks - 1;
            m_lastTick.barIndex = m_barIndex;
            m_lastTick.beatIndex = e.beatIndex;
            m_lastTick.subIndex = e.subIndex;
            m_lastTick.role = role;
            m_lastTick.accent = e.accent;

            // 推进到下一个事件：差值在小节内算，跨小节时补上"末尾到下一小节首拍"的距离
            ++m_eventIndex;
            if (m_eventIndex >= m_events.size()) {
                m_eventIndex = 0;
                ++m_barIndex;
                const double barBeats = static_cast<double>(m_pattern.meter.beats);
                m_nextEventBeat += barBeats - e.beatPosition + m_events[0].beatPosition;
            } else {
                m_nextEventBeat += m_events[m_eventIndex].beatPosition - e.beatPosition;
            }
        }
        m_beatCursor = blockEndBeat;
    } else {
        // 无事件（不可能：normalize 保证每拍至少 1 个点击）或采样率异常时，仍要推进拍位
        if (perBeat > 0.0) {
            m_beatCursor += static_cast<double>(frames) / perBeat;
        }
    }

    // 3) 把所有正在发声的点击叠加进本块
    for (ActiveClick& a : m_active) {
        if (!a.active) {
            continue;
        }
        const std::size_t offset = std::min(a.offset, frames - 1);
        const std::size_t room = frames - offset;
        const ClickVoice& v = m_voices[a.role];
        const std::size_t written =
            mixClickSlice(v, m_sampleRate, a.cursor, out + offset, room);
        a.cursor += written;
        a.offset = 0;
        if (written == 0 || a.cursor >= clickFrames(v, m_sampleRate)) {
            a.active = false;
        }
    }

    m_renderedFrames += static_cast<long long>(frames);
    return frames;
}

long long MetronomeRenderer::tickCount(int role) const {
    if (role < 0 || role >= kRoleCount) {
        return 0;
    }
    return m_tickCounters[role];
}

} // namespace pitch

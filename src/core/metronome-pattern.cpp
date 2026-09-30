#include "metronome-pattern.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace pitch {
namespace {

/// 允许的分母取值。用"吸附到最近合法值"而不是夹紧：这样 --unit 17 会变成 16 而不是 2。
int normalizeUnit(int unit) {
    const int allowed[] = {2, 4, 8, 16};
    int best = allowed[0];
    int bestDiff = std::abs(unit - allowed[0]);
    for (const int candidate : allowed) {
        const int diff = std::abs(unit - candidate);
        if (diff < bestDiff) {
            best = candidate;
            bestDiff = diff;
        }
    }
    return best;
}

int clampInt(int value, int lo, int hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}

} // namespace

Pattern normalize(const Pattern& in) {
    Pattern out;
    out.meter.beats = clampInt(in.meter.beats, kMinBeats, kMaxBeats);
    out.meter.unit = normalizeUnit(in.meter.unit);
    out.subdivisions.assign(static_cast<std::size_t>(out.meter.beats), kMinSubdivision);
    for (std::size_t i = 0; i < out.subdivisions.size(); ++i) {
        if (i < in.subdivisions.size()) {
            out.subdivisions[i] =
                clampInt(in.subdivisions[i], kMinSubdivision, kMaxSubdivision);
        }
    }
    return out;
}

int eventsPerBar(const Pattern& p) {
    const Pattern n = normalize(p);
    int total = 0;
    for (const int sub : n.subdivisions) {
        total += sub;
    }
    return total;
}

int accentGroupSize(const Meter& m) {
    const Meter n = normalize(Pattern{m, {}}).meter;
    if (n.unit == 8 && n.beats >= 6 && n.beats % 3 == 0) {
        return 3;
    }
    return 1;   // 简单拍号（含 3/8、7/8、5/8 这类不可推断分组的不规则拍号）
}

bool isAccentBeat(const Pattern& p, int beatIndex) {
    if (beatIndex < 0) {
        return false;
    }
    const Pattern n = normalize(p);
    if (beatIndex >= n.meter.beats) {
        return false;
    }
    const int group = accentGroupSize(n.meter);
    // **注意 group == 1 必须单独处理**：`beatIndex % 1 == 0` 恒真，会让简单拍号每一拍都变成重音
    // （实测踩过：4/4 变成 [1,2,3,4] 全是重音，被自检立刻抓出）
    if (group <= 1) {
        return beatIndex == 0;
    }
    return (beatIndex % group) == 0;
}

std::vector<ClickEvent> buildEvents(const Pattern& p) {
    const Pattern n = normalize(p);
    std::vector<ClickEvent> events;
    events.reserve(static_cast<std::size_t>(eventsPerBar(n)));

    for (int beat = 0; beat < n.meter.beats; ++beat) {
        // 该拍的细分数一定 >= 1（normalize 保证），故不会出现"这一拍没有点击"
        const int sub = n.subdivisions[static_cast<std::size_t>(beat)];
        const double step = 1.0 / static_cast<double>(sub);
        for (int k = 0; k < sub; ++k) {
            ClickEvent e;
            e.beatPosition = static_cast<double>(beat) + step * static_cast<double>(k);
            e.beatIndex = beat;
            e.subIndex = k;
            // 重音只落在"拍的首个点"上；复合拍号的组首（第 4、7… 拍）同样算重音
            e.accent = (k == 0) && isAccentBeat(n, beat);
            e.subdivision = (k != 0);
            events.push_back(e);
        }
    }
    return events;
}

ClickRole eventRole(const ClickEvent& e) {
    if (e.accent) {
        return ClickRole::Accent;
    }
    return e.subdivision ? ClickRole::Sub : ClickRole::Beat;
}

std::vector<Meter> presetMeters() {
    // 顺序按"常用优先"排：4/4 在最前，随后是按拍数递增的常见拍号。
    // 覆盖市面节拍器的常见清单（Ticks 的 11 种口径：1/4、2/4、3/4、4/4、5/4、3/8、5/8、6/8、7/8、9/8、12/8），
    // 另加 2/2（cut time）——它在乐谱里很常见，而上述清单里没有。
    return {
        Meter{4, 4},  Meter{3, 4},  Meter{2, 4},  Meter{6, 8}, Meter{5, 4}, Meter{5, 8},
        Meter{7, 8},  Meter{9, 8},  Meter{12, 8}, Meter{1, 4}, Meter{2, 2}, Meter{3, 8},
    };
}

std::string meterLabel(const Meter& m) {
    const Meter n = normalize(Pattern{m, {}}).meter;
    return std::to_string(n.beats) + "/" + std::to_string(n.unit);
}

std::string subdivisionLabel(int subdivision) {
    switch (clampInt(subdivision, kMinSubdivision, kMaxSubdivision)) {
    case 1:
        return "整拍（四分）";
    case 2:
        return "两个八分";
    case 3:
        return "三连音";
    default:
        return "四个十六分";
    }
}

int clampBpm(int bpm) {
    return clampInt(bpm, kMinBpm, kMaxBpm);
}

std::string toString(const Pattern& p) {
    const Pattern n = normalize(p);
    std::string out = meterLabel(n.meter);
    out += ':';
    for (std::size_t i = 0; i < n.subdivisions.size(); ++i) {
        if (i != 0) {
            out += ',';
        }
        out += std::to_string(n.subdivisions[i]);
    }
    return out;
}

Pattern fromString(const std::string& text) {
    // 容错解析：任何异常输入都退回默认 4/4 整拍。理由：这个字符串来自本地配置，
    // 一旦被手改成看不懂的内容，程序应当照常能用，而不是启动即报错。
    Pattern out;
    if (text.empty()) {
        return normalize(out);
    }

    const std::size_t colon = text.find(':');
    const std::string meterPart = (colon == std::string::npos) ? text : text.substr(0, colon);
    const std::string subPart = (colon == std::string::npos) ? std::string() : text.substr(colon + 1);

    const std::size_t slash = meterPart.find('/');
    if (slash == std::string::npos) {
        return normalize(out);
    }
    out.meter.beats = std::atoi(meterPart.substr(0, slash).c_str());
    out.meter.unit = std::atoi(meterPart.substr(slash + 1).c_str());

    out.subdivisions.clear();
    std::size_t pos = 0;
    while (pos <= subPart.size() && !subPart.empty()) {
        const std::size_t comma = subPart.find(',', pos);
        const std::string token = subPart.substr(pos, comma - pos);
        if (!token.empty()) {
            out.subdivisions.push_back(std::atoi(token.c_str()));
        }
        if (comma == std::string::npos) {
            break;
        }
        pos = comma + 1;
    }
    return normalize(out);
}

} // namespace pitch

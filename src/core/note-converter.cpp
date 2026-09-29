// 频率 ↔ 十二平均律音名/音分 换算实现
//
// 上游对照：tools/pitch-engine.js L57-67（freqToNote）、L29（NOTE_NAMES）。
// 注意 `Math.floor` 对负数向 −∞ 取整，C++ 侧必须用 std::floor 而非整数除法强转（后者向 0 取整），
// 否则 C0 以下的音名会整体错一个八度。

#include "note-converter.h"

#include <cmath>

namespace pitch {

NoteInfo NoteConverter::fromFrequency(double freq, double refA4) {
    const double n = 69.0 + 12.0 * std::log2(freq / refA4);
    return fromMidi(n);
}

NoteInfo NoteConverter::fromMidi(double midi) {
    const int nearest = static_cast<int>(std::llround(midi));
    // 先取模再兜底为正值：C++ 的 % 对负数给负余数，与 JS 的 ((x % 12) + 12) % 12 写法对齐
    const int mod = ((nearest % 12) + 12) % 12;
    NoteInfo info;
    info.midi = midi;
    info.noteIndex = mod;
    info.octave = static_cast<int>(std::floor(static_cast<double>(nearest) / 12.0)) - 1;
    info.cents = (midi - static_cast<double>(nearest)) * 100.0;
    return info;
}

const char* NoteConverter::format(int noteIndex, int octave) {
    // 四槽轮转：满足"同时格式化最多 4 个音名"的界面需求，且不引入堆分配（热路径禁用动态分配）
    static constexpr int kSlots = 4;
    static char slots[kSlots][8];
    static int next = 0;
    char* out = slots[next];
    next = (next + 1) % kSlots;

    // 音名索引先归一到 0..11：调用方传来越界值时给出确定的音名，而不是越界读表
    const int safeIndex = ((noteIndex % 12) + 12) % 12;
    const char* name = kNoteNames[static_cast<std::size_t>(safeIndex)];
    int len = 0;
    for (; name[len] != '\0' && len < 3; ++len) {
        out[len] = name[len];
    }
    // 八度为负时输出 "-1" 形式（SPN 允许 C-1 这类记法）
    int value = octave;
    if (value < 0) {
        out[len++] = '-';
        value = -value;
    }
    char digits[4];
    int count = 0;
    do {
        digits[count++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value > 0 && count < 3);
    for (int i = count - 1; i >= 0; --i) {
        out[len++] = digits[i];
    }
    out[len] = '\0';
    return out;
}

} // namespace pitch

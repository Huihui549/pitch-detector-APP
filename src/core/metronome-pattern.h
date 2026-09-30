// 节拍布局模型（拍号 + 逐拍细分）—— **纯逻辑，零 Qt 依赖**
//
// 为什么单独建模而不是直接在音频线程里算：
//   "一小节里到底有几个点击、每个点击落在什么位置、谁是强拍"这件事是**可测的纯数据**。
//   把它放进 src/core 后，可以脱离 Qt / 音频设备用单测逐项核对（见 tests/core/core-tests.cpp），
//   音频层只负责"把这些事件按采样点摆到时间轴上"。反过来（在音频回调里边算边判）就没法验证。
//
// 关键概念（与市面节拍器对齐，但把"逐拍细分"做成一等公民）：
//   · Meter（拍号）：beats = 每小节几拍（分子），unit = 每拍是什么时值（分母：4=四分音符，8=八分音符…）
//   · Subdivision（细分）：把**某一拍**等分成几份。1=整拍、2=两个八分、3=三连音、4=四个十六分。
//   · 逐拍独立：subdivisions[i] 只作用于第 i 拍——这正是"4/4 的第 1 拍拆成两个八分、其余仍是四分"
//     这种用法的来源（整小节统一细分的节拍器做不到）。
//
// 关于 unit（分母）与速度的关系——**容易误解，故写死在这里**：
//   BPM 一律指"每拍"的速度，而"一拍"就是分母那个时值。
//   即 6/8 时 BPM=120 表示每分钟 120 个**八分音符**（每小节 6 个点击），不是 120 个附点四分。
//   想要"6/8 的二拍感"，就把拍号设成 2 拍并给每拍 3 细分——强拍自然落在两个大拍上。
//   这样 unit 只影响界面显示与语义说明，不影响时间轴换算（时间轴只由 BPM 与细分数决定）。
//
// 编码要求（坑 A20）：本文件必须存为 UTF-8 **无 BOM**。

#pragma once

#include <string>
#include <vector>

namespace pitch {

/// 合法区间：夹紧而不是报错（界面上的按钮/shift 总会给出界值，报错只会让交互变脆）
constexpr int kMinBeats = 1;
constexpr int kMaxBeats = 16;          ///< 16 拍足够覆盖 12/8、15/8 这类复合拍
constexpr int kMinSubdivision = 1;
constexpr int kMaxSubdivision = 4;     ///< 1=整拍 2=八分 3=三连 4=十六分
constexpr int kMinBpm = 30;
constexpr int kMaxBpm = 300;
constexpr int kDefaultBpm = 96;

/// 拍号。
struct Meter {
    int beats = 4;   ///< 分子：每小节几拍
    int unit = 4;    ///< 分母：每拍时值（2/4/8/16）
};

/// 一个小节的节拍布局。
struct Pattern {
    Meter meter{};
    /// 每拍的细分数，长度等于 meter.beats；取值 kMinSubdivision..kMaxSubdivision
    std::vector<int> subdivisions{};
};

/// 一个小节内的一个点击事件。
struct ClickEvent {
    double beatPosition = 0.0;  ///< 小节内位置，单位=拍（第 1 拍拆两个八分 → 0.0 与 0.5）
    int beatIndex = 0;          ///< 所属拍（0 基）
    int subIndex = 0;           ///< 该拍内第几个细分点（0 基）
    bool accent = false;        ///< 强拍：每小节第一拍的第一个点
    bool subdivision = false;   ///< 是否为细分点（不是所属拍的起点）
};

/// 音色角色：决定这个点击用哪种点击声（界面上的三行"音色"与之对应）。
enum class ClickRole : int {
    Accent = 0,   ///< 强拍（每小节第一拍）
    Beat = 1,     ///< 其余拍的拍点
    Sub = 2,      ///< 细分点
};

/// 每小节里的**重音分组大小**。
///
/// 复合拍号（分母 8 且拍数是 3 的倍数：6/8、9/8、12/8）按 3 个八分音符一组，
/// 组首（第 1、4、7… 拍）与第一拍同样加重音——这是 6/8 听上去"二拍感"的来源，
/// 也是 MuseScore 等工具的既有行为（实测：6/8 即使按八分计 6 次点击，仍会在第 1、4 个八分加重音）。
/// 不规则的 7/8、5/8 无法从拍号推断分组（2+2+3 / 3+2 都对），故只保留第一拍为重音，不替用户猜。
int accentGroupSize(const Meter& m);

/// 某一拍是否应加重音（组首 = 重音）。
bool isAccentBeat(const Pattern& p, int beatIndex);

/// 把拍号与细分数夹到合法区间，并把 subdivisions 长度对齐 beats（新增的拍默认整拍）。
Pattern normalize(const Pattern& in);

/// 每小节的点击事件数。
int eventsPerBar(const Pattern& p);

/// 生成一小节的点击事件（按 beatPosition 升序）。
std::vector<ClickEvent> buildEvents(const Pattern& p);

/// 事件的音色角色。
ClickRole eventRole(const ClickEvent& e);

/// 界面预置的常见拍号（顺序即界面显示顺序）。
std::vector<Meter> presetMeters();

/// 序列化：形如 "4/4:2,1,1,1"（QSettings 里以字符串保存，避免多键同步问题）。
std::string toString(const Pattern& p);

/// 反序列化：无法解析的输入返回**默认 4/4 整拍**（不抛异常，界面拿到的永远是可用的模式）。
Pattern fromString(const std::string& text);

/// 拍号的可读文本，例如 "4/4"。
std::string meterLabel(const Meter& m);

/// 细分的可读文本，例如 2 → "两个八分"。
std::string subdivisionLabel(int subdivision);

/// 一小节的总"拍位长度"（= beats；细分不改变小节长度，只改变点击密度）。
inline double barLengthInBeats(const Pattern& p) {
    return static_cast<double>(normalize(p).meter.beats);
}

/// 把 BPM 夹到合法区间。
int clampBpm(int bpm);

} // namespace pitch

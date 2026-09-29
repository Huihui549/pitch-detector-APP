// 跨语言一致性对拍实现 —— 见 cross-check.h 的真值格式说明

#include "cross-check.h"

#include "analysis-runner.h"
#include "note-converter.h"
#include "pitch-engine.h"
#include "realtime-runner.h"
#include "wav-reader.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

namespace pitch {
namespace tools {

namespace {

/* ============================ 容差口径 ============================ */

/// 频率相对差上限（verify.md B1：≤ 1e-6）。
constexpr double kFreqRelTol = 1e-6;
/// 音分绝对差上限。音分是 log2 派生量，频率的极小差异会被放大，故略放宽。
constexpr double kCentsAbsTol = 1e-3;
/// 置信度绝对差上限。
constexpr double kConfidenceAbsTol = 1e-9;
/// RMS 绝对差上限。RMS 由 float 样点求和得出，两端求和顺序不同会带来浮点噪声，
/// 1e-6 远小于任何有意义的信号差异。
constexpr double kRmsAbsTol = 1e-6;
/// 相对差的分母兜底，避免除零。
constexpr double kEps = 1e-12;

/// analysis 记录的字段数（64 字节/帧）。
constexpr std::size_t kAnalysisFieldCount = 8;
/// realtime 记录的字段数（48 字节/帧）。
constexpr std::size_t kRealtimeFieldCount = 6;
/// analysis 记录中 freq 的下标（timeSec, freqRaw, freq, cents, confidence, rms, frameSize, octaveFixed）。
constexpr std::size_t kAnalysisFreqIndex = 2;
constexpr std::size_t kAnalysisCentsIndex = 3;
constexpr std::size_t kAnalysisConfidenceIndex = 4;
constexpr std::size_t kAnalysisRmsIndex = 5;
/// realtime 记录中 freq 的下标（offsetSamples, timeSec, freq, cents, confidence, tau）。
constexpr std::size_t kRealtimeFreqIndex = 2;
constexpr std::size_t kRealtimeCentsIndex = 3;
constexpr std::size_t kRealtimeConfidenceIndex = 4;

/* ============================ 指标累积 ============================ */

struct MetricAccum {
    std::string name;
    bool relative = true;
    double tol = 0.0;
    double worst = 0.0;
    bool exceeded = false;

    /// 记录一次比对。relative 为真时用相对差（分母带兜底），否则用绝对差。
    void record(double actual, double reference) {
        const double absDiff = std::abs(actual - reference);
        const double relDiff = absDiff / std::max(std::abs(reference), kEps);
        const double score = relative ? relDiff : absDiff;
        const bool over = relative ? (relDiff > tol && absDiff > kEps) : (absDiff > tol);
        if (score > worst) {
            worst = score;
            exceeded = over;
        }
    }

    MetricReport report() const {
        MetricReport r;
        r.name = name;
        r.relative = relative;
        r.tol = tol;
        r.worst = worst;
        r.exceeded = exceeded;
        return r;
    }
};

/* ============================ 文件读取 ============================ */

std::map<std::string, std::string> readManifest(const std::string& path, bool& ok) {
    std::map<std::string, std::string> kv;
    std::ifstream in(path);
    if (!in) {
        ok = false;
        return kv;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();   // 容忍 CRLF
        }
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
    ok = true;
    return kv;
}

/// 逐字段小端读取 float64 真值文件。不使用结构体 memcpy，避免对齐与填充差异。
bool readFloat64Records(const std::string& path,
                        std::size_t fieldCount,
                        std::vector<std::vector<double>>& out,
                        std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "无法打开真值文件：" + path;
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    in.seekg(0);
    const std::size_t recordBytes = fieldCount * 8;
    if (size < 0 || (static_cast<std::size_t>(size) % recordBytes) != 0) {
        error = "真值文件长度不是记录大小的整数倍：" + path;
        return false;
    }
    const std::size_t count = static_cast<std::size_t>(size) / recordBytes;
    out.assign(count, std::vector<double>(fieldCount, 0.0));
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t f = 0; f < fieldCount; ++f) {
            unsigned char b[8] = {0, 0, 0, 0, 0, 0, 0, 0};
            in.read(reinterpret_cast<char*>(b), 8);
            if (in.gcount() != 8) {
                error = "真值文件意外截断：" + path;
                return false;
            }
            std::uint64_t u = 0;
            for (int k = 7; k >= 0; --k) {
                u = (u << 8) | static_cast<std::uint64_t>(b[k]);
            }
            double v = 0.0;
            static_assert(sizeof(double) == 8, "本项目假定 double 为 64 位");
            std::memcpy(&v, &u, sizeof(double));
            out[i][f] = v;
        }
    }
    return true;
}

/// 逐行读取音名真值文件（纯 ASCII，每行一帧）。
bool readNoteLines(const std::string& path, std::vector<std::string>& out, std::string& error) {
    std::ifstream in(path);
    if (!in) {
        error = "无法打开音名真值文件：" + path;
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        out.push_back(line);
    }
    // 末尾换行会产生一个空行，去掉它（空行不是有效帧）
    if (!out.empty() && out.back().empty()) {
        out.pop_back();
    }
    return true;
}

/* ============================ 单个素材对拍 ============================ */
/// 读入待对拍素材的 WAV 数据，失败时填 failReason。
bool loadFixtureData(const std::string& dataDir,
                     const std::map<std::string, std::string>& meta,
                     const std::string& stem,
                     WavData& wav,
                     std::string& failReason) {
    const auto it = meta.find("fixture." + stem + ".wav");
    if (it == meta.end()) {
        failReason = "清单中缺少该素材的 wav 条目";
        return false;
    }
    wav = readWavMono(dataDir + "/" + it->second);
    if (!wav.ok) {
        failReason = "WAV 读取失败：" + wav.error;
        return false;
    }
    return true;
}

FixtureReport checkOneAnalysis(const std::string& dataDir,
                               const std::map<std::string, std::string>& meta,
                               const std::string& stem) {
    FixtureReport rep;
    rep.stem = stem;

    auto itWav = meta.find("fixture." + stem + ".wav");
    auto itNote = meta.find("fixture." + stem + ".note");
    if (itWav == meta.end()) {
        rep.failReason = "清单中缺少 wav 条目";
        return rep;
    }
    rep.wavFile = itWav->second;
    rep.expectNote = (itNote != meta.end()) ? itNote->second : "?";

    WavData wav;
    if (!loadFixtureData(dataDir, meta, stem, wav, rep.failReason)) {
        return rep;
    }

    const auto itHop = meta.find("hopAnalyze");
    if (itHop == meta.end()) {
        rep.failReason = "清单中缺少 hopAnalyze";
        return rep;
    }
    const std::size_t hop = static_cast<std::size_t>(std::stoull(itHop->second));

    const EngineConfig cfg;
    const Analysis analysis = AnalysisRunner::analyze(
        std::span<const float>(wav.samples.data(), wav.samples.size()),
        wav.sampleRate, hop, cfg, {}, nullptr);
    rep.cppFrames = analysis.frames.size();

    std::vector<std::vector<double>> ref;
    std::string err;
    const auto itBin = meta.find("fixture." + stem + ".analysisBin");
    if (itBin == meta.end() ||
        !readFloat64Records(dataDir + "/" + itBin->second, kAnalysisFieldCount, ref, err)) {
        rep.failReason = err.empty() ? "清单中缺少 analysisBin" : err;
        return rep;
    }
    std::vector<std::string> refNotes;
    const auto itNoteFile = meta.find("fixture." + stem + ".analysisNote");
    if (itNoteFile == meta.end() ||
        !readNoteLines(dataDir + "/" + itNoteFile->second, refNotes, err)) {
        rep.failReason = err.empty() ? "清单中缺少 analysisNote" : err;
        return rep;
    }

    rep.refFrames = ref.size();
    if (refNotes.size() != ref.size()) {
        rep.failReason = "音名真值行数与数值真值帧数不一致";
        return rep;
    }
    if (rep.cppFrames != rep.refFrames) {
        std::ostringstream os;
        os << "帧数不一致：C++ " << rep.cppFrames << " vs 真值 " << rep.refFrames;
        rep.failReason = os.str();
        return rep;
    }

    MetricAccum freq{"freq 相对差", true, kFreqRelTol, 0.0, false};
    MetricAccum cents{"cents 绝对差", false, kCentsAbsTol, 0.0, false};
    MetricAccum conf{"confidence 绝对差", false, kConfidenceAbsTol, 0.0, false};
    MetricAccum rms{"rms 绝对差", false, kRmsAbsTol, 0.0, false};

    for (std::size_t i = 0; i < ref.size(); ++i) {
        const std::vector<double>& r = ref[i];
        const Frame& f = analysis.frames[i];
        freq.record(f.freq, r[kAnalysisFreqIndex]);
        cents.record(f.cents, r[kAnalysisCentsIndex]);
        conf.record(f.confidence, r[kAnalysisConfidenceIndex]);
        rms.record(f.rms, r[kAnalysisRmsIndex]);

        // 音名必须逐帧一致：这是抓"标签碰巧对、频率差一个八度"的唯一手段（pitfalls #26）
        const std::string cppNote = NoteConverter::format(f.noteIndex, f.octave);
        if (cppNote != refNotes[i]) {
            ++rep.noteMismatch;
        }
    }

    rep.metrics = {freq.report(), cents.report(), conf.report(), rms.report()};
    rep.pass = !freq.exceeded && !cents.exceeded && !conf.exceeded && !rms.exceeded &&
               rep.noteMismatch == 0;
    return rep;
}

FixtureReport checkOneRealtime(const std::string& dataDir,
                               const std::map<std::string, std::string>& meta,
                               const std::string& stem) {
    FixtureReport rep;
    rep.stem = stem;

    auto itWav = meta.find("fixture." + stem + ".wav");
    auto itNote = meta.find("fixture." + stem + ".note");
    if (itWav == meta.end()) {
        rep.failReason = "清单中缺少 wav 条目";
        return rep;
    }
    rep.wavFile = itWav->second;
    rep.expectNote = (itNote != meta.end()) ? itNote->second : "?";

    WavData wav;
    if (!loadFixtureData(dataDir, meta, stem, wav, rep.failReason)) {
        return rep;
    }

    const auto itFrame = meta.find("realtimeFrame");
    const auto itHop = meta.find("realtimeHop");
    if (itFrame == meta.end() || itHop == meta.end()) {
        rep.failReason = "清单中缺少 realtimeFrame 或 realtimeHop";
        return rep;
    }
    const std::size_t frame = static_cast<std::size_t>(std::stoull(itFrame->second));
    const std::size_t hop = static_cast<std::size_t>(std::stoull(itHop->second));

    const std::span<const float> data(wav.samples.data(), wav.samples.size());

    // 真值一侧：上游实时循环 = 固定 4096 窗、单帧检测（不走级联）
    const EngineConfig fixedCfg;   // frameLadder 默认即上游 full ladder，故这里用 RealtimeRunner 复刻固定窗
    const std::vector<RealtimeFrame> refSide =
        RealtimeRunner::run(data, wav.sampleRate, frame, hop, fixedCfg, nullptr);

    // 被测一侧：C++ 级联窗长（文件分析口径的窗长策略，用于量化"级联 vs 固定"的差异）
    std::vector<std::size_t> ladder(kFrameLadder.begin(), kFrameLadder.end());
    EngineBuffers buffers;
    std::vector<RealtimeFrame> cppSide;
    for (std::size_t pos = 0; pos + frame <= data.size(); pos += hop) {
        const auto r = PitchEngine::detectWithLadderSizes(data, pos, wav.sampleRate,
                                                          0.0, fixedCfg, buffers, ladder);
        if (!r.has_value()) {
            continue;
        }
        const NoteInfo note = NoteConverter::fromFrequency(r->freq, fixedCfg.a4);
        RealtimeFrame f;
        f.offsetSamples = pos;
        f.timeSec = static_cast<double>(pos) / wav.sampleRate;
        f.freq = r->freq;
        f.noteIndex = note.noteIndex;
        f.octave = note.octave;
        f.cents = note.cents;
        f.confidence = r->confidence;
        f.tau = r->tau;
        cppSide.push_back(f);
    }

    std::vector<std::vector<double>> ref;
    std::string err;
    const auto itBin = meta.find("fixture." + stem + ".realtimeBin");
    if (itBin == meta.end() ||
        !readFloat64Records(dataDir + "/" + itBin->second, kRealtimeFieldCount, ref, err)) {
        rep.failReason = err.empty() ? "清单中缺少 realtimeBin" : err;
        return rep;
    }
    std::vector<std::string> refNotes;
    const auto itNoteFile = meta.find("fixture." + stem + ".realtimeNote");
    if (itNoteFile == meta.end() ||
        !readNoteLines(dataDir + "/" + itNoteFile->second, refNotes, err)) {
        rep.failReason = err.empty() ? "清单中缺少 realtimeNote" : err;
        return rep;
    }

    // 真值（上游）必须与 C++ 复刻的固定窗路径**完全一致**——先证明复刻成立，再谈级联差异
    rep.refFrames = ref.size();
    rep.cppFrames = refSide.size();
    if (refNotes.size() != ref.size()) {
        rep.failReason = "音名真值行数与数值真值帧数不一致";
        return rep;
    }
    if (rep.cppFrames != rep.refFrames) {
        std::ostringstream os;
        os << "上游固定窗复刻的帧数不一致：C++ " << rep.cppFrames << " vs 真值 " << rep.refFrames;
        rep.failReason = os.str();
        return rep;
    }

    MetricAccum freq{"freq 相对差", true, kFreqRelTol, 0.0, false};
    MetricAccum cents{"cents 绝对差", false, kCentsAbsTol, 0.0, false};
    MetricAccum conf{"confidence 绝对差", false, kConfidenceAbsTol, 0.0, false};
    for (std::size_t i = 0; i < ref.size(); ++i) {
        const std::vector<double>& r = ref[i];
        const RealtimeFrame& f = refSide[i];
        freq.record(f.freq, r[kRealtimeFreqIndex]);
        cents.record(f.cents, r[kRealtimeCentsIndex]);
        conf.record(f.confidence, r[kRealtimeConfidenceIndex]);
        const std::string cppNote = NoteConverter::format(f.noteIndex, f.octave);
        if (cppNote != refNotes[i]) {
            ++rep.noteMismatch;
        }
    }
    rep.metrics = {freq.report(), cents.report(), conf.report()};

    // 另报一项：级联窗长与固定 4096 的音名差异（已知口径差异，不是移植错误）
    rep.ladderVsFixedTotal = std::min(cppSide.size(), refSide.size());
    for (std::size_t i = 0; i < rep.ladderVsFixedTotal; ++i) {
        const std::string a = NoteConverter::format(cppSide[i].noteIndex, cppSide[i].octave);
        const std::string b = NoteConverter::format(refSide[i].noteIndex, refSide[i].octave);
        if (a != b) {
            ++rep.ladderVsFixedNoteDiff;
        }
    }

    rep.pass = !freq.exceeded && !cents.exceeded && !conf.exceeded && rep.noteMismatch == 0;
    return rep;
}

/* ============================ 报告输出 ============================ */

std::string formatDouble(double v) {
    std::ostringstream os;
    os << std::scientific << std::setprecision(3) << v;
    return os.str();
}

void printFixtureReport(const FixtureReport& r, CheckMode mode) {
    const char* flag = r.pass ? "PASS" : "FAIL";
    std::cout << "[" << flag << "] " << r.wavFile;
    if (!r.expectNote.empty()) {
        std::cout << "  期望音名 " << r.expectNote;
    }
    std::cout << "\n";

    if (!r.failReason.empty()) {
        std::cout << "        " << r.failReason << "\n";
        return;
    }
    std::cout << "        帧数 C++ " << r.cppFrames << " / 真值 " << r.refFrames
              << "，音名不一致 " << r.noteMismatch << " 帧\n";
    for (const MetricReport& m : r.metrics) {
        std::cout << "        " << m.name << " 最大 " << formatDouble(m.worst)
                  << (m.relative ? "（相对）" : "（绝对）")
                  << "，容差 " << formatDouble(m.tol)
                  << (m.exceeded ? "  ← 超容差" : "") << "\n";
    }
    if (mode == CheckMode::Realtime) {
        std::cout << "        [口径差异，非错误] 级联窗长 vs 上游固定窗 音名不同 "
                  << r.ladderVsFixedNoteDiff << " / " << r.ladderVsFixedTotal << " 帧\n";
    }
}

} // namespace

CrossCheckSummary runCrossCheck(const CrossCheckOptions& options) {
    CrossCheckSummary summary;

    bool manifestOk = false;
    const std::map<std::string, std::string> meta =
        readManifest(options.dataDir + "/reference.txt", manifestOk);
    if (!manifestOk) {
        std::cout << "[FAIL] 读不到清单：" << options.dataDir << "/reference.txt\n";
        std::cout << "       请先在 [PC] 仓库根目录执行：node tools/gen-test-fixtures.mjs\n";
        return summary;
    }

    const auto itCount = meta.find("fixtureCount");
    if (itCount == meta.end()) {
        std::cout << "[FAIL] 清单缺少 fixtureCount\n";
        return summary;
    }
    const std::size_t count = static_cast<std::size_t>(std::stoull(itCount->second));

    // 从清单里收集素材顺序（fixture.<stem>.wav 的行序即声明顺序）
    std::vector<std::string> stems;
    for (const auto& kv : meta) {
        const std::string prefix = "fixture.";
        const std::string suffix = ".wav";
        if (kv.first.size() > prefix.size() + suffix.size() &&
            kv.first.compare(0, prefix.size(), prefix) == 0 &&
            kv.first.compare(kv.first.size() - suffix.size(), suffix.size(), suffix) == 0) {
            stems.push_back(kv.first.substr(prefix.size(),
                                            kv.first.size() - prefix.size() - suffix.size()));
        }
    }
    if (stems.size() != count) {
        std::cout << "[FAIL] 清单素材条目数与 fixtureCount 不符（" << stems.size() << " vs "
                  << count << "）\n";
        return summary;
    }

    if (!options.onlyFixtures.empty()) {
        std::vector<std::string> filtered;
        for (const std::string& s : stems) {
            if (std::find(options.onlyFixtures.begin(), options.onlyFixtures.end(), s) !=
                options.onlyFixtures.end()) {
                filtered.push_back(s);
            }
        }
        stems.swap(filtered);
    }

    std::cout << "跨语言一致性对拍（C++ vs 上游 JS 引擎）\n";
    std::cout << "  真值目录: " << options.dataDir << "\n";
    std::cout << "  模式    : " << (options.mode == CheckMode::Analysis ? "analysis（文件分析，级联窗长）"
                                                                     : "realtime（实时链路）")
              << "\n";
    std::cout << "  素材数  : " << stems.size() << "\n\n";

    summary.fixtures = stems.size();
    for (const std::string& stem : stems) {
        const FixtureReport r = (options.mode == CheckMode::Analysis)
                                    ? checkOneAnalysis(options.dataDir, meta, stem)
                                    : checkOneRealtime(options.dataDir, meta, stem);
        printFixtureReport(r, options.mode);
        if (r.pass) {
            ++summary.passed;
        }
    }

    std::cout << "\n汇总: " << summary.passed << " / " << summary.fixtures << " 通过\n";
    summary.ok = (summary.passed == summary.fixtures) && summary.fixtures > 0;
    return summary;
}

} // namespace tools
} // namespace pitch

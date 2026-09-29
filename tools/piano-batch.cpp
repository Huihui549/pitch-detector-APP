// 钢琴 88 键素材批量分析（验收工具）—— 实现
//
// 目的：用**本项目的 C++ 引擎**逐个分析真实钢琴素材，给出可核对的命中报告。
// 这是上游 `tools/test-single-note.mjs` + `audio-test-report.md` 的 C++ 等价物——
// 上游的 84/88 基线就是这么量出来的；本项目要能复现同一口径。
//
// 判定口径（与上游一致，**双条件**，见 pitfalls #26）：
//   命中 = 众数音名正确 **且** |中位偏差| < 50 音分
//   只查音名会被"标签碰巧对、频率却差一个八度"的假命中混过去。
//
// 期望音名取自**文件名**（`tone (40) - C4.wav` → C4）：素材已由 tools/rename-piano.mjs
// 按"编号连续半音"补后缀，文件名即标准答案。
//
// 实现只用 C++ 标准库（与 cross-check / core-tests 一致，不依赖 Qt）。

#include "piano-batch.h"

#include "analysis-runner.h"
#include "note-converter.h"
#include "wav-reader.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
// 目录枚举与路径处理一律用 Windows 宽字符 API，**不要用 std::filesystem 的窄字符接口**：
// 中文路径会抛 `filesystem error: Cannot convert character sequence: Illegal byte sequence`
// （实测踩过，见坑 A29）。本项目素材目录名就是中文，属必经之路。
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace pitch {
namespace tools {

namespace {

/// 单个素材的分析结果。
struct ItemResult {
    std::string file;
    std::string expectNote;    ///< 文件名里的期望音名（空 = 无法解析）
    std::string modeNote;      ///< 众数音名（逐帧音名的众数）
    std::string medianNote;    ///< 中位频率对应的音名
    double medianFreq = 0.0;
    double medianCents = 0.0;
    int frames = 0;
    int distinctNotes = 0;
    bool hit = false;
};

/// 从文件名解析期望音名：形如 `... - C4.wav` / `... - A#2.wav`。
std::string expectedNoteFromName(const std::string& name) {
    const std::string suffix = ".wav";
    std::string stem = name;
    if (stem.size() > suffix.size() &&
        stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0) {
        stem = stem.substr(0, stem.size() - suffix.size());
    }
    // 找最后一个 " - "，其后即音名
    const std::string sep = " - ";
    const std::size_t pos = stem.rfind(sep);
    if (pos == std::string::npos) {
        return {};
    }
    std::string note = stem.substr(pos + sep.size());
    // 去掉首尾空白
    while (!note.empty() && (note.front() == ' ' || note.front() == '\t')) {
        note.erase(note.begin());
    }
    while (!note.empty() && (note.back() == ' ' || note.back() == '\t')) {
        note.pop_back();
    }
    // 合法性：A-G 开头，可带 #，后接（可带负号的）八度数字
    if (note.empty() || note[0] < 'A' || note[0] > 'G') {
        return {};
    }
    return note;
}

/// UTF-8 窄字符串 → 宽字符串（Windows）。
#if defined(_WIN32)
std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int need = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                         static_cast<int>(utf8.size()), nullptr, 0);
    if (need <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(need), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), out.data(), need);
    return out;
}
#endif

/// 列出目录下的 .wav 文件（返回完整路径，UTF-8 编码；按文件名排序）。
///
/// Windows 上走 FindFirstFileW/FindNextFileW（宽字符）：素材目录名常含中文，
/// 窄字符接口会直接失败或抛异常。
std::vector<std::string> listWavFiles(const std::string& dir, bool& ok, std::string& error) {
    std::vector<std::string> out;
    ok = false;
#if defined(_WIN32)
    const std::wstring wideDir = utf8ToWide(dir);
    if (wideDir.empty()) {
        error = "路径不是合法 UTF-8：" + dir;
        return out;
    }
    WIN32_FIND_DATAW found{};
    const std::wstring pattern = wideDir + L"\\*.wav";
    HANDLE handle = FindFirstFileW(pattern.c_str(), &found);
    if (handle == INVALID_HANDLE_VALUE) {
        // 区分"目录不存在"与"目录里没有 wav"：前者要报错，后者返回空列表即可
        const DWORD attrs = GetFileAttributesW(wideDir.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            error = "目录不存在或不是目录：" + dir;
            return out;
        }
        ok = true;   // 目录存在但没有 .wav
        return out;
    }
    std::vector<std::wstring> names;
    do {
        const std::wstring name = found.cFileName;
        if (name == L"." || name == L"..") {
            continue;
        }
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        // 只收 .wav（大小写不敏感）
        if (name.size() > 4) {
            std::wstring tail = name.substr(name.size() - 4);
            for (wchar_t& c : tail) {
                c = static_cast<wchar_t>(towlower(c));
            }
            if (tail == L".wav") {
                names.push_back(name);
            }
        }
    } while (FindNextFileW(handle, &found));
    FindClose(handle);

    // 宽 → UTF-8，并拼出完整路径
    for (const std::wstring& name : names) {
        const std::wstring full = wideDir + L"\\" + name;
        const int need = WideCharToMultiByte(CP_UTF8, 0, full.c_str(), static_cast<int>(full.size()),
                                             nullptr, 0, nullptr, nullptr);
        if (need <= 0) {
            continue;
        }
        std::string utf8(static_cast<std::size_t>(need), '\0');
        WideCharToMultiByte(CP_UTF8, 0, full.c_str(), static_cast<int>(full.size()), utf8.data(),
                            need, nullptr, nullptr);
        out.push_back(utf8);
    }
#else
    DIR* d = opendir(dir.c_str());
    if (d == nullptr) {
        error = "无法打开目录：" + dir;
        return out;
    }
    while (struct dirent* e = readdir(d)) {
        std::string name = e->d_name;
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".wav") == 0) {
            out.push_back(dir + "/" + name);
        }
    }
    closedir(d);
#endif
    std::sort(out.begin(), out.end());
    ok = true;
    return out;
}

/// 只取文件名部分（去掉目录），并保证是 UTF-8 文本（供报告与比对使用）。
std::string baseName(const std::string& path) {
    const std::size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

/// UTF-8 字符串 → 宽字符串（读取文件用）。
#if defined(_WIN32)
std::wstring toWidePath(const std::string& utf8) {
    return utf8ToWide(utf8);
}
#endif

} // namespace

int runPianoBatch(const PianoBatchOptions& options) {
    if (options.dir.empty()) {
        std::printf("[FAIL] 必须指定 --dir <素材目录>\n");
        return 2;
    }

    bool ok = false;
    std::string error;
    const std::vector<std::string> files = listWavFiles(options.dir, ok, error);
    if (!ok) {
        std::printf("[FAIL] %s\n", error.c_str());
        return 2;
    }
    if (files.empty()) {
        std::printf("[FAIL] 目录下没有 .wav 文件：%s\n", options.dir.c_str());
        return 2;
    }

    std::printf("钢琴素材批量分析（C++ 引擎，判定口径：众数音名正确 且 |偏差| < 50 音分）\n");
    std::printf("  素材目录：%s\n", options.dir.c_str());
    std::printf("  文件数  ：%zu\n\n", files.size());

    const EngineConfig cfg;
    constexpr std::size_t kHop = 441;   // 10 ms，与上游文件分析口径一致

    std::vector<ItemResult> results;
    results.reserve(files.size());

    const auto t0 = std::chrono::steady_clock::now();
    std::size_t done = 0;
    for (const std::string& path : files) {
        const std::string name = baseName(path);
        ItemResult item;
        item.file = name;
        item.expectNote = expectedNoteFromName(name);

        const WavData wav = readWavMonoW(toWidePath(path));
        if (!wav.ok) {
            std::printf("  [读取失败] %s：%s\n", name.c_str(), wav.error.c_str());
            results.push_back(item);
            continue;
        }

        const Analysis analysis = AnalysisRunner::analyze(
            std::span<const float>(wav.samples.data(), wav.samples.size()),
            wav.sampleRate, kHop, cfg, {}, nullptr);

        item.frames = static_cast<int>(analysis.frames.size());
        if (!analysis.frames.empty()) {
            // 众数音名（判定用）与不同音名数（八度抖动的度量）
            std::map<std::string, int> counts;
            for (const Frame& f : analysis.frames) {
                counts[NoteConverter::format(f.noteIndex, f.octave)]++;
            }
            int best = 0;
            for (const auto& kv : counts) {
                if (kv.second > best) {
                    best = kv.second;
                    item.modeNote = kv.first;
                }
            }
            item.distinctNotes = static_cast<int>(counts.size());

            item.medianFreq = analysis.summary.medianFreq;
            item.medianCents = analysis.summary.medianCents;
            const NoteInfo mn = NoteConverter::fromFrequency(analysis.summary.medianFreq, cfg.a4);
            item.medianNote = NoteConverter::format(mn.noteIndex, mn.octave);

            item.hit = !item.expectNote.empty() && item.modeNote == item.expectNote &&
                       std::abs(item.medianCents) < 50.0;
        }

        results.push_back(item);

        ++done;
        const char* flag = item.hit ? "命中" : "未中";
        // 进度打到 **stderr**：stdout 在重定向到文件时是全缓冲的，长时间看不到进度会误以为卡死
        // （实测踩过：后台跑了几分钟日志却是 0 字节）。stderr 默认无缓冲，能实时落盘。
        std::fprintf(stderr, "  [%s] %-28s 期望 %-4s 众数 %-4s 中位 %8.2f Hz (%+6.1f 音分) 帧 %4d 音名数 %d\n",
                     flag, item.file.c_str(), item.expectNote.c_str(), item.modeNote.c_str(),
                     item.medianFreq, item.medianCents, item.frames, item.distinctNotes);
        std::fflush(stderr);
    }

    const auto t1 = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(t1 - t0).count();

    // ---- 汇总 ----
    int hits = 0;
    int mixed = 0;        ///< 含多个音名的文件数（八度抖动度量）
    int unparsed = 0;     ///< 文件名无音名后缀
    std::vector<double> hitCents;
    for (const ItemResult& r : results) {
        if (r.expectNote.empty()) {
            ++unparsed;
            continue;
        }
        if (r.hit) {
            ++hits;
            hitCents.push_back(r.medianCents);
        }
        if (r.distinctNotes > 1) {
            ++mixed;
        }
    }
    std::sort(hitCents.begin(), hitCents.end());
    const double medianOfHits = hitCents.empty() ? 0.0 : hitCents[hitCents.size() / 2];

    std::printf("\n================ 汇总 ================\n");
    std::printf("文件数            ：%zu\n", results.size());
    std::printf("可判定（有后缀）  ：%zu\n", results.size() - static_cast<std::size_t>(unparsed));
    if (unparsed > 0) {
        std::printf("无音名后缀（未计入判定）：%d\n", unparsed);
    }
    std::printf("命中（双条件）    ：%d / %zu", hits, results.size() - static_cast<std::size_t>(unparsed));
    const double denom = static_cast<double>(results.size() - static_cast<std::size_t>(unparsed));
    if (denom > 0) {
        std::printf("（%.1f%%）", 100.0 * hits / denom);
    }
    std::printf("\n");
    std::printf("命中键偏差中位    ：%.1f 音分\n", medianOfHits);
    std::printf("含多个音名的文件  ：%d（八度抖动的度量）\n", mixed);
    std::printf("耗时              ：%.1f s\n", seconds);

    // ---- 未命中明细（逐个列出，便于定位是算法问题还是素材问题）----
    std::printf("\n未命中明细：\n");
    bool anyMiss = false;
    for (const ItemResult& r : results) {
        if (r.expectNote.empty()) {
            continue;
        }
        if (!r.hit) {
            anyMiss = true;
            std::printf("  · %-28s 期望 %-4s 众数 %-4s 中位音名 %-4s 中位 %8.2f Hz（%+6.1f 音分）帧 %d\n",
                        r.file.c_str(), r.expectNote.c_str(), r.modeNote.c_str(),
                        r.medianNote.c_str(), r.medianFreq, r.medianCents, r.frames);
        }
    }
    if (!anyMiss) {
        std::printf("  （无）\n");
    }

    // ---- 可选输出：CSV ----
    if (!options.csvPath.empty()) {
        std::ofstream csv(options.csvPath);
        if (!csv) {
            std::printf("[WARN] 无法写入 CSV：%s\n", options.csvPath.c_str());
        } else {
            csv << "file,expectNote,modeNote,medianNote,medianFreq,medianCents,frames,distinctNotes,hit\n";
            for (const ItemResult& r : results) {
                csv << r.file << ',' << r.expectNote << ',' << r.modeNote << ',' << r.medianNote << ','
                    << r.medianFreq << ',' << r.medianCents << ',' << r.frames << ','
                    << r.distinctNotes << ',' << (r.hit ? 1 : 0) << '\n';
            }
            std::printf("CSV 已写入：%s\n", options.csvPath.c_str());
        }
    }

    // ---- 可选输出：Markdown 报告 ----
    if (!options.mdPath.empty()) {
        std::ofstream md(options.mdPath);
        if (!md) {
            std::printf("[WARN] 无法写入 Markdown：%s\n", options.mdPath.c_str());
        } else {
            md << "# 钢琴 88 键真实素材 —— C++ 引擎测试报告\n\n";
            md << "> 由 `bin/piano-batch.exe --dir \"<素材目录>\" --md <本文件>` 生成。\n";
            md << "> 判定口径（与上游一致，双条件）：**众数音名正确 且 |中位偏差| < 50 音分**"
                  "（只查音名会被\"标签碰巧对、频率差一个八度\"的假命中混过去，pitfalls #26）。\n\n";
            md << "## 汇总\n\n";
            md << "| 指标 | 值 |\n|---|---|\n";
            md << "| 文件数 | " << results.size() << " |\n";
            md << "| 命中 | **" << hits << " / " << static_cast<int>(denom) << "**";
            if (denom > 0) {
                md << "（" << std::fixed << std::setprecision(1) << (100.0 * hits / denom) << "%）";
            }
            md << " |\n";
            md << "| 命中键偏差中位 | " << std::setprecision(1) << medianOfHits << " 音分 |\n";
            md << "| 含多个音名的文件 | " << mixed << " |\n";
            md << "| 耗时 | " << std::setprecision(1) << seconds << " s |\n\n";
            md << "## 逐文件结果\n\n";
            md << "| 文件 | 期望音名 | 众数音名 | 中位音名 | 中位频率(Hz) | 中位偏差(音分) | 帧数 | 音名数 | 命中 |\n";
            md << "|---|---|---|---|---|---|---|---|---|\n";
            for (const ItemResult& r : results) {
                md << "| " << r.file << " | " << r.expectNote << " | " << r.modeNote << " | "
                   << r.medianNote << " | " << std::fixed << std::setprecision(2) << r.medianFreq
                   << " | " << std::setprecision(1) << r.medianCents << " | " << r.frames
                   << " | " << r.distinctNotes << " | " << (r.hit ? "是" : "**否**") << " |\n";
            }
            std::printf("Markdown 报告已写入：%s\n", options.mdPath.c_str());
        }
    }

    return hits;
}

} // namespace tools
} // namespace pitch

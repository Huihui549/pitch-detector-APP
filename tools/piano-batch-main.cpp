// 钢琴 88 键素材批量分析工具入口
//
// 用法：
//   piano-batch --dir "<素材目录>" [--csv <输出.csv>] [--md <输出.md>]
//
// 退出码：0 = 有命中；1 = 无命中；2 = 参数或路径错误。
//
// **命令行编码（Windows，关键）**：素材路径常含中文。Windows 上传给 main 的 argv 是按
// **本地 ANSI 代码页**（本机为 GBK）编码的窄字符串，若直接当 UTF-8 解析会得到乱码，
// 表现为"目录不存在"（实测踩过，见坑 A29）。故这里改用 GetCommandLineW +
// CommandLineToArgvW 取**宽字符**参数，再统一转成 UTF-8 交给内部逻辑。

#include "piano-batch.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

/// 宽字符 → UTF-8。
#if defined(_WIN32)
std::string wideToUtf8(const std::wstring& w) {
    if (w.empty()) {
        return {};
    }
    const int need = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (need <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(need), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), out.data(), need,
                        nullptr, nullptr);
    return out;
}
#endif

/// 取得 UTF-8 编码的参数列表（Windows 走宽字符命令行，其它平台直接用 argv）。
std::vector<std::string> utf8Arguments(int argc, char** argv) {
    std::vector<std::string> args;
#if defined(_WIN32)
    int wideArgc = 0;
    LPWSTR* wideArgv = CommandLineToArgvW(GetCommandLineW(), &wideArgc);
    if (wideArgv != nullptr) {
        for (int i = 0; i < wideArgc; ++i) {
            args.push_back(wideToUtf8(wideArgv[i]));
        }
        LocalFree(wideArgv);
        return args;
    }
#endif
    for (int i = 0; i < argc; ++i) {
        args.push_back(argv[i]);
    }
    return args;
}

void printUsage() {
    std::printf("用法: piano-batch --dir \"<素材目录>\" [--csv <输出.csv>] [--md <输出.md>]\n");
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args = utf8Arguments(argc, argv);
    pitch::tools::PianoBatchOptions options;

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        }
        if (arg == "--dir" && i + 1 < args.size()) {
            options.dir = args[++i];
        } else if (arg.rfind("--dir=", 0) == 0) {
            options.dir = arg.substr(6);
        } else if (arg == "--csv" && i + 1 < args.size()) {
            options.csvPath = args[++i];
        } else if (arg.rfind("--csv=", 0) == 0) {
            options.csvPath = arg.substr(6);
        } else if (arg == "--md" && i + 1 < args.size()) {
            options.mdPath = args[++i];
        } else if (arg.rfind("--md=", 0) == 0) {
            options.mdPath = arg.substr(5);
        } else {
            std::printf("[FAIL] 未知参数：%s\n", arg.c_str());
            printUsage();
            return 2;
        }
    }

    const int hits = pitch::tools::runPianoBatch(options);
    if (hits < 0) {
        return 2;
    }
    return hits > 0 ? 0 : 1;
}

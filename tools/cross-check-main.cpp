// 跨语言一致性对拍工具入口
//
// 用法：
//   cross-check --data <真值目录> [--mode=analysis|realtime] [--only=stem1,stem2]
//
// 退出码：0 = 全部通过；1 = 有素材未通过；2 = 参数或真值缺失。
// 真值目录的生成见 tools/gen-test-fixtures.mjs（需要 Node，[PC] 仓库根目录执行）。

#include "cross-check.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

void printUsage() {
    std::cout << "用法: cross-check --data <真值目录> [--mode=analysis|realtime] "
                 "[--only=stem1,stem2]\n"
                 "\n"
                 "  --data   真值目录（含 reference.txt 与各 .f64 真值），必填\n"
                 "  --mode   对拍模式，默认 analysis\n"
                 "  --only   只跑指定素材（stem，即去掉 .wav 的文件名）\n"
                 "\n"
                 "示例: cross-check --data tests/data --mode=analysis\n";
}

std::vector<std::string> splitComma(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (const char c : s) {
        if (c == ',') {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) {
        out.push_back(cur);
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    pitch::tools::CrossCheckOptions options;
    bool hasData = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        }
        if (arg.rfind("--data=", 0) == 0) {
            options.dataDir = arg.substr(7);
            hasData = true;
        } else if (arg == "--data") {
            if (i + 1 >= argc) {
                std::cout << "[FAIL] --data 后面缺少路径\n";
                return 2;
            }
            options.dataDir = argv[++i];
            hasData = true;
        } else if (arg.rfind("--mode=", 0) == 0) {
            const std::string m = arg.substr(7);
            if (m == "analysis") {
                options.mode = pitch::tools::CheckMode::Analysis;
            } else if (m == "realtime") {
                options.mode = pitch::tools::CheckMode::Realtime;
            } else {
                std::cout << "[FAIL] 未知模式: " << m << "（可选 analysis / realtime）\n";
                return 2;
            }
        } else if (arg.rfind("--only=", 0) == 0) {
            options.onlyFixtures = splitComma(arg.substr(7));
        } else {
            std::cout << "[FAIL] 未知参数: " << arg << "\n";
            printUsage();
            return 2;
        }
    }

    if (!hasData || options.dataDir.empty()) {
        std::cout << "[FAIL] 必须指定 --data <真值目录>\n";
        printUsage();
        return 2;
    }

    const pitch::tools::CrossCheckSummary summary = pitch::tools::runCrossCheck(options);
    return summary.ok ? 0 : 1;
}

// WAV 读取（未压缩 PCM，下混为单声道）
//
// **量化口径必须与上游一致**（否则跨语言对拍会出现无法归因的幅度差异）：
//   [PC] D:\dev_project\pitch-detector\tools\wav-read.mjs
//     8 bit  → (byte − 128) / 128
//     16 bit → int16 / 32768      ← 注意是 32768 而不是 32767
//     32 bit → int32 / 2147483648
//   多声道 → 逐声道求和后除以声道数
//
// 只读未压缩 PCM（format = 1 或 0xFFFE）；压缩格式另找解码路径，不在此处猜。

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pitch {

/// WAV 读取结果。
struct WavData {
    std::vector<float> samples;  ///< 单声道样点
    double sampleRate = 0.0;     ///< 采样率（Hz）
    int channels = 0;            ///< 原始声道数
    int bits = 0;                ///< 原始位深
    bool ok = false;             ///< 读取是否成功
    std::string error;           ///< 失败原因（成功时为空）
};

/// 读取未压缩 PCM WAV 并下混为单声道。
///
/// @param path 文件路径（窄字符）。**仅适用于纯 ASCII 路径**：
///             中文等非 ASCII 路径在 Windows 上按本地代码页解释，会打不开文件。
///             非 ASCII 路径请用下面的宽字符重载（见坑 A29）。
/// @return 结果；失败时 ok=false 且 error 给出原因（调用方必须检查，不得静默当空数据用）
WavData readWavMono(const std::string& path);

#if defined(_WIN32)
/// 读取未压缩 PCM WAV（宽字符路径版本）。
///
/// 为什么需要它：Windows 上 `std::ifstream(const char*)` 按当前 ANSI 代码页解释路径，
/// 于是"中文目录/中文文件名"必然打不开。本项目 Qt 侧用 `QString::toStdWString()` 传进来，
/// 既保持 src/io **零 Qt 依赖**，又让中文路径可用。
WavData readWavMonoW(const std::wstring& path);
#endif

} // namespace pitch

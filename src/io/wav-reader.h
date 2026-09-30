// WAV 读取（未压缩 PCM，下混为单声道）
//
// **量化口径必须与上游一致**（否则跨语言对拍会出现无法归因的幅度差异）：
//   [上游] ../pitch-detector-web/tools/wav-read.mjs
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
/// @param path 文件路径，**必须是 UTF-8 编码**（跨平台统一口径）：
///             - Android / Linux：文件名本就是 UTF-8 字节，直接交给窄字符 `ifstream`；
///             - Windows：窄字符 `ifstream` 按 ANSI 代码页解释路径，中文必然打不开，
///               故内部先转 UTF-16 再走宽字符 API（坑 A29）。
///             Qt 侧统一用 `path.toUtf8().toStdString()` 传入；
///             **不要**用 `toStdString()`/`toLocal8Bit()`（那是本地代码页），也不要再调宽字符版。
/// @return 结果；失败时 ok=false 且 error 给出原因（调用方必须检查，不得静默当空数据用）
WavData readWavMono(const std::string& path);

#if defined(_WIN32)
/// 读取未压缩 PCM WAV（宽字符路径版本）。**仅 Windows 存在**：
/// 非 Windows 上 `wchar_t` 的宽度与编码都不同（Android 是 UTF-32），Qt 侧一律走上面的 UTF-8 入口。
///
/// 现有调用方只有 Windows 专有工具（`tools/piano-batch`，它从宽字符命令行取路径）。
WavData readWavMonoW(const std::wstring& path);
#endif

} // namespace pitch

// 音频文件解码（任意主流格式 → 单声道 float）
//
// 为什么需要它：原来的文件分析只吃"未压缩 PCM WAV"（`readWavMono`），而用户手上的素材
// 绝大多数是 mp3/m4a/aac/ogg——只认 WAV 会让"录音分析"这个功能名不副实。
//
// 两级策略（沿用节拍器自定义音色那套已验证的做法，现在收成一处，避免两份解码代码漂移）：
//   ① **未压缩 PCM WAV** → 直接走 `src/io` 的 `readWavMono()`：不依赖 Qt Multimedia，最可靠、无重采样
//   ② **其余格式** → `QAudioDecoder`（Qt Multimedia；本项目后端是 FFmpeg）
// 两级都不行时给出**可操作**的原因，而不是笼统的"解码失败"。
//
// 分层：src/audio → src/core / src/io；不依赖 UI。
//
// 编码要求（坑 A20）：本文件必须存为 UTF-8 **无 BOM**。

#pragma once

#include <QString>

#include <vector>

namespace pitch {

/// 解码结果。
struct DecodedAudio {
    std::vector<float> samples;  ///< 单声道样点（多声道已下混）
    double sampleRate = 0.0;     ///< 采样率（Hz）
    bool ok = false;             ///< 是否成功
    QString error;               ///< 失败原因（面向用户的中文说明）
    /// 实际走通的路径说明（如"PCM WAV 直读"/"解码器"）：显示给用户，也便于排查"为什么音质不对"
    QString route;
};

/// 解码音频文件为单声道 float。
/// @param path 本地路径（**UTF-8 由 Qt 侧保证**；内部按需转换）
DecodedAudio decodeAudioFile(const QString& path);

/// 文件选择对话框用的过滤器文本（"主流格式"口径：只列常用，不堆一长串冷门后缀）。
QString audioFileFilter();

} // namespace pitch

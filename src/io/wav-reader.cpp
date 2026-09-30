// WAV 读取实现
//
// 上游对照：[上游] ../pitch-detector-web/tools/wav-read.mjs（readWavMono）
// 与上游的三点一致要求：
//   1. 逐块扫描 chunk，不假设 fmt 与 data 的先后顺序
//   2. chunk 长度为奇数时跳过 1 字节填充
//   3. 位深换算的分母固定为 2^(bits−1) 的**正数**（16 bit 用 32768，不是 32767）

#include "wav-reader.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

#if defined(_WIN32)
// Windows 原生文件 API：readWavMonoW 用它按 UTF-16 路径打开文件
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace pitch {

namespace {

/// 按小端读无符号整数。WAV 一律小端，不随主机字节序变化。
std::uint32_t readU32LE(std::istream& in) {
    unsigned char b[4] = {0, 0, 0, 0};
    in.read(reinterpret_cast<char*>(b), 4);
    return static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8) |
           (static_cast<std::uint32_t>(b[2]) << 16) | (static_cast<std::uint32_t>(b[3]) << 24);
}

std::uint16_t readU16LE(std::istream& in) {
    unsigned char b[2] = {0, 0};
    in.read(reinterpret_cast<char*>(b), 2);
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(b[0]) |
                                      (static_cast<std::uint16_t>(b[1]) << 8));
}

/// 读 4 字节 ASCII 标识（"RIFF" / "fmt " / "data"）。
std::string readFourCC(std::istream& in) {
    char b[4] = {0, 0, 0, 0};
    in.read(b, 4);
    return std::string(b, static_cast<std::size_t>(in.gcount()));
}

/// 从已打开的流里解析 WAV（两个入口共用，避免重复实现）。
/// @param out 调用方持有的结果对象；解析失败时 out.ok=false 且 out.error 有原因
WavData parseWav(std::istream& in, WavData& out) {
    if (readFourCC(in) != "RIFF") {
        out.error = "不是 RIFF 文件（可能不是 WAV）";
        return out;
    }
    (void)readU32LE(in);   // RIFF 块长度：本实现按实际文件长度读取，故不使用该值
    if (readFourCC(in) != "WAVE") {
        out.error = "不是 WAVE 格式";
        return out;
    }

    bool haveFmt = false;
    std::uint16_t audioFormat = 0;
    std::uint16_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t bits = 0;
    std::streampos dataOffset = 0;
    std::uint32_t dataSize = 0;
    bool haveData = false;

    while (in.good()) {
        const std::string id = readFourCC(in);
        if (id.size() < 4) {
            break;   // 文件尾或块头不完整
        }
        const std::uint32_t size = readU32LE(in);
        const std::streampos body = in.tellg();
        if (body < 0) {
            break;
        }

        if (id == "fmt ") {
            audioFormat = readU16LE(in);
            channels = readU16LE(in);
            sampleRate = readU32LE(in);
            (void)readU32LE(in);   // 字节率：由采样率与格式推出，不信任文件里的值
            (void)readU16LE(in);   // 块对齐
            bits = readU16LE(in);
            haveFmt = true;
        } else if (id == "data") {
            dataOffset = body;
            dataSize = size;
            haveData = true;
            // data 块之后的内容与本读取无关，不再扫描
            break;
        }

        // 跳到下一块；块长度为奇数时有一个填充字节
        in.seekg(body + static_cast<std::streamoff>(size) + static_cast<std::streamoff>(size % 2));
    }

    if (!haveFmt) {
        out.error = "缺少 fmt 块";
        return out;
    }
    if (!haveData) {
        out.error = "缺少 data 块";
        return out;
    }
    if (audioFormat != 1 && audioFormat != 0xFFFE) {
        out.error = "非 PCM 编码（format=" + std::to_string(audioFormat) + "），只读未压缩 WAV";
        return out;
    }
    if (channels == 0 || bits == 0) {
        out.error = "fmt 块字段不合法（声道数或位深为 0）";
        return out;
    }

    // 数据实际可用长度不得超过文件尾（有些文件的 data 长度字段偏大）
    in.clear();
    in.seekg(0, std::ios::end);
    const std::streampos fileEnd = in.tellg();
    const std::streamoff available = fileEnd - dataOffset;
    if (available <= 0) {
        out.error = "data 块为空";
        return out;
    }
    if (static_cast<std::streamoff>(dataSize) > available) {
        dataSize = static_cast<std::uint32_t>(available);
    }

    const std::size_t bytesPerSample = static_cast<std::size_t>(bits) / 8u;
    if (bytesPerSample == 0 || (bits != 8 && bits != 16 && bits != 32)) {
        out.error = "不支持的位深：" + std::to_string(bits) + " bit";
        return out;
    }

    const std::size_t frameCount = static_cast<std::size_t>(dataSize) / (bytesPerSample * channels);
    const std::size_t usable = frameCount * channels * bytesPerSample;

    std::vector<unsigned char> raw(usable);
    in.seekg(dataOffset);
    in.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(usable));
    const std::size_t got = static_cast<std::size_t>(in.gcount());
    const std::size_t gotFrames = got / (bytesPerSample * channels);

    out.samples.resize(gotFrames);
    for (std::size_t i = 0; i < gotFrames; ++i) {
        double sum = 0.0;
        for (std::size_t c = 0; c < channels; ++c) {
            const std::size_t off = (i * channels + c) * bytesPerSample;
            double v = 0.0;
            if (bits == 8) {
                v = (static_cast<double>(raw[off]) - 128.0) / 128.0;
            } else if (bits == 16) {
                const std::int16_t s = static_cast<std::int16_t>(
                    static_cast<std::uint16_t>(raw[off]) |
                    (static_cast<std::uint16_t>(raw[off + 1]) << 8));
                v = static_cast<double>(s) / 32768.0;
            } else {
                const std::uint32_t u = static_cast<std::uint32_t>(raw[off]) |
                                        (static_cast<std::uint32_t>(raw[off + 1]) << 8) |
                                        (static_cast<std::uint32_t>(raw[off + 2]) << 16) |
                                        (static_cast<std::uint32_t>(raw[off + 3]) << 24);
                v = static_cast<double>(static_cast<std::int32_t>(u)) / 2147483648.0;
            }
            sum += v;
        }
        out.samples[i] = static_cast<float>(sum / static_cast<double>(channels));
    }

    out.sampleRate = static_cast<double>(sampleRate);
    out.channels = static_cast<int>(channels);
    out.bits = static_cast<int>(bits);
    out.ok = true;
    return out;
}

} // namespace

#if defined(_WIN32)
namespace {
/// UTF-8 → UTF-16（Windows 专用）。转换失败返回空串，由调用方按"打开失败"处理。
std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) {
        return std::wstring();
    }
    const int need =
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    if (need <= 0) {
        return std::wstring();
    }
    std::wstring wide(static_cast<std::size_t>(need), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), wide.data(), need);
    return wide;
}
} // namespace
#endif

WavData readWavMono(const std::string& path) {
#if defined(_WIN32)
    // 入参是 UTF-8：必须先转 UTF-16 再走宽字符 API，否则中文路径按 ANSI 解释会打不开（坑 A29）。
    return readWavMonoW(utf8ToWide(path));
#else
    WavData out;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        out.error = "无法打开文件：" + path;
        return out;
    }
    return parseWav(in, out);
#endif
}

#if defined(_WIN32)
WavData readWavMonoW(const std::wstring& path) {
    // 实现方式说明（踩过两次，别再换回去）：
    //   曾经试过"把 FILE* 包成自定义 streambuf"给 istream 用，但 WAV 解析需要
    //   tellg/seekg 与 istream::read() 的 gcount 语义，自定义 streambuf 上极易出偏差，
    //   现象是"缺少 fmt 块"（与真正的格式错误无法区分）。
    //   现在改为：**先把整个文件读进内存，再用同一个解析器**——
    //   代码路径与窄字符版完全一致，且不可能有流语义问题。
    //   代价是峰值多一份文件大小的内存（素材单个约 1.5 MB，可接受）。
    WavData out;

    // Windows 原生 API：以宽字符路径打开，彻底避开 ANSI 代码页问题
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        out.error = "无法打开文件（宽字符路径失败，错误码 " +
                    std::to_string(static_cast<unsigned long>(GetLastError())) + "）";
        return out;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(handle, &size) || size.QuadPart <= 0) {
        CloseHandle(handle);
        out.error = "文件为空或无法获取大小";
        return out;
    }
    std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const BOOL ok = ReadFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
    CloseHandle(handle);
    if (!ok) {
        out.error = "读取文件内容失败";
        return out;
    }
    bytes.resize(read);

    std::istringstream in(bytes, std::ios::binary);
    return parseWav(in, out);
}
#endif

} // namespace pitch

#include "audio-file-decoder.h"

#include "wav-reader.h"   // 第一级：PCM WAV 直读（零 Qt 依赖）

#include <QEventLoop>
#include <QFileInfo>
#include <QUrl>

#include <algorithm>

#ifndef PITCH_HAVE_QT_MULTIMEDIA
#define PITCH_HAVE_QT_MULTIMEDIA 0
#endif

#if PITCH_HAVE_QT_MULTIMEDIA
#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioFormat>
#endif

namespace pitch {

QString audioFileFilter() {
    // 只列**常用**格式：wav（无损直读）、mp3/m4a/aac/ogg/flac（主流压缩与无损）
    return QStringLiteral("音频文件 (*.wav *.mp3 *.m4a *.aac *.ogg *.flac);;"
                          "WAV (*.wav);;所有文件 (*)");
}

DecodedAudio decodeAudioFile(const QString& path) {
    DecodedAudio out;
    if (path.isEmpty()) {
        out.error = QStringLiteral("未指定文件");
        return out;
    }

    // ---------------- 第一级：PCM WAV 直读 ----------------
    // 注意读的是**原始文件头**而不是扩展名：有些 mp3 被改名成 .wav，
    // 按扩展名分流会让它们走到"直读失败"再回退，多一次无谓的失败路径。
    const WavData wav = readWavMono(path.toUtf8().toStdString());
    if (wav.ok && !wav.samples.empty() && wav.sampleRate > 0.0) {
        out.samples = wav.samples;
        out.sampleRate = wav.sampleRate;
        out.ok = true;
        out.route = QStringLiteral("PCM WAV 直读（%1 bit / %2 声道）").arg(wav.bits).arg(wav.channels);
        return out;
    }

#if PITCH_HAVE_QT_MULTIMEDIA
    // ---------------- 第二级：交给 Qt Multimedia 的解码器 ----------------
    // 信号必须在 start() **之前**连上，否则开头几个 bufferReady 会丢（与采集侧同一个坑）。
    QAudioDecoder decoder;
    QEventLoop loop;
    std::vector<float> samples;
    double rate = 0.0;
    bool sawAnyBuffer = false;
    bool formatUnsupported = false;

    QObject::connect(&decoder, &QAudioDecoder::bufferReady, [&decoder, &samples, &rate, &sawAnyBuffer,
                                                             &formatUnsupported]() {
        const QAudioBuffer buffer = decoder.read();
        if (!buffer.isValid()) {
            return;
        }
        sawAnyBuffer = true;
        const QAudioFormat format = buffer.format();
        rate = static_cast<double>(format.sampleRate());
        const int ch = std::max(1, format.channelCount());
        const int frames = static_cast<int>(buffer.frameCount());
        if (frames <= 0) {
            return;
        }
        samples.reserve(samples.size() + static_cast<std::size_t>(frames));

        // QAudioBuffer 只提供模板化的 constData<T>()：对两种会遇到的格式分别取值，
        // 其余格式（Int32/UInt8…）视为不可用并标记，最后给出明确原因（不静默产出空音频）。
        const bool isFloat = (format.sampleFormat() == QAudioFormat::Float);
        const bool isInt16 = (format.sampleFormat() == QAudioFormat::Int16);
        const float* asFloat = isFloat ? buffer.constData<float>() : nullptr;
        const qint16* asInt16 = isInt16 ? buffer.constData<qint16>() : nullptr;
        if (asFloat == nullptr && asInt16 == nullptr) {
            formatUnsupported = true;
            return;
        }
        for (int f = 0; f < frames; ++f) {
            double sum = 0.0;
            for (int c = 0; c < ch; ++c) {
                const std::size_t index = static_cast<std::size_t>(f) * static_cast<std::size_t>(ch) +
                                          static_cast<std::size_t>(c);
                sum += isFloat ? static_cast<double>(asFloat[index])
                               : static_cast<double>(asInt16[index]) / 32768.0;
            }
            samples.push_back(static_cast<float>(sum / static_cast<double>(ch)));
        }
    });
    QObject::connect(&decoder, &QAudioDecoder::finished, &loop, &QEventLoop::quit);
    // QAudioDecoder 同时有取值函数 error() 与信号 error(Error)，故显式指定信号签名（坑 A51）
    QObject::connect(&decoder,
                     static_cast<void (QAudioDecoder::*)(QAudioDecoder::Error)>(&QAudioDecoder::error),
                     &loop, &QEventLoop::quit);

    decoder.setSource(QUrl::fromLocalFile(path));
    decoder.start();
    loop.exec();   // 解码是异步的；一个几 MB 的音频通常不到 1 秒
    const QAudioDecoder::Error decodeError = decoder.error();
    const QString decoderErrorText = decoder.errorString();
    decoder.stop();

    if (decodeError == QAudioDecoder::NoError && !samples.empty() && rate > 0.0) {
        out.samples = samples;
        out.sampleRate = rate;
        out.ok = true;
        out.route = QStringLiteral("解码器（Qt Multimedia / FFmpeg）");
        return out;
    }

    // ---------------- 失败：把原因说清楚（三种情况要能分开）----------------
    if (formatUnsupported) {
        out.error = QStringLiteral("该文件解码后的样点格式暂不支持（仅支持 Float32 / Int16）");
    } else if (!sawAnyBuffer && decodeError == QAudioDecoder::NoError) {
        out.error = QStringLiteral("解码器没有产出任何数据：可能不是音频文件，或格式不被支持");
    } else {
        out.error = QStringLiteral("解码失败：%1")
                        .arg(decoderErrorText.isEmpty() ? QStringLiteral("原因未知") : decoderErrorText);
    }
    if (!wav.error.empty()) {
        out.error += QStringLiteral("（直读 WAV 也失败：%1）").arg(QString::fromStdString(wav.error));
    }
    return out;
#else
    Q_UNUSED(path)
    out.error = QStringLiteral("本二进制未包含 Qt Multimedia，只能分析未压缩 PCM WAV；%1")
                    .arg(wav.error.empty() ? QStringLiteral("请改用 WAV 素材")
                                           : QString::fromStdString(wav.error));
    return out;
#endif
}

} // namespace pitch

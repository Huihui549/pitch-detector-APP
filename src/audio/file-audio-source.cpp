// 把 WAV 当作实时流播放的采集实现 —— 见头文件说明

#include "file-audio-source.h"

#include "wav-reader.h"

#include <QTimer>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace pitch {

FileAudioSource::FileAudioSource(QObject* parent) : IAudioSource(parent) {
    // 定时器精度：Qt 默认的 CoarseTimer 抖动可能到 5%，会让帧进不均匀。
    // 这里用 PreciseTimer，代价可忽略，但回放节奏更接近真实设备。
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &FileAudioSource::onTimerTick);
}

FileAudioSource::~FileAudioSource() = default;

bool FileAudioSource::load(const QString& path) {
    stop();
    // 走宽字符入口：素材路径常含中文，窄字符版在 Windows 上按本地代码页解释会打不开（坑 A29）
    const WavData wav = readWavMonoW(path.toStdWString());
    if (!wav.ok) {
        emit errorOccurred(AudioErrorKind::Unknown,
                           QStringLiteral("无法读取音频文件：%1").arg(QString::fromStdString(wav.error)));
        return false;
    }
    if (wav.samples.empty()) {
        emit errorOccurred(AudioErrorKind::Unknown, QStringLiteral("音频文件不含任何样点"));
        return false;
    }
    m_samples = wav.samples;
    m_sampleRate = static_cast<int>(std::lround(wav.sampleRate));
    m_path = path;
    m_cursor = 0;
    m_loaded = true;
    return true;
}

void FileAudioSource::start(int sampleRate, int channels) {
    Q_UNUSED(channels);
    if (!m_loaded) {
        emit errorOccurred(AudioErrorKind::NoDevice, QStringLiteral("尚未载入音频文件"));
        emit stateChanged(AudioState::Error);
        return;
    }
    if (sampleRate > 0 && sampleRate != m_sampleRate) {
        // 与真实设备不同：本实现不做重采样（那会引入额外变量，破坏"输入已知"这个前提）。
        // 明确报告不一致，由上层决定是否接受。
        emit formatMismatch(sampleRate, m_sampleRate, channels, 1);
    }
    if (m_timer.isActive()) {
        m_timer.stop();
    }
    m_cursor = 0;
    emit stateChanged(AudioState::Starting);

    // 每个 tick 喂 kChunkSamples 个样点，间隔按真实速率换算：
    // 间隔(ms) = 样点数 / 采样率 × 1000 / 倍率
    const double intervalMs =
        static_cast<double>(kChunkSamples) / static_cast<double>(m_sampleRate) * 1000.0 / m_speedFactor;
    m_timer.start(std::max(1, static_cast<int>(std::lround(intervalMs))));

    emit stateChanged(AudioState::Running);
}

void FileAudioSource::stop() {
    if (m_timer.isActive()) {
        m_timer.stop();
    }
    emit stateChanged(AudioState::Stopped);
}

QString FileAudioSource::description() const {
    if (!m_loaded) {
        return QStringLiteral("文件回放（未载入）");
    }
    return QStringLiteral("文件回放：%1（%2 Hz，%3 样点，%4 倍速）")
        .arg(m_path)
        .arg(m_sampleRate)
        .arg(static_cast<qulonglong>(m_samples.size()))
        .arg(m_speedFactor);
}

void FileAudioSource::onTimerTick() {
    if (m_cursor >= m_samples.size()) {
        stop();
        return;
    }
    const std::size_t remaining = m_samples.size() - m_cursor;
    const std::size_t count = std::min<std::size_t>(static_cast<std::size_t>(kChunkSamples), remaining);

    QVector<float> chunk(static_cast<int>(count));
    for (std::size_t i = 0; i < count; ++i) {
        chunk[static_cast<int>(i)] = m_samples[m_cursor + i];
    }
    m_cursor += count;

    emit samplesReady(chunk, m_sampleRate);

    if (m_cursor >= m_samples.size()) {
        stop();
    }
}

} // namespace pitch

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
    // 路径统一按 UTF-8 传入：Windows 上由 readWavMono 内部转 UTF-16，中文路径才不会打不开
    // （坑 A29）；Android/Linux 的文件名本就是 UTF-8 字节，同一条写法也成立。
    const WavData wav = readWavMono(path.toUtf8().toStdString());
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
    // 文件回放不存在"设备格式协商"：采样率由**文件**决定，调用方传进来的只是默认假设，
    // 因此这里**不发 formatMismatch**——发了只会让界面挂一条"格式与请求不同"的噪音提示
    // （真实差异由 description() 显示实际采样率即可）。
    Q_UNUSED(sampleRate);
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

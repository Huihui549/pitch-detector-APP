// 真实麦克风采集（Qt Multimedia）—— 实现
//
// 本文件只在 Qt Multimedia 可用时参与编译（见头文件说明与 src/audio/CMakeLists.txt）。

#include "qt-audio-source.h"

#include <QMediaDevices>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace pitch {

QtAudioSource::QtAudioSource(QObject* parent) : IAudioSource(parent) {
    m_pollTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_pollTimer, &QTimer::timeout, this, &QtAudioSource::pollDevice);
}

QtAudioSource::~QtAudioSource() {
    stop();
}

bool QtAudioSource::isAvailable() const {
    return !QMediaDevices::audioInputs().isEmpty();
}

QString QtAudioSource::description() const {
    const QList<QAudioDevice> devices = QMediaDevices::audioInputs();
    if (devices.isEmpty()) {
        return QStringLiteral("麦克风（未找到输入设备）");
    }
    const QAudioDevice& d = devices.first();
    return QStringLiteral("麦克风：%1（%2 Hz / %3 声道）")
        .arg(d.description())
        .arg(m_actualFormat.sampleRate() > 0 ? m_actualFormat.sampleRate() : d.preferredFormat().sampleRate())
        .arg(m_actualFormat.channelCount() > 0 ? m_actualFormat.channelCount() : d.preferredFormat().channelCount());
}

QString QtAudioSource::statsDescription() const {
    return QStringLiteral("轮询 %1 次　·　%2 字节　·　样点 %3（非零 %4%）　·　块 RMS 峰值 %5　·　幅度峰值 %6%7")
        .arg(m_pollCount)
        .arg(m_bytesReceived)
        .arg(m_samplesSeen)
        .arg(m_samplesSeen > 0 ? 100.0 * static_cast<double>(m_samplesNonZero) / static_cast<double>(m_samplesSeen)
                               : 0.0,
             0, 'f', 2)
        .arg(m_peakChunkRms, 0, 'g', 4)
        .arg(m_peakAbs, 0, 'g', 4)
        .arg(m_lastError.isEmpty() ? QString() : QStringLiteral("　·　错误：") + m_lastError);
}

double QtAudioSource::nonZeroRatio() const {
    if (m_samplesSeen <= 0) {
        return 0.0;
    }
    return static_cast<double>(m_samplesNonZero) / static_cast<double>(m_samplesSeen);
}

void QtAudioSource::start(int sampleRate, int channels) {
    stop();

    const QList<QAudioDevice> devices = QMediaDevices::audioInputs();
    if (devices.isEmpty()) {
        emit errorOccurred(AudioErrorKind::NoDevice,
                           QStringLiteral("未找到音频输入设备"));
        emit stateChanged(AudioState::Error);
        return;
    }
    const QAudioDevice device = devices.first();

    // 期望格式：**单声道 + 32 位浮点**，采样率优先取设备首选值。
    //
    // 为什么优先设备采样率而不是硬用 44.1 kHz：本机默认设备只支持 48 kHz，
    // 硬要 44.1 kHz 会被拒（用户实测见到"请求 44100/1，实际 48000/2"的提示），
    // 而采样率本身对算法没影响——τ 换算一律按**实际**采样率走。
    // 真正影响体验的是**声道数**：拿到 2 声道会让每帧样点数翻倍，等效降低出帧率。
    m_requestedFormat.setSampleRate(sampleRate > 0 ? sampleRate
                                                   : device.preferredFormat().sampleRate());
    m_requestedFormat.setChannelCount(channels > 0 ? channels : 1);
    m_requestedFormat.setSampleFormat(QAudioFormat::Float);

    if (!device.isFormatSupported(m_requestedFormat)) {
        // 退让顺序：① 设备首选采样率 + 单声道 + Float ② 设备首选格式整体
        // 目标始终是**尽量拿到单声道 Float**；实在不行才接受整份首选格式并显式提示。
        QAudioFormat candidate = device.preferredFormat();
        candidate.setChannelCount(channels > 0 ? channels : 1);
        candidate.setSampleFormat(QAudioFormat::Float);
        if (device.isFormatSupported(candidate)) {
            m_actualFormat = candidate;
        } else {
            m_actualFormat = device.preferredFormat();
        }
        emit formatMismatch(m_requestedFormat.sampleRate(), m_actualFormat.sampleRate(),
                            m_requestedFormat.channelCount(), m_actualFormat.channelCount());
        m_formatAdjusted = true;
    } else {
        m_actualFormat = m_requestedFormat;
        m_formatAdjusted = false;
    }

    if (m_actualFormat.channelCount() <= 0 || m_actualFormat.sampleRate() <= 0) {
        emit errorOccurred(AudioErrorKind::FormatUnsupported,
                           QStringLiteral("设备返回的音频格式不合法"));
        emit stateChanged(AudioState::Error);
        return;
    }

    emit stateChanged(AudioState::Starting);
    m_source = new QAudioSource(device, m_actualFormat, this);

    // **显式设小设备缓冲**：不设时 Qt 用设备默认缓冲（可能几百毫秒），
    // 数据要攒满才交付，直接表现为"出数很慢、不灵敏"（用户实测的正是这个问题）。
    // 取"约 1024 个声道帧"：够小以保证刷新频率，又不至于每次只给几个样点。
    const int bytesPerFrame = std::max(1, static_cast<int>(m_actualFormat.bytesPerFrame()));
    m_source->setBufferSize(bytesPerFrame * 1024);

    // 顺序很重要：先连 error 与 start，再拿 QIODevice。
    // 之前把 connect(readyRead) 放在 start() 之后，启动瞬间的信号全丢了。
    connect(m_source, &QAudioSource::stateChanged, this, [this](QAudio::State s) {
        if (s == QAudio::StoppedState && m_source != nullptr && m_source->error() != QAudio::NoError) {
            m_lastError = QStringLiteral("QAudioSource 状态=Stopped，错误码=%1")
                              .arg(static_cast<int>(m_source->error()));
            emit errorOccurred(AudioErrorKind::Unknown, m_lastError);
            emit stateChanged(AudioState::Error);
        }
    });

    m_device = m_source->start();
    if (m_device == nullptr) {
        // 打开失败：把 Qt 的错误码一并报出来，否则只剩"可能是权限"这种无法验证的猜测
        m_lastError = QStringLiteral("QAudioSource::start() 返回空，错误码=%1")
                          .arg(static_cast<int>(m_source->error()));
        emit errorOccurred(AudioErrorKind::PermissionDenied,
                           QStringLiteral("无法打开音频输入：%1").arg(m_lastError));
        emit stateChanged(AudioState::Error);
        delete m_source;
        m_source = nullptr;
        return;
    }

    // 用定时轮询把数据**拉**出来（QAudioSource 的 QIODevice 默认是拉取模型）。
    // 间隔取"约 256 样点"：比设备缓冲更密，保证设备一有数据就取走，
    // 不至于在设备缓冲里积压成"一批一批地到"（那会明显降低出帧频率）。
    m_pollCount = 0;
    m_bytesReceived = 0;
    m_lastBytes = 0;
    m_samplesSeen = 0;
    m_samplesNonZero = 0;
    m_peakChunkRms = 0.0;
    m_peakAbs = 0.0;
    const int intervalMs =
        std::max(3, static_cast<int>(std::lround(256.0 / std::max(1, m_actualFormat.sampleRate()) * 1000.0)));
    m_pollTimer.start(intervalMs);

    emit stateChanged(AudioState::Running);
}

void QtAudioSource::stop() {
    m_pollTimer.stop();
    if (m_source != nullptr) {
        m_source->stop();
        delete m_source;
        m_source = nullptr;
        m_device = nullptr;
        emit stateChanged(AudioState::Stopped);
    }
}

void QtAudioSource::pollDevice() {
    if (m_device == nullptr) {
        return;
    }
    ++m_pollCount;
    const QByteArray chunk = m_device->readAll();
    m_lastBytes = chunk.size();
    if (chunk.isEmpty()) {
        return;   // 正常：这一轮设备还没给数据
    }
    m_bytesReceived += chunk.size();
    const QVector<float> mono = toMonoFloat(chunk.constData(), chunk.size());
    if (!mono.isEmpty()) {
        // 统计：非零样点比例 + 单块 RMS 峰值 + 幅度范围。
        // 这四项合起来能判定"通道是否真的在送有效音频"：
        //   · 非零比例低 → 通道给的是静音填充
        //   · 非零比例高但 RMS 极小 → 通道通了但电平极低（麦克风静音/增益为 0）
        double sumSquares = 0.0;
        double peakAbs = 0.0;
        for (const float v : mono) {
            ++m_samplesSeen;
            if (v != 0.0f) {
                ++m_samplesNonZero;
            }
            const double d = static_cast<double>(v);
            sumSquares += d * d;
            peakAbs = std::max(peakAbs, std::abs(d));
        }
        const double chunkRms = std::sqrt(sumSquares / static_cast<double>(mono.size()));
        m_peakChunkRms = std::max(m_peakChunkRms, chunkRms);
        m_peakAbs = std::max(m_peakAbs, peakAbs);

        emit samplesReady(mono, m_actualFormat.sampleRate());
    }
}

QVector<float> QtAudioSource::toMonoFloat(const char* data, qsizetype bytes) const {
    const int channels = std::max(1, m_actualFormat.channelCount());
    const QAudioFormat::SampleFormat fmt = m_actualFormat.sampleFormat();

    int bytesPerSample = 0;
    switch (fmt) {
    // Qt 6 的 QAudioFormat 只有 UInt8 这唯一的 8 位格式（没有 Int8）。
    // Qt 5 曾有 Int8，照 Qt5 记忆写会编译失败——实测踩过。
    case QAudioFormat::UInt8:
        bytesPerSample = 1;
        break;
    case QAudioFormat::Int16:
        bytesPerSample = 2;
        break;
    case QAudioFormat::Int32:
    case QAudioFormat::Float:
        bytesPerSample = 4;
        break;
    case QAudioFormat::Unknown:
    default:
        return {};
    }

    const qsizetype frameBytes = static_cast<qsizetype>(bytesPerSample) * channels;
    const qsizetype frames = frameBytes > 0 ? bytes / frameBytes : 0;
    if (frames <= 0) {
        return {};
    }

    QVector<float> mono(static_cast<int>(frames));
    for (qsizetype i = 0; i < frames; ++i) {
        double sum = 0.0;
        for (int c = 0; c < channels; ++c) {
            const char* p = data + (i * channels + c) * bytesPerSample;
            double v = 0.0;
            switch (fmt) {
            case QAudioFormat::UInt8: {
                quint8 raw = 0;
                std::memcpy(&raw, p, 1);
                v = (static_cast<double>(raw) - 128.0) / 128.0;
                break;
            }
            case QAudioFormat::Int16: {
                qint16 raw = 0;
                std::memcpy(&raw, p, 2);
                v = static_cast<double>(raw) / 32768.0;
                break;
            }
            case QAudioFormat::Int32: {
                qint32 raw = 0;
                std::memcpy(&raw, p, 4);
                v = static_cast<double>(raw) / 2147483648.0;
                break;
            }
            case QAudioFormat::Float: {
                float raw = 0.0f;
                std::memcpy(&raw, p, 4);
                v = static_cast<double>(raw);
                break;
            }
            default:
                break;
            }
            sum += v;
        }
        mono[static_cast<int>(i)] = static_cast<float>(sum / channels);
    }
    return mono;
}

} // namespace pitch

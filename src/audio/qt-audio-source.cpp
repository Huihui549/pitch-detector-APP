// 真实麦克风采集（Qt Multimedia）—— 实现
//
// 本文件只在 Qt Multimedia 可用时参与编译（见头文件说明与 src/audio/CMakeLists.txt）。

#include "qt-audio-source.h"

#include <QCoreApplication>
#include <QMediaDevices>
#include <QPermissions>
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

#if defined(Q_OS_ANDROID)
    // 手机端 RECORD_AUDIO 是**运行期权限**（清单里声明只是第一步）。
    // 没授权时 QAudioSource::start() 直接返回空，而 error() 仍是 NoError(0)——
    // 错误信息里没有任何指向权限的线索（实测踩过，坑 A41）。故这里先查后申请，
    // 授权成功后按原参数自动重试一次启动。
    QCoreApplication* app = QCoreApplication::instance();
    QMicrophonePermission micPermission;
    if (app != nullptr && app->checkPermission(micPermission) != Qt::PermissionStatus::Granted) {
        emit stateChanged(AudioState::Starting);
        app->requestPermission(micPermission, this,
                               [this, sampleRate, channels](const QPermission& result) {
                                   if (result.status() == Qt::PermissionStatus::Granted) {
                                       start(sampleRate, channels);
                                   } else {
                                       emit errorOccurred(
                                           AudioErrorKind::PermissionDenied,
                                           QStringLiteral("麦克风权限被拒：请在系统设置里允许本应用录音后重试"));
                                       emit stateChanged(AudioState::Error);
                                   }
                               });
        return;
    }
#endif

    const QList<QAudioDevice> devices = QMediaDevices::audioInputs();
    if (devices.isEmpty()) {
        emit errorOccurred(AudioErrorKind::NoDevice,
                           QStringLiteral("未找到音频输入设备"));
        emit stateChanged(AudioState::Error);
        return;
    }
    const QAudioDevice device = devices.first();
    const QAudioFormat preferred = device.preferredFormat();

    // 采样率：**主动与设备对齐**，而不是事后再提示"格式与请求不同"。
    // 理由：算法与采样率无关（τ 换算、帧长全部按实际采样率算，见 IAudioSource::actualSampleRate
    // 的约定）；而设备不支持请求采样率时，后端只会给设备首选值——那会让界面永远挂着一条
    // "设备实际格式与请求不同"的提示，用户看到的正是"为什么不能一致"。
    int wantRate = sampleRate > 0 ? sampleRate : preferred.sampleRate();
    if (preferred.sampleRate() > 0 && wantRate != preferred.sampleRate()) {
        QAudioFormat probe = preferred;
        probe.setSampleRate(wantRate);
        probe.setChannelCount(channels > 0 ? channels : 1);
        probe.setSampleFormat(QAudioFormat::Float);
        if (!device.isFormatSupported(probe)) {
            wantRate = preferred.sampleRate();   // 不支持就别请求，直接用设备首选值
        }
    }

    // 请求格式：**单声道 + 32 位浮点**。真正影响体验的是声道数（2 声道会让每帧样点数翻倍）。
    m_requestedFormat = preferred;
    m_requestedFormat.setSampleRate(wantRate);
    m_requestedFormat.setChannelCount(channels > 0 ? channels : 1);
    m_requestedFormat.setSampleFormat(QAudioFormat::Float);

    emit stateChanged(AudioState::Starting);

    // 先**真的去开**请求格式：isFormatSupported() 只是能力查询，在 Android 上偏保守
    // （曾出现"它说单声道不支持、实际能开"的情况）。实际格式一律以 QAudioSource::format() 为准。
    bool opened = openDevice(device, m_requestedFormat);
    if (!opened) {
        // 退让顺序：① 设备首选采样率 + 请求声道数 + Float ② 设备首选格式整体
        QAudioFormat candidate = preferred;
        candidate.setChannelCount(m_requestedFormat.channelCount());
        candidate.setSampleFormat(QAudioFormat::Float);
        const QAudioFormat fallback = device.isFormatSupported(candidate) ? candidate : preferred;
        opened = openDevice(device, fallback);
    }
    if (!opened) {
        emit errorOccurred(AudioErrorKind::PermissionDenied,
                           QStringLiteral("无法打开音频输入：%1").arg(m_lastError));
        emit stateChanged(AudioState::Error);
        return;
    }

    // 只报**真实**的协商差异：能对齐的在上面对齐了，不该再打扰用户
    if (m_actualFormat.sampleRate() != m_requestedFormat.sampleRate()
        || m_actualFormat.channelCount() != m_requestedFormat.channelCount()) {
        m_formatAdjusted = true;
        emit formatMismatch(m_requestedFormat.sampleRate(), m_actualFormat.sampleRate(),
                            m_requestedFormat.channelCount(), m_actualFormat.channelCount());
    } else {
        m_formatAdjusted = false;
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

namespace {
/// 打开失败且错误码为 0 时的常见原因提示：按平台给出可执行的排查方向，
/// 否则现场只能对着"错误码=0"猜（实测踩过：手机上就是权限没授）。
QString openFailureHint() {
#if defined(Q_OS_ANDROID)
    return QStringLiteral(
        "（错误码 0 却打不开，手机上通常是：录音权限未授予，或麦克风正被其它应用占用）");
#elif defined(Q_OS_WIN)
    return QStringLiteral(
        "（错误码 0 却打不开，Windows 上通常是：系统隐私设置里禁止了应用访问麦克风，或设备被独占）");
#else
    return QStringLiteral("（错误码 0 却打不开，通常是：系统未授权麦克风，或设备被其它进程独占）");
#endif
}
} // namespace

bool QtAudioSource::openDevice(const QAudioDevice& device, const QAudioFormat& format) {
    m_source = new QAudioSource(device, format, this);

    // **实际格式以 Qt 为准**：构造时传的是请求值，后端可能改写（Android 上很常见）。
    // 以前用 isFormatSupported() 预判，判错就会一直提示"格式与请求不同"；这里读的是真值。
    const QAudioFormat actual = m_source->format();
    m_actualFormat =
        (actual.sampleRate() > 0 && actual.channelCount() > 0) ? actual : device.preferredFormat();
    if (m_actualFormat.sampleRate() <= 0 || m_actualFormat.channelCount() <= 0) {
        m_lastError = QStringLiteral("设备返回的音频格式不合法（采样率 %1，声道 %2）")
                          .arg(m_actualFormat.sampleRate())
                          .arg(m_actualFormat.channelCount());
        delete m_source;
        m_source = nullptr;
        return false;
    }

    // **显式设小设备缓冲**：不设时 Qt 用设备默认缓冲（可能几百毫秒），数据要攒满才交付，
    // 表现为"出数很慢、不灵敏"（用户实测的正是这个问题）。取"约 1024 个声道帧"。
    const int bytesPerFrame = std::max(1, static_cast<int>(m_actualFormat.bytesPerFrame()));
    m_source->setBufferSize(static_cast<qsizetype>(bytesPerFrame) * 1024);

    // 顺序很重要：先连 stateChanged 再 start()，否则启动瞬间的信号会丢（实测踩过）
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
        m_lastError = QStringLiteral("QAudioSource::start() 返回空，错误码=%1%2")
                          .arg(static_cast<int>(m_source->error()))
                          .arg(openFailureHint());
        delete m_source;
        m_source = nullptr;
        m_device = nullptr;
        return false;
    }
    return true;
}

} // namespace pitch

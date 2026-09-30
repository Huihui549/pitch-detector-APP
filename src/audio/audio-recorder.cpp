#include "audio-recorder.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>

#ifndef PITCH_HAVE_QT_MULTIMEDIA
#define PITCH_HAVE_QT_MULTIMEDIA 0
#endif

#if PITCH_HAVE_QT_MULTIMEDIA
#include <QAudioDevice>
#include <QAudioInput>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QMediaFormat>
#include <QMediaRecorder>
#endif

namespace pitch {

#if PITCH_HAVE_QT_MULTIMEDIA
namespace {

/// 容器 → 扩展名。**保存与临时文件都按它命名**：扩展名与实际容器不一致会产出"假文件"
/// （名字叫 .wav、内容其实是 mp3），播放器多半会直接报错或时长乱掉。
QString extensionFor(QMediaFormat::FileFormat f) {
    switch (f) {
    case QMediaFormat::MP3:
        return QStringLiteral("mp3");
    case QMediaFormat::Mpeg4Audio:
        return QStringLiteral("m4a");
    case QMediaFormat::Wave:
        return QStringLiteral("wav");
    case QMediaFormat::FLAC:
        return QStringLiteral("flac");
    default:
        return QStringLiteral("dat");
    }
}

} // namespace
#endif

AudioRecorder::AudioRecorder(QObject* parent) : QObject(parent) {
#if PITCH_HAVE_QT_MULTIMEDIA
    m_recorder = new QMediaRecorder(this);
    m_session = new QMediaCaptureSession(this);
    m_input = new QAudioInput(this);

    // 首选 mp3；后端不支持时按"可用列表"退让（MP3 → MPEG-4 Audio(AAC) → Wave）。
    // **必须同时指定容器与音频编码**：只设容器会让编码停在 Unspecified（实测读回就是 Unspecified），
    // 等于把"到底用什么编码"交给后端猜——猜错就会录不出来或产出与预期不符的文件。
    struct Candidate {
        QMediaFormat::FileFormat format;
        QMediaFormat::AudioCodec codec;
    };
    const Candidate wanted[] = {
        {QMediaFormat::MP3, QMediaFormat::AudioCodec::MP3},
        {QMediaFormat::Mpeg4Audio, QMediaFormat::AudioCodec::AAC},
        {QMediaFormat::Wave, QMediaFormat::AudioCodec::Wave},
    };
    bool settled = false;
    const QList<QMediaFormat::FileFormat> formats =
        QMediaFormat().supportedFileFormats(QMediaFormat::Encode);
    for (const Candidate& candidate : wanted) {
        if (!formats.contains(candidate.format)) {
            continue;
        }
        QMediaFormat f(candidate.format);
        if (!f.supportedAudioCodecs(QMediaFormat::Encode).contains(candidate.codec)) {
            continue;
        }
        f.setAudioCodec(candidate.codec);
        m_recorder->setMediaFormat(f);
        settled = true;
        break;
    }
    if (!settled) {
        m_lastError = QStringLiteral("后端没有可用的音频编码容器，无法录音");
    }

    // **读回本录音器实际生效的格式**，并据此决定扩展名。
    // 不能拿"探测用的另一个 recorder"的描述当本实例的结论——两者可能不同（我原先就是这么写的）。
    const QMediaFormat chosen = m_recorder->mediaFormat();
    m_extension = extensionFor(chosen.fileFormat());
    m_description = QStringLiteral("容器 %1 ｜ 音频编码 %2 ｜ 扩展名 .%3")
                        .arg(QMediaFormat::fileFormatName(chosen.fileFormat()))
                        .arg(QMediaFormat::audioCodecName(chosen.audioCodec()))
                        .arg(m_extension);

    m_session->setAudioInput(m_input);
    m_session->setRecorder(m_recorder);

    QObject::connect(m_recorder, &QMediaRecorder::recorderStateChanged, this,
                     [this](QMediaRecorder::RecorderState) { emit stateChanged(); });
    QObject::connect(m_recorder, &QMediaRecorder::errorOccurred, this,
                     [this](QMediaRecorder::Error, const QString& message) {
                         m_lastError = message;
                         if (m_state == State::Recording || m_state == State::Paused) {
                             setState(State::Idle);
                         }
                         emit errorOccurred(message);
                     });
#endif
}

AudioRecorder::~AudioRecorder() {
    stopAndDiscard();
}

bool AudioRecorder::available() const {
#if PITCH_HAVE_QT_MULTIMEDIA
    return m_recorder != nullptr && !QMediaDevices::defaultAudioInput().isNull();
#else
    return false;
#endif
}

QString AudioRecorder::unavailableReason() const {
#if PITCH_HAVE_QT_MULTIMEDIA
    if (m_recorder == nullptr) {
        return QStringLiteral("录音器初始化失败");
    }
    if (QMediaDevices::defaultAudioInput().isNull()) {
        return QStringLiteral("本机没有可用的音频输入设备（没有麦克风），无法录音");
    }
    return {};
#else
    return QStringLiteral("本二进制在构建时未包含 Qt Multimedia，无法录音");
#endif
}

QString AudioRecorder::negotiatedFormatDescription() {
#if PITCH_HAVE_QT_MULTIMEDIA
    QMediaRecorder probe;
    QMediaFormat format;
    const QList<QMediaFormat::FileFormat> formats =
        format.supportedFileFormats(QMediaFormat::Encode);
    const QMediaFormat::FileFormat wanted[] = {QMediaFormat::MP3, QMediaFormat::Mpeg4Audio,
                                               QMediaFormat::Wave};
    for (const QMediaFormat::FileFormat candidate : wanted) {
        if (!formats.contains(candidate)) {
            continue;
        }
        QMediaFormat f(candidate);
        const QList<QMediaFormat::AudioCodec> codecs = f.supportedAudioCodecs(QMediaFormat::Encode);
        if (codecs.isEmpty()) {
            continue;
        }
        probe.setMediaFormat(f);
        const QMediaFormat chosen = probe.mediaFormat();
        return QStringLiteral("容器 %1 ｜ 音频编码 %2")
            .arg(QMediaFormat::fileFormatName(chosen.fileFormat()))
            .arg(QMediaFormat::audioCodecName(chosen.audioCodec()));
    }
    return QStringLiteral("（后端没有可用的录制容器）");
#else
    return QStringLiteral("（未包含 Qt Multimedia）");
#endif
}

double AudioRecorder::elapsedSec() const {
    if (m_state == State::Recording) {
        qint64 paused = m_pausedTotalMs;
        return static_cast<double>(m_clock.elapsed() - paused) / 1000.0;
    }
    if (m_state == State::Paused) {
        const qint64 paused = m_pausedTotalMs + (m_clock.elapsed() - m_pauseStartedMs);
        return static_cast<double>(m_clock.elapsed() - paused) / 1000.0;
    }
    return static_cast<double>(m_pausedTotalMs > 0 ? m_pausedTotalMs : 0) / 1000.0;
}

void AudioRecorder::setState(State state) {
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged();
}

bool AudioRecorder::start() {
    m_lastError.clear();
#if PITCH_HAVE_QT_MULTIMEDIA
    if (m_recorder == nullptr) {
        m_lastError = QStringLiteral("录音器初始化失败");
        return false;
    }
    if (QMediaDevices::defaultAudioInput().isNull()) {
        m_lastError = unavailableReason();
        return false;
    }

    // 每次录制都用新的临时文件：这样"停止=清空"只需删文件，不必担心残留。
    // **扩展名必须与协商到的容器一致**：Qt 的 FFmpeg 后端会参考它选封装器，
    // 用 ".tmp" 这种扩展名有记录失败的风险（原来就是 .tmp，等于把风险留给运行期）。
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
                        QStringLiteral("/pitch-detector");
    QDir().mkpath(dir);
    m_tempPath = dir + QStringLiteral("/take-") +
                 QString::number(QDateTime::currentMSecsSinceEpoch()) + QLatin1Char('.') + m_extension;

    m_recorder->setOutputLocation(QUrl::fromLocalFile(m_tempPath));
    m_recorder->record();
    if (m_recorder->recorderState() != QMediaRecorder::RecordingState) {
        m_lastError = m_recorder->errorString().isEmpty()
                          ? QStringLiteral("录音未能开始（设备或权限被拒绝）")
                          : m_recorder->errorString();
        return false;
    }
    m_clock.start();
    m_pausedTotalMs = 0;
    m_pauseStartedMs = 0;
    m_hasTake = true;
    setState(State::Recording);
    return true;
#else
    m_lastError = unavailableReason();
    return false;
#endif
}

void AudioRecorder::pause() {
#if PITCH_HAVE_QT_MULTIMEDIA
    if (m_state != State::Recording || m_recorder == nullptr) {
        return;
    }
    m_recorder->pause();
    m_pauseStartedMs = m_clock.elapsed();
    setState(State::Paused);
#endif
}

void AudioRecorder::resume() {
#if PITCH_HAVE_QT_MULTIMEDIA
    if (m_state != State::Paused || m_recorder == nullptr) {
        return;
    }
    m_pausedTotalMs += m_clock.elapsed() - m_pauseStartedMs;
    m_recorder->record();
    setState(State::Recording);
#endif
}

void AudioRecorder::stopAndDiscard() {
#if PITCH_HAVE_QT_MULTIMEDIA
    if (m_recorder != nullptr && m_state != State::Idle) {
        m_recorder->stop();
    }
#endif
    m_lastError.clear();
    // 用户定义：停止=清除已录内容，从头开始
    if (!m_tempPath.isEmpty()) {
        QFile::remove(m_tempPath);
        m_tempPath.clear();
    }
    m_hasTake = false;
    m_pausedTotalMs = 0;
    m_pauseStartedMs = 0;
    setState(State::Idle);
}

void AudioRecorder::finish() {
#if PITCH_HAVE_QT_MULTIMEDIA
    if (m_recorder != nullptr && m_state != State::Idle) {
        m_recorder->stop();   // 停止会把容器收尾（mp3 的尾部信息在此时写入），文件才完整
    }
#endif
    m_lastError.clear();
    // 与 stopAndDiscard 的唯一区别：**保留临时文件**（hasTake 仍为 true）
    setState(State::Idle);
}

bool AudioRecorder::saveAs(const QString& path, QString* errorOut) {
    // 录音中直接点保存：**先把文件收尾再复制**——否则复制到的是写了一半、缺尾部的文件
    // （表现是"保存出来的 mp3 播不了或时长不对"）。收尾后保留本次录音，取消保存也不会丢数据。
    if (m_state == State::Recording || m_state == State::Paused) {
        finish();
    }
    if (!m_hasTake || m_tempPath.isEmpty() || !QFileInfo::exists(m_tempPath)) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("没有可保存的录音（先录一段）");
        }
        return false;
    }

    QString target = path;
    if (target.startsWith(QStringLiteral("file://"))) {
        target = target.mid(7);
    }
    // 扩展名决定容器：QMediaRecorder 已经按协商好的容器写好了内容，
    // 故这里只做"换个位置"，**不重新编码**（避免二次损失，也避免依赖转码器）。
    const QFileInfo info(target);
    QDir().mkpath(info.absolutePath());
    if (QFileInfo::exists(target) && !QFile::remove(target)) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("目标文件已存在且无法覆盖：%1").arg(target);
        }
        return false;
    }
    if (!QFile::copy(m_tempPath, target)) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("保存失败：无法写入 %1").arg(target);
        }
        return false;
    }
    if (errorOut != nullptr) {
        errorOut->clear();
    }
    return true;
}

} // namespace pitch

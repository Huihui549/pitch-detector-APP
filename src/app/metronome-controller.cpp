#include "metronome-controller.h"

#include "wav-reader.h"   // 自定义音频的第一级加载：直接读未压缩 PCM WAV

#include <QDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QSettings>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <cstring>

#ifndef PITCH_HAVE_QT_MULTIMEDIA
#define PITCH_HAVE_QT_MULTIMEDIA 0
#endif

#if PITCH_HAVE_QT_MULTIMEDIA
#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioFormat>
#endif

namespace pitch {
namespace {

/// 自定义样本的播放上限（秒）：节拍器只需要短促的一声。
/// 用户上传的可能是整首歌，不截断会糊成一片，也会让每次点击都占用很长的时间窗。
constexpr double kSampleMaxSeconds = 1.0;

/// 自定义样本先归一化到满量程，再用"角色目标峰值"作为增益。
/// 这样自定义音色与内置音色的**峰值口径完全一致**（强拍 0.95 / 弱拍 0.80 / 细分 0.45），
/// 用户换个音色不会突然变大变小，也不会因为素材本身过载而削顶。
constexpr float kSampleFullScale = 1.0f;

QString roleTitle(int role) {
    switch (role) {
    case 0:
        return QStringLiteral("强拍");
    case 2:
        return QStringLiteral("细分");
    default:
        return QStringLiteral("弱拍");
    }
}

QString builtinName(int role) {
    switch (role) {
    case 0:
        return QStringLiteral("内置 · 高音（1568 Hz）");
    case 2:
        return QStringLiteral("内置 · 轻音（784 Hz）");
    default:
        return QStringLiteral("内置 · 中音（1047 Hz）");
    }
}

/// 把任意采样率/声道的样点归一化（下混已在调用方完成）。
void normalizePeak(std::vector<float>& samples, float target) {
    float peak = 0.0f;
    for (const float v : samples) {
        peak = std::max(peak, std::fabs(v));
    }
    if (peak <= 1e-6f) {
        return;
    }
    const float scale = target / peak;
    for (float& v : samples) {
        v *= scale;
    }
}

} // namespace

MetronomeController::MetronomeController(QObject* parent) : QObject(parent) {
    for (int role = 0; role < 3; ++role) {
        m_voiceName[role] = builtinName(role);
        applyVoice(role);
    }

    // 拍点指示：以 ~25 ms 轮询引擎发布的原子量（音频线程只写、界面线程只读，无需加锁）
    m_tickTimer.setInterval(25);
    connect(&m_tickTimer, &QTimer::timeout, this, &MetronomeController::pollTick);
    m_tickTimer.start();

    // 试听用的自动收尾（见 previewSound）
    m_previewTimer.setSingleShot(true);
    connect(&m_previewTimer, &QTimer::timeout, this, &MetronomeController::stopPreviewIfNeeded);

    loadSettings();
    refreshStatus();
}

// ---------------------------------------------------------------- 速度

QString MetronomeController::tempoTerm() const {
    // 术语按通行区间给出，仅作参考（同一术语在不同教材里区间略有差异）
    const int b = m_bpm;
    if (b < 40) {
        return QStringLiteral("Grave");
    }
    if (b < 60) {
        return QStringLiteral("Largo");
    }
    if (b < 66) {
        return QStringLiteral("Larghetto");
    }
    if (b < 76) {
        return QStringLiteral("Adagio");
    }
    if (b < 108) {
        return QStringLiteral("Andante");
    }
    if (b < 120) {
        return QStringLiteral("Moderato");
    }
    if (b < 156) {
        return QStringLiteral("Allegro");
    }
    if (b < 200) {
        return QStringLiteral("Vivace");
    }
    return QStringLiteral("Presto");
}

void MetronomeController::applyBpm(int bpm) {
    const int clamped = clampBpm(bpm);
    if (clamped == m_bpm) {
        return;
    }
    m_bpm = clamped;
    m_engine.setBpm(m_bpm);
    QSettings settings;
    settings.setValue(QStringLiteral("metronome/bpm"), m_bpm);
    emit bpmChanged();
    refreshStatus();
}

void MetronomeController::setBpm(int bpm) {
    // 手动改速度 → 点击测速的历史作废（否则下一次点击会与旧间隔一起平均，给出莫名其妙的值）
    m_tap.reset();
    m_tapInfo.clear();
    applyBpm(bpm);
    emit statusChanged();
}

void MetronomeController::nudgeBpm(int delta) {
    setBpm(m_bpm + delta);
}

void MetronomeController::tapTempo() {
    if (!m_tapClock.isValid()) {
        m_tapClock.start();
    }
    const int bpm = m_tap.tap(m_tapClock.elapsed());
    if (bpm > 0) {
        applyBpm(bpm);   // 注意：不走 setBpm，否则会把刚积累的点击历史清掉
        m_tapInfo = QStringLiteral("已点 %1 下 → %2 BPM").arg(m_tap.tapCount()).arg(bpm);
    } else {
        m_tapInfo = QStringLiteral("已点 %1 下（再点几下就能算出速度）").arg(m_tap.tapCount());
    }
    emit statusChanged();
}

// ---------------------------------------------------------------- 拍号与细分

QString MetronomeController::meterLabel() const {
    return QString::fromStdString(pitch::meterLabel(m_pattern.meter));
}

QVariantList MetronomeController::subdivisions() const {
    QVariantList list;
    for (const int sub : m_pattern.subdivisions) {
        list.append(sub);
    }
    return list;
}

QString MetronomeController::patternSummary() const {
    const Pattern n = normalize(m_pattern);
    QString text = QString::fromStdString(pitch::meterLabel(n.meter));
    text += QStringLiteral(" ｜ 每拍：");
    for (std::size_t i = 0; i < n.subdivisions.size(); ++i) {
        if (i != 0) {
            text += QStringLiteral(" / ");
        }
        text += QString::fromStdString(pitch::subdivisionLabel(n.subdivisions[i]));
    }
    return text;
}

QVariantList MetronomeController::presetMeters() const {
    QVariantList list;
    for (const Meter& m : pitch::presetMeters()) {
        QVariantMap item;
        item.insert(QStringLiteral("label"), QString::fromStdString(pitch::meterLabel(m)));
        item.insert(QStringLiteral("beats"), m.beats);
        item.insert(QStringLiteral("unit"), m.unit);
        list.append(item);
    }
    return list;
}

void MetronomeController::applyPreset(int index) {
    const std::vector<Meter> presets = pitch::presetMeters();
    if (index < 0 || index >= static_cast<int>(presets.size())) {
        return;
    }
    applyMeter(presets[static_cast<std::size_t>(index)].beats,
               presets[static_cast<std::size_t>(index)].unit);
}

void MetronomeController::applyMeter(int beats, int unit) {
    Pattern next;
    next.meter.beats = beats;
    next.meter.unit = unit;
    // 换拍号时把细分数清零（全部整拍）：沿用旧数组会得到"新的拍数 + 旧的细分"这种随机组合
    next.subdivisions.assign(static_cast<std::size_t>(std::max(1, beats)), 1);
    m_pattern = normalize(next);
    m_engine.setPattern(m_pattern);
    savePattern();
    emit patternChanged();
    refreshStatus();
}

void MetronomeController::cycleSubdivision(int beatIndex) {
    Pattern next = normalize(m_pattern);
    if (beatIndex < 0 || beatIndex >= static_cast<int>(next.subdivisions.size())) {
        return;
    }
    const int current = next.subdivisions[static_cast<std::size_t>(beatIndex)];
    const int following = (current >= kMaxSubdivision) ? kMinSubdivision : current + 1;
    setSubdivision(beatIndex, following);
}

void MetronomeController::setSubdivision(int beatIndex, int subdivision) {
    Pattern next = normalize(m_pattern);
    if (beatIndex < 0 || beatIndex >= static_cast<int>(next.subdivisions.size())) {
        return;
    }
    next.subdivisions[static_cast<std::size_t>(beatIndex)] =
        std::clamp(subdivision, kMinSubdivision, kMaxSubdivision);
    m_pattern = next;
    m_engine.setPattern(m_pattern);
    savePattern();
    emit patternChanged();
    refreshStatus();
}

void MetronomeController::setAllSubdivisions(int subdivision) {
    Pattern next = normalize(m_pattern);
    for (int& value : next.subdivisions) {
        value = std::clamp(subdivision, kMinSubdivision, kMaxSubdivision);
    }
    m_pattern = next;
    m_engine.setPattern(m_pattern);
    savePattern();
    emit patternChanged();
    refreshStatus();
}

// ---------------------------------------------------------------- 播放

void MetronomeController::start() {
    if (m_userPlaying) {
        return;
    }
    m_previewStartedEngine = false;
    m_previewTimer.stop();
    m_engine.setBpm(m_bpm);
    m_engine.setPattern(m_pattern);
    const bool ok = m_engine.start();
    if (!ok) {
        m_notice = m_engine.lastError();
        m_userPlaying = false;
    } else {
        m_userPlaying = true;
    }
    refreshStatus();
    emit playingChanged();
}

void MetronomeController::stop() {
    m_previewTimer.stop();
    m_previewStartedEngine = false;
    const bool wasPlaying = m_userPlaying;
    m_userPlaying = false;
    if (m_engine.running()) {
        m_engine.stop();
    }
    m_activeBeat = -1;
    m_activeSub = 0;
    m_activeAccent = false;
    refreshStatus();
    if (wasPlaying || !m_engine.running()) {
        emit playingChanged();
        emit tickChanged();
    }
}

void MetronomeController::toggle() {
    if (m_userPlaying) {
        stop();
    } else {
        start();
    }
}

void MetronomeController::pollTick() {
    const TickInfo tick = m_engine.pollTick();
    if (tick.counter < 0 || tick.counter == m_lastTickCounter) {
        return;
    }
    m_lastTickCounter = tick.counter;
    ++m_tickCount;
    m_activeBeat = tick.beatIndex;
    m_activeSub = tick.subIndex;
    m_activeAccent = tick.accent;
    emit tickChanged();
}

// ---------------------------------------------------------------- 音色

QVariantList MetronomeController::soundRoles() const {
    QVariantList list;
    for (int role = 0; role < 3; ++role) {
        QVariantMap item;
        item.insert(QStringLiteral("role"), role);
        item.insert(QStringLiteral("title"), roleTitle(role));
        item.insert(QStringLiteral("name"), m_voiceName[role]);
        item.insert(QStringLiteral("custom"), m_voiceCustom[role]);
        list.append(item);
    }
    return list;
}

void MetronomeController::applyVoice(int role) {
    // 只负责"回到内置音色"：内置音色已在 core 里按角色峰值归一化（voice.gain = 1.0），
    // 自定义样本由 loadSample() 直接装进引擎
    m_engine.setVoice(role, builtinVoice(role));
}

QString MetronomeController::chooseSound(int role) {
    if (role < 0 || role > 2) {
        return {};
    }
    const QString path = QFileDialog::getOpenFileName(
        nullptr, QStringLiteral("选择节拍音音频（推荐未压缩 PCM WAV；也可用 mp3/m4a 等）"),
        m_voicePath[role].isEmpty() ? QString() : QFileInfo(m_voicePath[role]).absolutePath(),
        QStringLiteral("音频文件 (*.wav *.mp3 *.m4a *.aac *.ogg *.flac);;所有文件 (*)"));
    if (path.isEmpty()) {
        return {};
    }
    QString error;
    if (loadSample(path, role, &error)) {
        m_voicePath[role] = path;
        m_voiceCustom[role] = true;
        m_voiceName[role] = QFileInfo(path).fileName() + QStringLiteral("（自定义）");
        m_notice.clear();
        saveVoices();
        emit voicesChanged();
        refreshStatus();
    } else {
        m_notice = error;
        emit statusChanged();
    }
    return path;
}

void MetronomeController::clearSound(int role) {
    if (role < 0 || role > 2) {
        return;
    }
    m_voiceCustom[role] = false;
    m_voicePath[role].clear();
    m_voiceName[role] = builtinName(role);
    applyVoice(role);
    saveVoices();
    m_notice.clear();
    emit voicesChanged();
    refreshStatus();
}

void MetronomeController::previewSound(int role) {
    if (role < 0 || role > 2) {
        return;
    }
    if (!m_engine.running()) {
        // 未在播放时也能试听：临时启动输出，约 0.8 秒后自动停。
        // 注意 playing() 报的是**用户意图**（m_userPlaying），故试听期间界面仍显示"未播放"。
        if (!m_engine.available()) {
            m_notice = m_engine.unavailableReason();
            emit statusChanged();
            return;
        }
        if (!m_engine.start()) {
            m_notice = m_engine.lastError();
            emit statusChanged();
            return;
        }
        m_previewStartedEngine = true;
    }
    m_engine.preview(role);
    m_previewTimer.start(800);
}

void MetronomeController::stopPreviewIfNeeded() {
    if (!m_previewStartedEngine) {
        return;
    }
    m_previewStartedEngine = false;
    if (m_userPlaying) {
        return;   // 这 0.8 秒内用户点了播放：输出要留着，不能停
    }
    m_engine.stop();
    refreshStatus();
    emit playingChanged();
}

bool MetronomeController::loadSample(const QString& path, int role, QString* errorOut) {
    if (path.isEmpty()) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("未选择文件");
        }
        return false;
    }

    // 第一级：未压缩 PCM WAV 直接读（不依赖 Qt Multimedia，行为最确定）
    const WavData wav = readWavMono(path.toUtf8().toStdString());
    if (wav.ok && !wav.samples.empty() && wav.sampleRate > 0.0) {
        ClickVoice voice;
        voice.sample = wav.samples;
        voice.sampleRate = wav.sampleRate;
        voice.maxSeconds = kSampleMaxSeconds;
        normalizePeak(voice.sample, kSampleFullScale);
        voice.gain = rolePeakTarget(role);
        m_engine.setVoice(role, voice);
        return true;
    }

#if PITCH_HAVE_QT_MULTIMEDIA
    // 第二级：交给 QAudioDecoder（mp3/m4a/ogg… 由 Qt Multimedia 的后端解码，本项目是 FFmpeg）。
    // 信号必须在 start() **之前**连上，否则开头几个 bufferReady 会丢（与采集侧同一个坑）。
    QAudioDecoder decoder;
    QEventLoop loop;
    std::vector<float> samples;
    double rate = 0.0;

    QObject::connect(&decoder, &QAudioDecoder::bufferReady, [&decoder, &samples, &rate]() {
        const QAudioBuffer buffer = decoder.read();
        if (!buffer.isValid()) {
            return;
        }
        const QAudioFormat format = buffer.format();
        rate = static_cast<double>(format.sampleRate());
        const int ch = std::max(1, format.channelCount());
        const int frames = static_cast<int>(buffer.frameCount());
        if (frames <= 0) {
            return;
        }
        samples.reserve(samples.size() + static_cast<std::size_t>(frames));

        // 按缓冲的实际格式取数：QAudioBuffer 只提供模板化的 constData<T>()，
        // 故这里对两种会遇到的格式分别取值（其余格式视为无法使用，直接跳过本块）。
        const float* asFloat = nullptr;
        const qint16* asInt16 = nullptr;
        const bool isFloat = (format.sampleFormat() == QAudioFormat::Float);
        const bool isInt16 = (format.sampleFormat() == QAudioFormat::Int16);
        if (isFloat) {
            asFloat = buffer.constData<float>();
        } else if (isInt16) {
            asInt16 = buffer.constData<qint16>();
        }
        if (asFloat == nullptr && asInt16 == nullptr) {
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
    // 注意：QAudioDecoder 同时有同名成员——取值函数 error() 与信号 error(Error)，
    // 直接取 &QAudioDecoder::error 会因重载而无法解析，故显式指定信号签名。
    QObject::connect(&decoder,
                     static_cast<void (QAudioDecoder::*)(QAudioDecoder::Error)>(&QAudioDecoder::error),
                     &loop, &QEventLoop::quit);

    decoder.setSource(QUrl::fromLocalFile(path));
    decoder.start();
    loop.exec();   // 解码是异步的：节拍音通常只有几十 KB，这里等待很短
    const bool failed = (decoder.error() != QAudioDecoder::NoError);
    decoder.stop();

    if (!failed && !samples.empty() && rate > 0.0) {
        ClickVoice voice;
        voice.sample = samples;
        voice.sampleRate = rate;
        voice.maxSeconds = kSampleMaxSeconds;
        normalizePeak(voice.sample, kSampleFullScale);
        voice.gain = rolePeakTarget(role);
        m_engine.setVoice(role, voice);
        return true;
    }
#endif

    if (errorOut != nullptr) {
        *errorOut = QStringLiteral("无法载入该音频：%1。建议改用未压缩 PCM WAV"
                                   "（手机上请先把文件放到 Download/Music 等公共目录）")
                        .arg(wav.error.empty() ? QStringLiteral("解码失败")
                                               : QString::fromStdString(wav.error));
    }
    return false;
}

// ---------------------------------------------------------------- 状态与持久化

QString MetronomeController::outputDescription() const {
    const QString text = m_engine.description();
    return text.isEmpty() ? QStringLiteral("未打开输出设备") : text;
}

QString MetronomeController::engineStats() const {
    return QStringLiteral("已送出声卡 %1 帧 ｜ 音频回调 %2 次")
        .arg(m_engine.renderedFrames())
        .arg(m_engine.callbackCount());
}

void MetronomeController::refreshStatus() {
    QStringList parts;
    if (m_engine.running()) {
        parts << QStringLiteral("播放中");
    } else {
        parts << QStringLiteral("已停止");
    }
    parts << QStringLiteral("%1 BPM").arg(m_bpm);
    parts << QStringLiteral("%1 拍/小节").arg(m_pattern.meter.beats);
    if (m_engine.actualSampleRate() > 0) {
        parts << QStringLiteral("%1 Hz").arg(m_engine.actualSampleRate());
    }
    m_statusText = parts.join(QStringLiteral(" ｜ "));
    if (m_notice.isEmpty() && !m_engine.available()) {
        m_notice = m_engine.unavailableReason();
    }
    emit statusChanged();
}

void MetronomeController::loadSettings() {
    QSettings settings;
    m_bpm = clampBpm(settings.value(QStringLiteral("metronome/bpm"), kDefaultBpm).toInt());
    m_pattern = fromString(
        settings.value(QStringLiteral("metronome/pattern"), QStringLiteral("4/4:1,1,1,1"))
            .toString()
            .toStdString());
    m_engine.setBpm(m_bpm);
    m_engine.setPattern(m_pattern);

    for (int role = 0; role < 3; ++role) {
        const QString path = settings.value(QStringLiteral("metronome/voice%1").arg(role)).toString();
        if (path.isEmpty()) {
            continue;
        }
        QString error;
        if (loadSample(path, role, &error)) {
            m_voicePath[role] = path;
            m_voiceCustom[role] = true;
            m_voiceName[role] = QFileInfo(path).fileName() + QStringLiteral("（自定义）");
        } else {
            // 配置里记着的文件没了（删除/换机）：回到内置音色，并把问题告诉用户
            m_notice = QStringLiteral("上次用的自定义节拍音已不可用：%1").arg(error);
        }
    }
    emit bpmChanged();
    emit patternChanged();
    emit voicesChanged();
}

void MetronomeController::savePattern() {
    QSettings settings;
    settings.setValue(QStringLiteral("metronome/pattern"),
                      QString::fromStdString(toString(m_pattern)));
}

void MetronomeController::saveVoices() {
    QSettings settings;
    for (int role = 0; role < 3; ++role) {
        settings.setValue(QStringLiteral("metronome/voice%1").arg(role), m_voicePath[role]);
    }
}

void MetronomeController::resetToDefaults() {
    setBpm(kDefaultBpm);
    applyMeter(4, 4);
    for (int role = 0; role < 3; ++role) {
        clearSound(role);
    }
    m_notice.clear();
    refreshStatus();
}

} // namespace pitch

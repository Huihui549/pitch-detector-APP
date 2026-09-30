// 实时会话控制器 —— 实现
//
// 算法调用点全部集中在本文件的 processFrame()：界面与其它模块都不得自行算音高（分层铁律）。

#include "pitch-session-controller.h"

#include "file-audio-source.h"
#include "note-converter.h"
#include "pitch-engine.h"

#if PITCH_HAVE_QT_MULTIMEDIA
#include "qt-audio-source.h"
#endif

#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>
#include <cmath>

namespace pitch {

namespace {

/// 环形缓冲上限：要能同时容纳最大的实时窗（本项目实时窗最大 16384）加一个回调批次。
constexpr std::size_t kRingLimit = 16384 + 4096;

/// 实时检测的窗长候选（升序）。
///
/// 与上游的差异（有意为之，理由如下）：
///   上游实时链路是"固定 4096 窗、82–1050 Hz"，音域只覆盖中段（约 22 个音）；
///   文件分析则用 1024→16384 级联覆盖全 88 键。本项目首版就要"手机 APP 能测钢琴/吉他"，
///   4096 窗对 A0（27.5 Hz，周期 1604 样点）装不下足够周期，必然判不出。
///   故实时链路采用**两级窗长**：先 4096（低延迟，中高音即刻可用），
///   若无效再用 16384（多约 279 ms 才够样点，仅用于低音）。
///   这与"按最高音/最低音收口 τ 区间"的算法内部逻辑一致，不改变任何判据。
///   代价：低音在启动后约 0.65 s 内不可测（要攒够 16384 样点），且 16384 窗的单帧代价更高。
///   决策已记入 ADR-0009。
const std::vector<std::size_t> kRealtimeLadder{4096, 16384};

} // namespace

PitchSessionController::PitchSessionController(QObject* parent) : QObject(parent) {
    m_ringLimit = kRingLimit;
    m_holdTimer.setSingleShot(true);
    m_holdTimer.setInterval(kHoldMs);
    connect(&m_holdTimer, &QTimer::timeout, this, &PitchSessionController::onHoldTimeout);

#if PITCH_HAVE_QT_MULTIMEDIA
    m_unavailableReason.clear();
#else
    m_unavailableReason = QStringLiteral(
        "未安装 Qt Multimedia 模块，麦克风不可用。"
        "请用 Qt 安装目录下的 MaintenanceTool 勾选 Qt Multimedia 后重新构建；"
        "或在下方选择“文件回放”用一段 WAV 验证实时链路。");
#endif
}

PitchSessionController::~PitchSessionController() = default;

QString PitchSessionController::frameDescription() const {
    QStringList parts;
    for (const std::size_t n : kRealtimeLadder) {
        parts << QString::number(static_cast<qulonglong>(n));
    }
    return parts.join(QStringLiteral(" / "));
}

QString PitchSessionController::rangeDescription() const {
    return QStringLiteral("%1–%2 Hz")
        .arg(m_config.fMin, 0, 'f', 0)
        .arg(m_config.fMax, 0, 'f', 0);
}

QString PitchSessionController::captureStats() const {
#if PITCH_HAVE_QT_MULTIMEDIA
    // 只有麦克风采集有轮询统计；文件回放走定时器喂数据，统计意义不同
    if (const auto* mic = dynamic_cast<const QtAudioSource*>(m_source.get())) {
        return mic->statsDescription();
    }
#endif
    if (m_source) {
        return m_source->description();
    }
    return QStringLiteral("未启动");
}

bool PitchSessionController::takeWindow(std::size_t frame, std::vector<float>& out) const {
    if (m_ring.size() < frame) {
        return false;
    }
    out.resize(frame);
    // 取最近 frame 个样点：deque 的末尾即最新
    const std::size_t start = m_ring.size() - frame;
    for (std::size_t i = 0; i < frame; ++i) {
        out[i] = m_ring[start + i];
    }
    return true;
}

void PitchSessionController::adoptSource(std::unique_ptr<IAudioSource> source) {
    m_source = std::move(source);
    if (!m_source) {
        return;
    }
    // 全部信号都在这里统一连接——漏掉任何一个都会表现为"看起来在跑但没有数据"
    connect(m_source.get(), &IAudioSource::samplesReady,
            this, &PitchSessionController::onSamplesReady);
    connect(m_source.get(), &IAudioSource::errorOccurred,
            this, &PitchSessionController::onAudioError);
    connect(m_source.get(), &IAudioSource::formatMismatch,
            this, &PitchSessionController::onFormatMismatch);
    connect(m_source.get(), &IAudioSource::stateChanged,
            this, &PitchSessionController::onAudioStateChanged);

    m_sourceDescription = m_source->description();
    m_peakRms = 0.0;
    m_callbackCount = 0;
    // 节流计时器重新起算：否则上一轮的时间戳会让首帧被跳过
    m_framePacer.start();
    emit stateChanged();
}

void PitchSessionController::startMicrophone() {
#if PITCH_HAVE_QT_MULTIMEDIA
    stop();
    setNotice(QString());
    m_ring.clear();
    m_unavailableReason.clear();
    adoptSource(std::make_unique<QtAudioSource>());
    if (m_source) {
        m_source->start(44100, 1);
    }
#else
    m_unavailableReason = QStringLiteral(
        "未安装 Qt Multimedia 模块，麦克风不可用。请用 Qt 安装目录下的 MaintenanceTool 勾选 Qt Multimedia 后重新构建。");
    emit stateChanged();
#endif
}

void PitchSessionController::startFilePlayback(const QString& wavPath) {
    stop();
    auto source = std::make_unique<FileAudioSource>();
    if (!source->load(wavPath)) {
        m_stateText = QStringLiteral("载入失败");
        m_unavailableReason = QStringLiteral("无法载入音频文件（需未压缩 PCM WAV）：%1").arg(wavPath);
        emit stateChanged();
        return;
    }
    setNotice(QString());
    m_ring.clear();
    m_unavailableReason.clear();
    resetStatistics();
    adoptSource(std::move(source));
    if (m_source) {
        m_source->start(44100, 1);
    }
}

QString PitchSessionController::chooseAudioFile() {
    // 原生对话框：行为确定，且返回的是本地文件路径（不是 URL）。
    // QML 的 FileDialog 在 Windows 上曾给出无法使用的路径（用户实测），故改用这里。
    const QString path = QFileDialog::getOpenFileName(
        nullptr, QStringLiteral("选择音频文件（未压缩 PCM WAV）"), m_lastDir,
        QStringLiteral("WAV 音频 (*.wav);;所有文件 (*)"));
    if (path.isEmpty()) {
        return {};   // 用户取消
    }
    m_lastDir = QFileInfo(path).absolutePath();
    return path;
}

QString PitchSessionController::chooseAudioFileAndPlay() {
    const QString path = chooseAudioFile();
    if (path.isEmpty()) {
        return {};
    }
    startFilePlayback(path);
    return path;
}

void PitchSessionController::injectSamples(const QVector<float>& mono, int sampleRate) {
    // 与麦克风共用同一段"入环缓冲 + 处理"的代码，但**绕过节流**。
    //
    // 为什么必须绕过节流：注入是瞬间完成的（一个循环里塞完 1.2 秒信号），
    // 若走 onSamplesReady 的 35 ms 节流，只有第一批会被处理，后面全被丢弃，
    // 环缓冲永远攒不满一帧 → 检出恒为"—"。这是实测踩过的回归：
    // 加节流后 --looptest 从 5/5 掉到 0/5。
    // 真实采集有 QTimer 提供的时间间隔，节流才有意义。
    m_running = true;
    m_stateText = QStringLiteral("注入测试");
    m_sampleRate = sampleRate > 0 ? sampleRate : m_sampleRate;

    double sumSquares = 0.0;
    for (const float v : mono) {
        sumSquares += static_cast<double>(v) * static_cast<double>(v);
    }
    if (!mono.isEmpty()) {
        const double rawRms = std::sqrt(sumSquares / static_cast<double>(mono.size()));
        if (rawRms > m_peakRms) {
            m_peakRms = rawRms;
        }
    }
    ++m_callbackCount;

    for (const float v : mono) {
        m_ring.push_back(v);
    }
    while (m_ring.size() > m_ringLimit) {
        m_ring.pop_front();
    }

    processFrame();
    emit readingChanged();
}

void PitchSessionController::stop() {
    if (m_source) {
        m_source->stop();
        m_source.reset();
    }
    m_running = false;
    m_stateText = QStringLiteral("已停止");
    m_holdTimer.stop();
    m_recentReadings.clear();   // 稳定性窗口跟着会话走，避免上一段的读数影响下一段
    m_noteName = QStringLiteral("—");
    m_confidence = 0.0;
    m_frequency = 0.0;
    m_rms = 0.0;
    m_yinCurve.clear();
    emit readingChanged();
    emit stateChanged();
}

void PitchSessionController::resetStatistics() {
    m_curvePoints.clear();
    m_curve.clear();
    m_highestNote = QStringLiteral("—");
    m_lowestNote = QStringLiteral("—");
    m_highestMidi = -1;
    m_lowestMidi = -1;
    m_rangeSemitones = 0;
    m_validSeconds = 0.0;
    m_consecutiveHits = 0;
    m_peakRms = 0.0;
    m_clock.restart();
    m_lastFrameMs = 0;
    emit rangeChanged();
    emit curveChanged();
    emit statisticsReset();
}

void PitchSessionController::setNotice(const QString& text) {
    if (m_notice == text) {
        return;
    }
    m_notice = text;
    emit noticeChanged();
}

void PitchSessionController::onFormatMismatch(int requestedRate, int actualRate,
                                              int requestedChannels, int actualChannels) {
    setNotice(QStringLiteral("注意：设备实际格式与请求不同（请求 %1 Hz/%2 声道，实际 %3 Hz/%4 声道），"
                             "已按实际格式计算。")
                  .arg(requestedRate)
                  .arg(requestedChannels)
                  .arg(actualRate)
                  .arg(actualChannels));
    m_sampleRate = actualRate;
    emit stateChanged();
}

void PitchSessionController::onAudioError(AudioErrorKind kind, const QString& message) {
    Q_UNUSED(kind);
    m_stateText = QStringLiteral("采集错误");
    m_unavailableReason = message;
    m_running = false;
    emit stateChanged();
}

void PitchSessionController::onAudioStateChanged(AudioState state) {
    switch (state) {
    case AudioState::Running:
        m_running = true;
        m_stateText = QStringLiteral("正在监听");
        m_clock.restart();
        m_lastFrameMs = 0;
        // **设备描述要在真正启动之后再取一次**：用哪个设备是 start() 里才决定的
        // （例如系统默认是蓝牙时会改用内置麦克风），启动前取到的只是"列表第一个"，
        // 与实际使用的设备不一致——实测因此误判成"改动没生效"。
        if (m_source != nullptr) {
            m_sourceDescription = m_source->description();
        }
        break;
    case AudioState::Stopped:
        m_running = false;
        m_stateText = QStringLiteral("已停止");
        break;
    case AudioState::Starting:
        m_stateText = QStringLiteral("启动中…");
        break;
    case AudioState::Error:
        m_running = false;
        m_stateText = QStringLiteral("采集错误");
        break;
    }
    emit stateChanged();
}

void PitchSessionController::onSamplesReady(const QVector<float>& mono, int sampleRate) {
    if (mono.isEmpty()) {
        return;
    }
    m_sampleRate = sampleRate > 0 ? sampleRate : m_sampleRate;

    // 记录**原始**信号强度峰值（在静音门槛之前）：这是判断"麦克风是否收到声音"的唯一可靠指标。
    // 不加它就分不清"没声音"与"有声音但算法没出结果"（实测因此绕过弯路）。
    //
    // 注意用 double 累加：float 样点的平方和在长缓冲上直接累加到 float 会丢精度，
    // 极小信号（如本机麦克风在安静环境下的电平）会被算成 0（实测踩过）。
    double sumSquares = 0.0;
    for (const float v : mono) {
        sumSquares += static_cast<double>(v) * static_cast<double>(v);
    }
    const double rawRms = mono.isEmpty() ? 0.0 : std::sqrt(sumSquares / static_cast<double>(mono.size()));
    if (rawRms > m_peakRms) {
        m_peakRms = rawRms;
    }
    ++m_callbackCount;

    for (const float v : mono) {
        m_ring.push_back(v);
    }
    // 环缓冲上限：超出即丢最旧的（实时链路只关心最近一段）
    while (m_ring.size() > m_ringLimit) {
        m_ring.pop_front();
    }

    // 检测节流：按固定节奏出帧（约 28 次/秒）。
    // 不节流会跟着回调频率走——实测低到 7 次/秒，用户直接感觉"不灵敏"。
    if (m_framePacer.isValid() && m_framePacer.elapsed() < kFrameIntervalMs) {
        return;
    }
    m_framePacer.restart();
    processFrame();
}

void PitchSessionController::processFrame() {
    // 算法要的是连续内存；deque 不保证连续（也没有 data()），故先复制到连续缓冲。
    // 复制量固定为环缓冲上限（约 20 K 个 float = 80 KB），远比逐帧重算便宜，可接受。
    if (m_ring.size() < 4096) {
        return;   // 连最短窗都不够，直接跳过
    }
    m_scratch.assign(m_ring.begin(), m_ring.end());
    const std::span<const float> ringSpan(m_scratch.data(), m_scratch.size());

    // 先短窗（低延迟、高音准），无效再长窗（低音需要足够周期）
    const auto result = PitchEngine::detectWithLadderSizes(
        ringSpan, 0, static_cast<double>(m_sampleRate), 0.0, m_config, m_buffers, kRealtimeLadder);

    if (!result.has_value()) {
        // 无效帧：启动保持计时器；到期后读数清成"—"（不留残值，pitfalls #2）
        if (!m_holdTimer.isActive()) {
            m_holdTimer.start();
        }
        return;
    }

    std::vector<float> window;
    if (!takeWindow(result->frameSize, window)) {
        return;
    }
    const std::span<const float> windowSpan(window.data(), window.size());

    // 静音门槛：与上游同为"绝对 + 相对峰值"。实时链路没有整段峰值，故用当前窗峰值近似
    // （上游实时页同样只有局部信息，口径一致）
    const double frameRms = PitchEngine::rms(windowSpan);
    double peak = 0.0;
    for (const float v : window) {
        peak = std::max(peak, std::abs(static_cast<double>(v)));
    }
    const double rmsFloor = std::max(kRmsMin, peak * kRmsRelMin);
    m_rmsFloor = rmsFloor;   // 暴露给界面画"静音门槛刻线"，界面不得自己写这个数
    if (frameRms < rmsFloor) {
        if (!m_holdTimer.isActive()) {
            m_holdTimer.start();
        }
        return;
    }

    const NoteInfo info = NoteConverter::fromFrequency(result->freq, m_config.a4);
    m_rms = frameRms;
    // 保持计时器的语义：**连续 kHoldMs 没有可接受读数**才转「—」（不留残值，pitfalls #2）。
    // 故它只在下面两个"拒绝该帧"的分支里启动（且已启动时不重启），接受读数时停用；

    // ① **显示门槛**：低置信度帧不许改写读数。
    //    没有这一条时，噪声与辅音帧（置信度 0.1~0.5）会把音名刷成别的音，界面看起来
    //    "唱 C4 却来回跳、就是出不来 C4"（用户真机实测）。
    if (result->confidence < kDisplayMinConfidence) {
        if (!m_holdTimer.isActive()) {
            m_holdTimer.start();
        }
        return;
    }

    // ② **稳定性**：把达标帧放进小窗口，只显示"窗口内占多数"的音名（≥ kStableMinCount 帧）。
    //    单靠门槛仍会跳：人声换气/辅音帧偶尔也过门槛且常偏一个八度。
    //    不足多数就沿用上一个可靠读数（保持计时器到期后自动转「—」，不留残值）。
    m_recentReadings.push_back(RecentReading{info.noteIndex, info.octave, info.cents,
                                             result->freq, result->confidence});
    while (static_cast<int>(m_recentReadings.size()) > kStableWindow) {
        m_recentReadings.pop_front();
    }
    int bestNote = m_recentReadings.back().noteIndex;
    int bestCount = 0;
    for (const RecentReading& candidate : m_recentReadings) {
        int count = 0;
        for (const RecentReading& other : m_recentReadings) {
            if (other.noteIndex == candidate.noteIndex) {
                ++count;
            }
        }
        if (count >= bestCount) {   // >= 让并列时取更新的那个
            bestCount = count;
            bestNote = candidate.noteIndex;
        }
    }
    if (bestCount < kStableMinCount) {
        if (!m_holdTimer.isActive()) {
            m_holdTimer.start();
        }
        return;
    }
    // 显示值取窗口内该音名的**最新一帧**，保证音名/频率/音分三者自洽
    for (auto it = m_recentReadings.rbegin(); it != m_recentReadings.rend(); ++it) {
        if (it->noteIndex == bestNote) {
            m_noteName = QString::fromLatin1(NoteConverter::format(it->noteIndex, it->octave));
            m_octave = it->octave;
            m_cents = it->cents;
            m_frequency = it->freq;
            m_confidence = it->confidence;
            break;
        }
    }

    // 接受了新读数：停用保持（下一段"无可靠读数"重新计时）
    m_holdTimer.stop();

    // 曲线与有效时长：只有通过门槛与稳定性检查的读数才会走到这里
    // （门槛统一在函数上方处理，这里不再重复判断，避免"曲线有点而读数没有"的不一致）
    {
        const double now = static_cast<double>(m_clock.elapsed()) / 1000.0;
        m_curvePoints.push_back(CurvePoint{now, m_frequency});
        while (!m_curvePoints.empty() && (now - m_curvePoints.front().t) > kCurveSeconds) {
            m_curvePoints.pop_front();
        }
        const double base = m_curvePoints.empty() ? 0.0 : m_curvePoints.front().t;
        m_curve.clear();
        for (const CurvePoint& p : m_curvePoints) {
            QVariantMap m;
            m.insert(QStringLiteral("t"), p.t - base);
            m.insert(QStringLiteral("freq"), p.freq);
            m_curve.append(m);
        }
        emit curveChanged();

        // 累计有效时长：按帧间实际间隔累加，而不是"帧数 × 假定帧长"
        // （采样率可能不是 44.1 kHz，帧进也不固定）
        const qint64 nowMs = m_clock.elapsed();
        if (m_lastFrameMs > 0) {
            m_validSeconds += static_cast<double>(nowMs - m_lastFrameMs) / 1000.0;
        }
        m_lastFrameMs = nowMs;
    }

    updateRangeTracking(info, m_confidence);

    // YIN 谷值曲线（调试用）。用同一窗再取一次中间量：算法内部的 cmnd 是逐帧复用的缓冲，
    // 界面不得自行重算（上游 pitfalls #22），故由引擎通过出参给出。
    std::span<const double> curveSpan;
    int tauMin = 0;
    int tauMax = 0;
    const auto withCurve = PitchEngine::detect(windowSpan, static_cast<double>(m_sampleRate),
                                               m_config, m_buffers, &curveSpan, &tauMin, &tauMax);
    if (withCurve.has_value() && !curveSpan.empty()) {
        m_yinCurve.clear();
        // 抽稀：界面画不下几千个 τ 点，每 4 个取一个足够描述谷形
        for (int tau = tauMin; tau <= tauMax; tau += 4) {
            QVariantMap m;
            m.insert(QStringLiteral("tau"), tau);
            m.insert(QStringLiteral("cmnd"), curveSpan[static_cast<std::size_t>(tau)]);
            m_yinCurve.append(m);
        }
    }

    emit readingChanged();
}

void PitchSessionController::updateRangeTracking(const NoteInfo& info, double confidence) {
    // 上游口径：置信度门槛 0.85（比实时显示的 0.75 更严）+ 连续命中 ≥3 帧
    if (confidence < 0.85) {
        m_consecutiveHits = 0;
        return;
    }
    ++m_consecutiveHits;
    if (m_consecutiveHits < kRangeMinConsecutive) {
        return;
    }

    const int midi = static_cast<int>(std::llround(info.midi));
    bool changed = false;
    if (m_highestMidi < 0 || midi > m_highestMidi) {
        m_highestMidi = midi;
        m_highestNote = QString::fromLatin1(NoteConverter::format(info.noteIndex, info.octave));
        changed = true;
    }
    if (m_lowestMidi < 0 || midi < m_lowestMidi) {
        m_lowestMidi = midi;
        m_lowestNote = QString::fromLatin1(NoteConverter::format(info.noteIndex, info.octave));
        changed = true;
    }
    if (m_highestMidi >= 0 && m_lowestMidi >= 0) {
        m_rangeSemitones = m_highestMidi - m_lowestMidi;
        changed = true;
    }
    if (changed) {
        emit rangeChanged();
    }
}

void PitchSessionController::onHoldTimeout() {
    // 保持到期：清读数，但**不清统计与曲线**（那是本次会话的累计结果）
    m_noteName = QStringLiteral("—");
    m_frequency = 0.0;
    m_cents = 0.0;
    m_confidence = 0.0;
    m_consecutiveHits = 0;
    emit readingChanged();
}

} // namespace pitch

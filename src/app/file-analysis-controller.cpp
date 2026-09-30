// 文件分析控制器 —— 实现
//
// 线程模型（关键，改动前先读完）：
//   1. analyze() 在主线程：载入路径 → moveToThread(&m_worker) → m_worker.start()
//   2. runAnalysis() 在工作线程：只读文件、跑算法、把结果写进 m_pending*；**不碰 QML/界面对象**
//   3. runAnalysis() 末尾：moveToThread(主线程) → m_worker.quit() → 用
//      QMetaObject::invokeMethod(this, ..., Qt::QueuedConnection) 请求"在主线程收尾"
//   4. onAnalysisCompleted() 在主线程：把 m_pending* 交给模型、发 resultChanged / finished
//
// 为什么完成通知要走"排到主线程"而不是在工作线程直接 emit：
//   控制器在分析期间线程亲和性属于工作线程，此时 emit 的信号对"同线程"的接收者（含 QML）是
//   **直连**——接收者会在工作线程里跑，去碰界面对象就是未定义行为。
//   把收尾排到主线程后 emit，接收者才落在主线程，这是唯一安全的顺序。

#include "file-analysis-controller.h"

#include "analysis-runner.h"
#include "audio-file-decoder.h"   // 主流格式（mp3/m4a/aac/flac）+ PCM WAV 两级解码
#include "note-converter.h"
#include "piano-roll-renderer.h"  // 卷帘渲染（界面显示与导出长图共用同一实现）
#include "theme.h"                // 卷帘配色从主题令牌折算
#include "wav-reader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMetaObject>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

namespace pitch {

/* ============================ FrameTableModel ============================ */

FrameTableModel::FrameTableModel(QObject* parent) : QAbstractListModel(parent) {}

int FrameTableModel::rowCount(const QModelIndex& parent) const {
    // 列表模型：只有顶层有效
    return parent.isValid() ? 0 : static_cast<int>(m_frames.size());
}

QVariant FrameTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_frames.size())) {
        return {};
    }
    const Frame& f = m_frames[static_cast<std::size_t>(index.row())];
    switch (role) {
    case TimeRole:
        return f.timeSec;
    case FreqRole:
        return f.freq;
    case NoteRole:
        return QString::fromLatin1(NoteConverter::format(f.noteIndex, f.octave));
    case CentsRole:
        return f.cents;
    case ConfidenceRole:
        return f.confidence;
    case RmsRole:
        return f.rms;
    case OctaveFixedRole:
        return f.octaveFixed;
    default:
        return {};
    }
}

QHash<int, QByteArray> FrameTableModel::roleNames() const {
    return {
        {TimeRole, "timeSec"},
        {FreqRole, "freq"},
        {NoteRole, "note"},
        {CentsRole, "cents"},
        {ConfidenceRole, "confidence"},
        {RmsRole, "rms"},
        {OctaveFixedRole, "octaveFixed"},
    };
}

void FrameTableModel::setFrames(std::vector<Frame> frames) {
    beginResetModel();
    m_frames = std::move(frames);
    endResetModel();
}

void FrameTableModel::clear() {
    beginResetModel();
    m_frames.clear();
    endResetModel();
}

// 说明：原来的 `toCsv()` 已删除（用户 2026-09-30：导出 CSV 改为导出钢琴卷帘长图）。
// 逐帧数据仍通过本模型的 role 提供给界面表格，只是不再有 CSV 导出这条路径。

CurvePreview buildCurvePreview(const std::vector<Frame>& frames, int maxPoints) {
    CurvePreview preview;
    preview.totalFrames = static_cast<int>(frames.size());
    if (frames.empty() || maxPoints <= 0) {
        return preview;
    }

    const std::size_t step = std::max<std::size_t>(
        1, static_cast<std::size_t>((frames.size() + static_cast<std::size_t>(maxPoints) - 1) /
                                    static_cast<std::size_t>(maxPoints)));

    double lo = frames.front().freq;
    double hi = frames.front().freq;
    for (std::size_t i = 0; i < frames.size(); i += step) {
        const Frame& f = frames[i];
        QVariantMap point;
        point.insert(QStringLiteral("t"), f.timeSec);
        point.insert(QStringLiteral("freq"), f.freq);
        preview.points.append(point);
        lo = std::min(lo, f.freq);
        hi = std::max(hi, f.freq);
    }
    // 末点必补：抽样可能跳过最后一帧，曲线会缺尾
    if ((frames.size() - 1) % step != 0) {
        const Frame& last = frames.back();
        QVariantMap point;
        point.insert(QStringLiteral("t"), last.timeSec);
        point.insert(QStringLiteral("freq"), last.freq);
        preview.points.append(point);
        lo = std::min(lo, last.freq);
        hi = std::max(hi, last.freq);
    }

    const double pad = std::max(1.0, (hi - lo) * 0.15);
    preview.freqMin = lo - pad;
    preview.freqMax = hi + pad;
    preview.sampledFrames = static_cast<int>(preview.points.size());
    return preview;
}

/* ============================ FileAnalysisController ============================ */

FileAnalysisController::FileAnalysisController(ThemeProvider* theme, QObject* parent)
    : QObject(parent), m_theme(theme) {
    // 卷帘区域**从一打开就存在**（用户要求常显）：先按默认音域/时长算好几何，
    // QML 因此一开始就能拿到非零的 rollWidthPx/rollHeightPx 并显示空卷帘。
    m_geometry = computeRollGeometry(57, 72, 8.0, kDisplayMaxWidthPx, kDisplayPxPerSecond,
                                    kRollPxPerSemitone, kRollKeyboardWidthPx, kRollAxisHeightPx);
    m_progressTimer.setInterval(100);
    connect(&m_progressTimer, &QTimer::timeout, this, &FileAnalysisController::pollProgress);
    // 线程启动即执行分析（对象已在 analyze() 里移到该线程）
    connect(&m_worker, &QThread::started, this, &FileAnalysisController::runAnalysis);
}

FileAnalysisController::~FileAnalysisController() {
    m_cancelRequested.store(true);
    if (m_worker.isRunning()) {
        m_worker.quit();
        m_worker.wait(5000);
    }
}

QString FileAnalysisController::loadedPath() const {
    return m_loadedPath;
}

QVariantMap FileAnalysisController::preview() const {
    const CurvePreview preview = buildCurvePreview(m_model.frames());
    QVariantMap map;
    map.insert(QStringLiteral("points"), preview.points);
    map.insert(QStringLiteral("freqMin"), preview.freqMin);
    map.insert(QStringLiteral("freqMax"), preview.freqMax);
    map.insert(QStringLiteral("totalFrames"), preview.totalFrames);
    map.insert(QStringLiteral("sampledFrames"), preview.sampledFrames);
    return map;
}

void FileAnalysisController::setState(const QString& text, const QString& error) {
    m_stateText = text;
    m_errorText = error;
    emit stateChanged();
}

QString FileAnalysisController::chooseAndAnalyze() {
    // 原生对话框：QML 的 FileDialog 在 Windows 上返回的路径不可靠（用户实测"无法载入"）
    const QString path = QFileDialog::getOpenFileName(
        nullptr, QStringLiteral("选择要分析的音频（主流格式均可）"), m_lastDir,
        pitch::audioFileFilter());
    if (path.isEmpty()) {
        return {};
    }
    m_lastDir = QFileInfo(path).absolutePath();
    analyze(path);
    return path;
}

QString FileAnalysisController::exportRollImage() {
    if (!hasResult()) {
        setState(m_stateText, QStringLiteral("还没有可导出的分析结果"));
        return {};
    }
    const QString suggested =
        m_lastDir.isEmpty() ? QStringLiteral("piano-roll.png")
                            : m_lastDir + QStringLiteral("/piano-roll.png");
    // 原生保存对话框：与"选择文件"同一条已验证可靠的路（QML FileDialog 在 Windows 上不可靠）
    const QString path = QFileDialog::getSaveFileName(
        nullptr, QStringLiteral("导出钢琴卷帘长图（PNG）"), suggested,
        QStringLiteral("PNG 图片 (*.png)"));
    if (path.isEmpty()) {
        return {};
    }
    // 导出用**同一份**渲染实现（见 renderRollImage），只是把宽度上限放大：
    // 于是导出的长图与屏幕上看到的完全一致，只是分辨率更高（长录音才会有差别）。
    const QImage image = renderRollImage(kExportMaxWidthPx, kDisplayPxPerSecond);
    QString error;
    if (!saveRollPng(image, path, &error)) {
        setState(m_stateText, error);
        return {};
    }
    m_lastDir = QFileInfo(path).absolutePath();
    setState(QStringLiteral("已导出长图：%1（%2 × %3 像素）")
                 .arg(QFileInfo(path).fileName())
                 .arg(image.width())
                 .arg(image.height()),
             QString());
    return path;
}

void FileAnalysisController::clearResult() {
    if (m_analyzing.load()) {
        cancel();
        return;
    }
    m_model.clear();
    m_summary.clear();
    m_fileName.clear();
    m_loadedPath.clear();
    m_decodeRoute.clear();
    m_geometry = RollGeometry{};
    ++m_rollRevision;
    setState(QStringLiteral("未载入文件"), QString());
    emit resultChanged();
}

void FileAnalysisController::analyze(const QString& path) {
    if (m_worker.isRunning()) {
        setState(m_stateText, QStringLiteral("已有分析在进行中，请先等待完成或取消"));
        return;
    }

    const QString local = path.startsWith(QStringLiteral("file://")) ? path.mid(7) : path;
    const QFileInfo info(local);
    if (!info.exists()) {
        setState(QStringLiteral("载入失败"), QStringLiteral("文件不存在：%1").arg(local));
        emit finished(false);
        return;
    }

    m_loadedPath = local;
    m_fileName = info.fileName();
    m_model.clear();
    m_summary.clear();
    m_pendingFrames.clear();
    m_pendingOk = false;
    m_sampleRate = 0;
    m_cancelRequested.store(false);
    m_progress.store(0.0);
    m_analyzing.store(true);

    // ---- 解码放在主线程完成，再把样点交给工作线程做重活 ----
    // 为什么分开：解码（尤其 mp3/m4a）走 Qt Multimedia 的**异步**解码器，需要事件循环；
    // 而"分析"是纯计算且耗时长（88 键素材要几十秒），必须留在工作线程里，否则界面会假死。
    setState(QStringLiteral("解码中…"), QString());
    emit resultChanged();
    emit progressChanged();

    const DecodedAudio decoded = decodeAudioFile(local);
    if (!decoded.ok) {
        m_analyzing.store(false);
        setState(QStringLiteral("载入失败"), decoded.error);
        emit finished(false);
        return;
    }
    m_pendingSamples = decoded.samples;
    m_pendingSampleRate = decoded.sampleRate;
    m_decodeRoute = decoded.route;
    m_sampleRate = static_cast<int>(std::lround(decoded.sampleRate));

    setState(QStringLiteral("分析中…"), QString());
    emit resultChanged();

    m_progressTimer.start();
    // 顺序不能反：先移线程再 start；反过来会把工作线程的事件循环一起搬走
    this->moveToThread(&m_worker);
    m_worker.start();
}

void FileAnalysisController::cancel() {
    m_cancelRequested.store(true);
    setState(QStringLiteral("正在取消…"), m_errorText);
}

void FileAnalysisController::pollProgress() {
    emit progressChanged();
}

void FileAnalysisController::runAnalysis() {
    // ===== 以下在工作线程执行：只碰数据 =====
    // 样点已在主线程解码完成（见 analyze() 的说明），这里只做纯计算的分析。
    if (m_pendingSamples.empty() || m_pendingSampleRate <= 0.0) {
        m_errorText = QStringLiteral("没有可分析的样点（解码结果为空）");
        m_pendingOk = false;
        this->moveToThread(QCoreApplication::instance()->thread());
        m_worker.quit();
        QMetaObject::invokeMethod(this, "onAnalysisCompleted", Qt::QueuedConnection);
        return;
    }

    m_sampleRate = static_cast<int>(std::lround(m_pendingSampleRate));

    // 帧进 441 样点 = 10 ms（上游文件分析口径：精度优先，允许非实时）
    constexpr std::size_t kHop = 441;
    const EngineConfig cfg;
    const Analysis analysis = AnalysisRunner::analyze(
        std::span<const float>(m_pendingSamples.data(), m_pendingSamples.size()),
        m_pendingSampleRate, kHop, cfg,
        [this](double p) { m_progress.store(std::min(1.0, std::max(0.0, p))); },
        nullptr);

    m_pendingFrames = analysis.frames;
    m_pendingSummary = analysis.summary;
    m_pendingOk = !analysis.frames.empty();

    if (m_pendingOk) {
        // 统计摘要：音名众数 + 中位偏差 + 不同音名数（混合音名是"八度抖动"的度量）
        std::map<std::string, int> noteCount;
        for (const Frame& f : analysis.frames) {
            noteCount[NoteConverter::format(f.noteIndex, f.octave)]++;
        }
        std::string modeNote = "-";
        int modeCount = 0;
        for (const auto& kv : noteCount) {
            if (kv.second > modeCount) {
                modeCount = kv.second;
                modeNote = kv.first;
            }
        }
        QString summary;
        summary += QStringLiteral("文件：%1\n").arg(m_fileName);
        summary += QStringLiteral("采样率：%1 Hz（%2）\n").arg(m_sampleRate).arg(m_decodeRoute);
        summary += QStringLiteral("有效帧：%1（帧进 10 ms）\n").arg(analysis.frames.size());
        summary += QStringLiteral("众数音名：%1（%2 帧，占 %3%）\n")
                       .arg(QString::fromStdString(modeNote))
                       .arg(modeCount)
                       .arg(100.0 * modeCount / static_cast<double>(analysis.frames.size()), 0, 'f', 1);
        summary += QStringLiteral("中位频率：%1 Hz（偏差 %2 音分）\n")
                       .arg(analysis.summary.medianFreq, 0, 'f', 2)
                       .arg(analysis.summary.medianCents, 0, 'f', 1);
        summary += QStringLiteral("不同音名：%1 个%2\n")
                       .arg(noteCount.size())
                       .arg(noteCount.size() > 1
                                ? QStringLiteral("（含混合音名：换音或八度抖动，看曲线确认）")
                                : QString());
        summary += QStringLiteral("八度校正折回：%1 帧\n").arg(analysis.summary.octaveFixedCount);
        summary += QStringLiteral("峰值 RMS：%1；静音门槛：%2")
                       .arg(analysis.summary.peakRms, 0, 'f', 4)
                       .arg(analysis.summary.rmsFloor, 0, 'f', 4);
        m_summary = summary;
    } else {
        m_summary = QStringLiteral(
            "未检测到有效音高。可能原因：文件短于约 371 ms（算法需要至少一个最大窗）、"
            "纯静音、或音高超出 27–4300 Hz。");
    }

    this->moveToThread(QCoreApplication::instance()->thread());
    m_worker.quit();
    QMetaObject::invokeMethod(this, "onAnalysisCompleted", Qt::QueuedConnection);
}

RollPalette FileAnalysisController::rollPalette() const {
    // 主题为空（无界面自检）时给一套保守配色：不允许"没有主题就画不出图"
    if (m_theme == nullptr) {
        RollPalette fallback;
        fallback.background = QColor(0x0b, 0x0f, 0x14);
        fallback.rowAlt = QColor(0x1a, 0x22, 0x2c);
        fallback.gridLine = QColor(0x26, 0x30, 0x3c);
        fallback.gridStrong = QColor(0x93, 0xa2, 0xb4);
        fallback.axisText = QColor(0x93, 0xa2, 0xb4);
        fallback.axisLine = QColor(0x26, 0x30, 0x3c);
        fallback.curve = QColor(0x2e, 0xd3, 0xb7);
        fallback.curveGlow = QColor(0x17, 0x56, 0x4c);
        fallback.surface = QColor(0x12, 0x18, 0x21);
        fallback.text = QColor(0xf2, 0xf6, 0xfa);
        fallback.keyLabelOnDark = QColor(0xee, 0xf2, 0xf6);
        return fallback;
    }
    return rollPaletteFromTheme(*m_theme);
}

QImage FileAnalysisController::renderRollImage(int maxWidthPx, double preferredPxPerSecond) const {
    const std::vector<Frame>& frames = m_model.frames();

    // 没有结果时**也画一张**：只有键盘、网格与时间轴（用户要求"卷帘区域常显"）。
    // 默认音域取 A3–C5（单音练习最常见的范围），默认时长 8 秒，时间刻度照样画出来。
    if (frames.empty()) {
        const RollGeometry geom = computeRollGeometry(57, 72, 8.0, maxWidthPx, preferredPxPerSecond,
                                                     kRollPxPerSemitone, kRollKeyboardWidthPx,
                                                     kRollAxisHeightPx);
        return renderPianoRoll({}, geom, rollPalette(), kDefaultA4);
    }

    int lowest = 0;
    int highest = 0;
    midiRangeOf(frames, kDefaultA4, &lowest, &highest);
    const RollGeometry geom = computeRollGeometry(lowest, highest, durationOf(frames), maxWidthPx,
                                                 preferredPxPerSecond, kRollPxPerSemitone,
                                                 kRollKeyboardWidthPx, kRollAxisHeightPx);
    return renderPianoRoll(frames, geom, rollPalette(), kDefaultA4);
}

void FileAnalysisController::onAnalysisCompleted() {
    // ===== 以下在主线程执行：可以安全地更新模型与发信号 =====
    m_progressTimer.stop();
    m_analyzing.store(false);
    m_progress.store(1.0);

    if (m_pendingOk) {
        m_model.setFrames(std::move(m_pendingFrames));
        m_pendingFrames.clear();
        // 卷帘几何按结果算一次，并自增版本号让 QML 的 Image 换 URL（重新取图）
        int lowest = 0;
        int highest = 0;
        midiRangeOf(m_model.frames(), kDefaultA4, &lowest, &highest);
        m_geometry = computeRollGeometry(lowest, highest, durationOf(m_model.frames()),
                                        kDisplayMaxWidthPx, kDisplayPxPerSecond, kRollPxPerSemitone,
                                        kRollKeyboardWidthPx, kRollAxisHeightPx);
        ++m_rollRevision;
    }
    // 样点已用完就释放（一段几分钟的录音是几十 MB，留着没意义）
    m_pendingSamples.clear();
    m_pendingSamples.shrink_to_fit();
    m_pendingSampleRate = 0.0;
    setState(m_pendingOk ? QStringLiteral("分析完成") : QStringLiteral("无有效音高"),
             m_pendingOk ? QString() : m_errorText);
    emit progressChanged();
    emit resultChanged();
    emit finished(m_pendingOk);
}

} // namespace pitch

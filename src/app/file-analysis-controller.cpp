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
#include "note-converter.h"
#include "wav-reader.h"

#include <QCoreApplication>
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

QString FrameTableModel::toCsv() const {
    if (m_frames.empty()) {
        return {};
    }
    QString out;
    // 列沿用上游导出口径（timestamp/freq/note/cents/confidence），另加 rms 与 octaveFixed 便于排查
    out += QStringLiteral("timestamp,freq,note,cents,confidence,rms,octaveFixed\n");
    for (const Frame& f : m_frames) {
        out += QStringLiteral("%1,%2,%3,%4,%5,%6,%7\n")
                   .arg(f.timeSec, 0, 'f', 4)
                   .arg(f.freq, 0, 'f', 4)
                   .arg(QString::fromLatin1(NoteConverter::format(f.noteIndex, f.octave)))
                   .arg(f.cents, 0, 'f', 2)
                   .arg(f.confidence, 0, 'f', 4)
                   .arg(f.rms, 0, 'f', 6)
                   .arg(f.octaveFixed ? 1 : 0);
    }
    return out;
}

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

FileAnalysisController::FileAnalysisController(QObject* parent) : QObject(parent) {
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
        nullptr, QStringLiteral("选择要分析的音频（未压缩 PCM WAV）"), m_lastDir,
        QStringLiteral("WAV 音频 (*.wav);;所有文件 (*)"));
    if (path.isEmpty()) {
        return {};
    }
    m_lastDir = QFileInfo(path).absolutePath();
    analyze(path);
    return path;
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

    setState(QStringLiteral("分析中…"), QString());
    emit resultChanged();
    emit progressChanged();

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
    // 路径统一按 **UTF-8** 传入：不要用 QString::toStdString()（那是本地代码页）。
    // Windows 上 readWavMono 内部转 UTF-16 再打开，中文路径才不会失败（坑 A29，实测踩过）；
    // Android/Linux 的文件名本就是 UTF-8 字节。
    const WavData wav = readWavMono(m_loadedPath.toUtf8().toStdString());

    if (!wav.ok) {
        m_errorText = QStringLiteral("无法读取音频：%1（只支持未压缩 PCM WAV）")
                          .arg(QString::fromStdString(wav.error));
        m_pendingOk = false;
        // 回到主线程再收尾
        this->moveToThread(QCoreApplication::instance()->thread());
        m_worker.quit();
        QMetaObject::invokeMethod(this, "onAnalysisCompleted", Qt::QueuedConnection);
        return;
    }

    m_sampleRate = static_cast<int>(std::lround(wav.sampleRate));

    // 帧进 441 样点 = 10 ms（上游文件分析口径：精度优先，允许非实时）
    constexpr std::size_t kHop = 441;
    const EngineConfig cfg;
    const Analysis analysis = AnalysisRunner::analyze(
        std::span<const float>(wav.samples.data(), wav.samples.size()),
        wav.sampleRate, kHop, cfg,
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
        summary += QStringLiteral("采样率：%1 Hz\n").arg(m_sampleRate);
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

void FileAnalysisController::onAnalysisCompleted() {
    // ===== 以下在主线程执行：可以安全地更新模型与发信号 =====
    m_progressTimer.stop();
    m_analyzing.store(false);
    m_progress.store(1.0);

    if (m_pendingOk) {
        m_model.setFrames(std::move(m_pendingFrames));
        m_pendingFrames.clear();
    }
    setState(m_pendingOk ? QStringLiteral("分析完成") : QStringLiteral("无有效音高"),
             m_pendingOk ? QString() : m_errorText);
    emit progressChanged();
    emit resultChanged();
    emit finished(m_pendingOk);
}

bool FileAnalysisController::exportCsv(const QString& path) {
    const QString csv = m_model.toCsv();
    if (csv.isEmpty()) {
        setState(m_stateText, QStringLiteral("没有可导出的数据（请先分析一个音频文件）"));
        return false;
    }
    const QString local = path.startsWith(QStringLiteral("file://")) ? path.mid(7) : path;
    QFile file(local);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setState(m_stateText, QStringLiteral("无法写入文件：%1").arg(file.errorString()));
        return false;
    }
    QTextStream out(&file);
    out << csv;
    file.close();
    setState(QStringLiteral("已导出 CSV：%1").arg(local), QString());
    return true;
}

} // namespace pitch

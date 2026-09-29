// 文件分析控制器 + 逐帧表格模型
//
// 职责：载入音频 → 在**独立线程**里跑整段分析 → 把逐帧结果暴露成表格模型 → 导出 CSV。
// 分析必须离开主线程：上游实测 88 键（每个 9 s）耗时数十秒，若在界面线程跑会整窗卡死。
//
// 为什么用 QThread + moveToThread 而不是 QtConcurrent：
//   本项目的 Qt 组件清单里没有 Concurrent 模块（实测），而 QThread 属 Qt6::Core，零额外依赖。
//   用法是"把本对象临时移到工作线程跑完再移回"，避免自定义 worker 类与信号转发样板。
//
// 线程约束（必须遵守，否则是未定义行为）：
//   · analyze() 只在主线程调用；它负责移线程、启动、并在结果回来后移回
//   · runAnalysis() 只会在工作线程执行；它**不碰**任何界面对象
//   · 进度用一个原子量从工作线程传出，主线程用定时器轮询后发信号

#pragma once

#include "pitch-types.h"

#include <QAbstractListModel>
#include <QObject>
#include <QString>
#include <QThread>
#include <QTimer>
#include <QVariantList>

#include <atomic>
#include <vector>

namespace pitch {

/// 曲线预览数据：给界面画整段曲线用的抽样点列 + 自适应纵轴范围。
///
/// 为什么要在 C++ 侧做：QML 里按 role 号逐行取几万帧会卡界面，
/// 而按 role 号（Qt::UserRole+1 之类的裸数字）取数据本身就是脆弱写法。
/// 抽样与量程一次算完，界面只负责画。
struct CurvePreview {
    QVariantList points;   ///< [{t, freq}, ...]
    double freqMin = 0.0;  ///< 纵轴下限（已留 15% 余量）
    double freqMax = 0.0;  ///< 纵轴上限
    int totalFrames = 0;   ///< 原始帧数（界面可显示"已抽样 N/M"）
    int sampledFrames = 0; ///< 抽样后点数
};

/// 逐帧结果表格模型（供 QML ListView 使用）。
class FrameTableModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        TimeRole = Qt::UserRole + 1,
        FreqRole,
        NoteRole,
        CentsRole,
        ConfidenceRole,
        RmsRole,
        OctaveFixedRole,
    };

    explicit FrameTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// 用新结果整体替换（触发 model reset）。
    void setFrames(std::vector<Frame> frames);

    /// 清空。
    void clear();

    /// 当前行数。
    int count() const { return static_cast<int>(m_frames.size()); }

    /// 只读访问底层帧序列（供摘要、曲线抽样等使用；不经过 model role）。
    const std::vector<Frame>& frames() const { return m_frames; }

    /// 导出为 CSV 文本（含表头）。返回空串表示无数据。
    QString toCsv() const;

private:
    std::vector<Frame> m_frames;
};

/// 从帧序列生成曲线预览（抽样 + 量程）。与模型解耦，便于单测与复用。
CurvePreview buildCurvePreview(const std::vector<Frame>& frames, int maxPoints = 3000);

/// 文件分析控制器。
class FileAnalysisController : public QObject {
    Q_OBJECT

    /// 当前状态文案。
    Q_PROPERTY(QString stateText READ stateText NOTIFY stateChanged)
    /// 是否正在分析。
    Q_PROPERTY(bool analyzing READ analyzing NOTIFY stateChanged)
    /// 进度 0..1。
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    /// 逐帧结果（表格模型）。
    Q_PROPERTY(QObject* frames READ frames CONSTANT)
    /// 结果摘要（多行文本，供界面显示统计）。
    Q_PROPERTY(QString summary READ summary NOTIFY resultChanged)
    /// 曲线预览（抽样点列 + 量程），供界面直接画整段曲线。
    Q_PROPERTY(QVariantMap preview READ preview NOTIFY resultChanged)
    /// 已载入的文件名。
    Q_PROPERTY(QString fileName READ fileName NOTIFY resultChanged)
    /// 载入文件的采样率。
    Q_PROPERTY(int sampleRate READ sampleRate NOTIFY resultChanged)
    /// 有效帧数。
    Q_PROPERTY(int frameCount READ frameCount NOTIFY resultChanged)
    /// 错误/提示信息。
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged)

public:
    explicit FileAnalysisController(QObject* parent = nullptr);
    ~FileAnalysisController() override;

    QString stateText() const { return m_stateText; }
    bool analyzing() const { return m_analyzing.load(); }
    double progress() const { return m_progress.load(); }
    QObject* frames() { return &m_model; }
    QString summary() const { return m_summary; }
    QString fileName() const { return m_fileName; }
    int sampleRate() const { return m_sampleRate; }
    int frameCount() const { return m_model.count(); }
    QString errorText() const { return m_errorText; }
    /// 曲线预览：一条 QVariantMap，键为 points / freqMin / freqMax / totalFrames / sampledFrames。
    QVariantMap preview() const;

public slots:
    /// 载入音频文件（只读 WAV）并立即开始分析。
    void analyze(const QString& path);

    /// 取消当前分析（下一帧循环检查后退出）。
    void cancel();

    /// 把逐帧结果写到 CSV 文件。
    /// @return 成功与否；失败原因见 errorText
    bool exportCsv(const QString& path);

    /// 是否已载入可用于实时回放的文件（供"用文件验证实时链路"用）。
    Q_INVOKABLE bool hasPlayableFile() const { return !m_loadedPath.isEmpty(); }
    /// 已载入文件的完整路径（供实时回放使用）。
    Q_INVOKABLE QString loadedPath() const;

    /// 弹出原生文件选择框选一段音频并开始分析（用 C++ 侧 QFileDialog，
    /// 因为 QML 的 FileDialog 在 Windows 上返回的路径不可靠）。返回选中的路径。
    Q_INVOKABLE QString chooseAndAnalyze();

signals:
    void stateChanged();
    void progressChanged();
    void resultChanged();
    /// 分析结束（无论成功/取消/失败）。**只在主线程发出**（见实现里的线程模型说明）。
    void finished(bool ok);

private slots:
    /// 在工作线程执行；不得触碰界面对象。
    void runAnalysis();
    /// 分析收尾，在主线程执行（由 runAnalysis 通过 QueuedConnection 请求）。
    void onAnalysisCompleted();
    /// 主线程轮询进度。
    void pollProgress();

private:
    void setState(const QString& text, const QString& error);
    void moveToWorkerAndStart();

    FrameTableModel m_model;
    QString m_stateText = QStringLiteral("未载入文件");
    QString m_errorText;
    QString m_summary;
    QString m_fileName;
    QString m_loadedPath;
    QString m_lastDir;                 ///< 上次选文件的目录
    int m_sampleRate = 0;

    QThread m_worker;
    std::atomic<bool> m_analyzing{false};
    std::atomic<bool> m_cancelRequested{false};
    std::atomic<double> m_progress{0.0};
    QTimer m_progressTimer;

    // 工作线程与主线程之间传递的数据（只在分析期间使用，访问顺序由线程切换保证）
    std::vector<Frame> m_pendingFrames;
    AnalysisResult m_pendingSummary{};
    bool m_pendingOk = false;
};

} // namespace pitch

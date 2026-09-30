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
#include "piano-roll-renderer.h"

#include <QAbstractListModel>
#include <QImage>
#include <QObject>
#include <QString>
#include <QThread>
#include <QTimer>
#include <QVariantList>

#include <atomic>
#include <vector>

namespace pitch {

class ThemeProvider;   ///< 主题令牌（定义在 theme.h；这里只用指针）

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
    /// 实际使用的解码路径（"PCM WAV 直读" / "解码器"）：排查"为什么音质/时长不对"的第一条线索
    Q_PROPERTY(QString decodeRoute READ decodeRoute NOTIFY resultChanged)

    // ---------------- 钢琴卷帘（渲染在 C++，界面与导出共用同一份实现）----------------
    /// 卷帘图像版本号：结果变化时自增，QML 的 Image 据此换 URL（QML 侧不做任何绘制）
    Q_PROPERTY(int rollRevision READ rollRevision NOTIFY resultChanged)
    /// 卷帘整图尺寸（QML 用它设置 Image 的宽高，从而获得 1:1 显示 + 横向滚动）
    Q_PROPERTY(int rollWidthPx READ rollWidthPx NOTIFY resultChanged)
    Q_PROPERTY(int rollHeightPx READ rollHeightPx NOTIFY resultChanged)
    /// 时长（秒）与覆盖的 MIDI 音域（界面显示用）
    Q_PROPERTY(double durationSec READ durationSec NOTIFY resultChanged)
    Q_PROPERTY(int lowestMidi READ lowestMidi NOTIFY resultChanged)
    Q_PROPERTY(int highestMidi READ highestMidi NOTIFY resultChanged)
    /// 是否已有可画的结果
    Q_PROPERTY(bool hasResult READ hasResult NOTIFY resultChanged)

    // 卷帘的绘制参数（界面与导出共用；导出只是把宽度上限放大）
    //
    // 为什么显示与导出要分开两个宽度上限：屏幕上那张图最终会成为一张 GPU 纹理，
    // 而不少手机 GPU 的纹理上限是 4096 像素——超了会直接不显示（比"糊"更糟）。
    // 导出成 PNG 不受该限制，所以长图导出可以用更大的宽度换更高的时间分辨率。
public:
    static constexpr int kDisplayMaxWidthPx = 3800;
    static constexpr double kDisplayPxPerSecond = 130.0;
    static constexpr int kExportMaxWidthPx = 12000;
    static constexpr int kRollPxPerSemitone = 15;
    static constexpr int kRollKeyboardWidthPx = 52;
    static constexpr int kRollAxisHeightPx = 22;

private:
    // （无私有成员声明节——属性与常量见上；成员变量在文件末尾）

public:
    /// @param theme 主题令牌来源（卷帘配色从这里折算，避免出现"第二处颜色定义"）；
    ///              可为空——无界面自检（`--selftest`）会这样构造，它不渲染卷帘
    explicit FileAnalysisController(ThemeProvider* theme = nullptr, QObject* parent = nullptr);
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
    QString decodeRoute() const { return m_decodeRoute; }
    /// 曲线预览：一条 QVariantMap，键为 points / freqMin / freqMax / totalFrames / sampledFrames。
    QVariantMap preview() const;

    int rollRevision() const { return m_rollRevision; }
    int rollWidthPx() const { return m_geometry.widthPx; }
    int rollHeightPx() const { return m_geometry.heightPx; }
    double durationSec() const { return m_geometry.durationSec; }
    int lowestMidi() const { return m_geometry.lowestMidi; }
    int highestMidi() const { return m_geometry.highestMidi; }
    bool hasResult() const { return m_model.count() > 0; }

    /// 渲染卷帘整图（供 QQuickImageProvider 与导出共用；**同一份几何与配色**）。
    /// @param maxWidthPx 宽度上限：界面显示用小一点（受 GPU 纹理上限约束），导出可以大很多
    ///
    /// **没有分析结果时也会返回一张图**（键盘 + 网格 + 时间轴常显，只是没有曲线）——
    /// 用户要求"卷帘区域应该常显"：空着比画一张空卷帘更让人以为功能坏了。
    QImage renderRollImage(int maxWidthPx, double preferredPxPerSecond) const;

public slots:
    /// 载入音频文件（只读 WAV）并立即开始分析。
    void analyze(const QString& path);

    /// 取消当前分析（下一帧循环检查后退出）。
    void cancel();

    /// 弹出原生保存对话框，把钢琴卷帘导出成**一张长图**（PNG）。
    /// 取代原来的"导出逐帧 CSV"（用户 2026-09-30：CSV 换成整段长图，看图比看数字有用）。
    /// @return 实际保存的路径；取消或失败返回空串（失败原因见 errorText）
    Q_INVOKABLE QString exportRollImage();

    /// 清空当前结果（换文件/重新录音前调用）。
    Q_INVOKABLE void clearResult();

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
    /// 卷帘配色（从主题令牌折算；主题为空时用一套保守默认值）
    RollPalette rollPalette() const;

    FrameTableModel m_model;
    QString m_stateText = QStringLiteral("未载入文件");
    QString m_errorText;
    QString m_summary;
    QString m_fileName;
    QString m_loadedPath;
    QString m_lastDir;                 ///< 上次选文件的目录
    QString m_decodeRoute;             ///< 实际走通的解码路径（显示给用户）
    int m_sampleRate = 0;

    // 卷帘：几何在分析完成后按帧数据算一次；图像版本号用于让 QML 的 Image 换 URL
    ThemeProvider* m_theme = nullptr;
    RollGeometry m_geometry{};
    int m_rollRevision = 0;

    // 解码在主线程完成后把样点交给工作线程（见 analyze() 的说明：解码快、分析慢，分开更稳）
    std::vector<float> m_pendingSamples;
    double m_pendingSampleRate = 0.0;

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

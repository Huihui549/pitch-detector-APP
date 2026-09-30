// 实时会话控制器
//
// 职责（architecture.md 3.3）：开/停采集、驱动 YIN、维护滚动窗口、去抖与保持、
// 音域统计、向界面暴露属性与信号。**不含音高算法**——算法只在 src/core（分层铁律）。
//
// 设计要点与依据：
//   1. 滚动窗口用环形缓冲，**不假设每次回调的样点数**（坑 A4：真实设备回调粒度不固定）
//   2. 读数保持 700 ms 防闪断（上游实时页参数）
//   3. 静音/低置信度发"—"而不是残留上一次的值（上游 spec 边界要求，pitfalls #2）
//   4. 音域极值只在置信度达标且连续命中 ≥3 帧时更新（pitfalls #4）
//   5. 实时链路用固定窗 4096（ADR-0004），与文件分析的级联窗长刻意不同

#pragma once

#include "i-audio-source.h"
#include "pitch-types.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVector>

#include <deque>
#include <memory>

namespace pitch {

/// 实时音高会话控制器。
class PitchSessionController : public QObject {
    Q_OBJECT

    /// 当前音名（无有效音高时为 "—"）。
    Q_PROPERTY(QString noteName READ noteName NOTIFY readingChanged)
    /// 当前八度（无有效时为 INT_MIN，界面据此决定是否显示）。
    Q_PROPERTY(int octave READ octave NOTIFY readingChanged)
    /// 音分偏差（±50 内；无有效时无意义）。
    Q_PROPERTY(double cents READ cents NOTIFY readingChanged)
    /// 当前频率 Hz（无有效时为 0）。
    Q_PROPERTY(double frequency READ frequency NOTIFY readingChanged)
    /// 置信度 0..1。
    Q_PROPERTY(double confidence READ confidence NOTIFY readingChanged)
    /// 当前帧 RMS。
    Q_PROPERTY(double rms READ rms NOTIFY readingChanged)
    /// 是否正在采集。
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    /// 状态文案（"已停止" / "正在监听" 等）。
    Q_PROPERTY(QString stateText READ stateText NOTIFY stateChanged)
    /// 采集实现描述（设备名/文件路径），显示在调试区。
    Q_PROPERTY(QString sourceDescription READ sourceDescription NOTIFY stateChanged)
    /// 实际采样率。
    Q_PROPERTY(int sampleRate READ sampleRate NOTIFY stateChanged)
    /// 若采集不可用，这里是原因（空串表示可用）。
    Q_PROPERTY(QString unavailableReason READ unavailableReason NOTIFY stateChanged)
    /// 采集统计（轮询次数/累计字节/最近一次字节/错误）：用于区分
    /// "没启动"、"启动了但收不到数据"、"有数据但读数不对"三种情况。
    Q_PROPERTY(QString captureStats READ captureStats NOTIFY readingChanged)
    /// 采集期间的**原始 RMS 峰值**（未过静音门槛）。
    /// 它能回答"麦克风到底有没有收到声音"——与"检测到有效音高"是两件事：
    /// 前者为 0 说明采集链路有问题，前者正常而后者为空说明是信号/算法层面的问题。
    Q_PROPERTY(double peakRms READ peakRms NOTIFY stateChanged)
    /// 收到音频回调的次数（诊断：0 表示采集层没把数据送上来）。
    Q_PROPERTY(int callbackCount READ callbackCount NOTIFY readingChanged)
    /// 提示信息（格式不一致、削顶等），空串表示无提示。
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    /// 本次会话最高音（音名+八度，未测到为 "—"）。
    Q_PROPERTY(QString highestNote READ highestNote NOTIFY rangeChanged)
    /// 本次会话最低音。
    Q_PROPERTY(QString lowestNote READ lowestNote NOTIFY rangeChanged)
    /// 音域跨度（半音数，未测到为 0）。
    Q_PROPERTY(int rangeSemitones READ rangeSemitones NOTIFY rangeChanged)
    /// 累计有效时长（秒）。
    Q_PROPERTY(double validSeconds READ validSeconds NOTIFY rangeChanged)
    /// 近 5 秒音高曲线点（x = 相对秒，y = 频率），供界面直接画。
    Q_PROPERTY(QVariantList curve READ curve NOTIFY curveChanged)
    /// 调试：归一化差分曲线（谷值曲线）点，y = cmnd 值。
    Q_PROPERTY(QVariantList yinCurve READ yinCurve NOTIFY readingChanged)
    /// 当前生效的判定阈值（供调试界面画参考线）。
    /// **由 C++ 提供而不是在 QML 里写死**：阈值是算法参数，界面只呈现；
    /// 若在界面里复制一份，就重新走上游 pitfalls #22"同一参数多处维护"的老路。
    Q_PROPERTY(double yinThreshold READ yinThreshold CONSTANT)
    /// 实时链路所用窗长的可读描述（供界面显示；窗长取值同样属算法参数，不在 QML 里重复）。
    Q_PROPERTY(QString frameDescription READ frameDescription CONSTANT)
    /// 工作音域的可读描述（如 "27–4300 Hz"）。音域是算法参数，界面只显示。
    Q_PROPERTY(QString rangeDescription READ rangeDescription CONSTANT)
    /// 界面门槛与保持时长由 C++ 暴露：QML 里不得自己写阈值（改一处即可全局生效）
    Q_PROPERTY(double displayMinConfidence READ displayMinConfidence CONSTANT)
    Q_PROPERTY(int holdMs READ holdMs CONSTANT)
    /// 当前帧实际采用的静音门槛（绝对门槛与相对峰值门槛取大者）：界面画刻线用，**不得自己写 0.008**
    Q_PROPERTY(double rmsFloor READ rmsFloor NOTIFY readingChanged)

public:
    explicit PitchSessionController(QObject* parent = nullptr);
    ~PitchSessionController() override;

    QString noteName() const { return m_noteName; }
    int octave() const { return m_octave; }
    double cents() const { return m_cents; }
    double frequency() const { return m_frequency; }
    double confidence() const { return m_confidence; }
    double rms() const { return m_rms; }
    bool running() const { return m_running; }
    QString stateText() const { return m_stateText; }
    QString sourceDescription() const { return m_sourceDescription; }
    int sampleRate() const { return m_sampleRate; }
    QString unavailableReason() const { return m_unavailableReason; }
    QString captureStats() const;
    double peakRms() const { return m_peakRms; }
    int callbackCount() const { return m_callbackCount; }
    QString notice() const { return m_notice; }
    QString highestNote() const { return m_highestNote; }
    QString lowestNote() const { return m_lowestNote; }
    int rangeSemitones() const { return m_rangeSemitones; }
    double validSeconds() const { return m_validSeconds; }
    QVariantList curve() const { return m_curve; }
    QVariantList yinCurve() const { return m_yinCurve; }
    double yinThreshold() const { return kYinThreshold; }
    double displayMinConfidence() const { return kDisplayMinConfidence; }
    int holdMs() const { return kHoldMs; }
    double rmsFloor() const { return m_rmsFloor; }
    QString frameDescription() const;
    QString rangeDescription() const;

public slots:
    /// 开始采集（使用真实麦克风）。模块缺失或无设备时通过 unavailableReason 报告。
    void startMicrophone();

    /// 开始"文件回放"采集：把 WAV 当实时流喂入（无麦克风环境下验证实时链路）。
    void startFilePlayback(const QString& wavPath);

    /// 测试用：直接注入一段单声道样点（复用麦克风的入环缓冲与检测逻辑，但**不节流**）。
    /// 存在的理由：真实麦克风受环境影响，"没出数"时无法区分是采集问题还是算法问题；
    /// 注入已知信号可以先把算法链条验证干净（对应上游"实时链路从未验证"的空白）。
    /// 注入是瞬间完成的，故不能用采集那套按时间节流，否则只有第一批数据会被处理。
    void injectSamples(const QVector<float>& mono, int sampleRate);

    /// 弹出原生文件选择框选一段音频，并**立即开始实时回放**。
    /// 用 C++ 侧 QFileDialog 而不是 QML 的 FileDialog：后者在 Windows 上返回的路径不可靠
    /// （实测用户点选后报"无法载入音频文件"）。返回选中的路径，取消则返回空串。
    Q_INVOKABLE QString chooseAudioFileAndPlay();

    /// 弹出原生文件选择框，只返回路径（供"文件分析"页使用，不启动回放）。
    Q_INVOKABLE QString chooseAudioFile();

    /// 停止采集。
    void stop();

    /// 清零统计（音域、曲线、累计时长）。换乐器/换测法前调用。
    void resetStatistics();

signals:
    void readingChanged();
    void stateChanged();
    void noticeChanged();
    void rangeChanged();
    void curveChanged();
    /// 一次统计清零或新会话开始。
    void statisticsReset();

private slots:
    void onSamplesReady(const QVector<float>& mono, int sampleRate);
    void onAudioError(pitch::AudioErrorKind kind, const QString& message);
    void onFormatMismatch(int requestedRate, int actualRate, int requestedChannels, int actualChannels);
    void onAudioStateChanged(pitch::AudioState state);
    void onHoldTimeout();

private:
    /// 从环形缓冲取最近 frame 个样点（不足则返回空）。
    bool takeWindow(std::size_t frame, std::vector<float>& out) const;

    /// 接管一个采集源：连接它的全部信号并把本控制器置为运行态。
    ///
    /// **所有采集源都必须经此方法接入**——早先在两处各自 new 了源却忘了 connect，
    /// 结果是"采集层明明读到了数据、会话层一帧都收不到"（回调次数恒为 0），
    /// 且界面只显示"正在监听"，极难定位。
    void adoptSource(std::unique_ptr<IAudioSource> source);

    /// 处理一帧：跑算法 → 去抖 → 更新统计与曲线 → 发信号。
    void processFrame();

    void setNotice(const QString& text);
    void updateRangeTracking(const NoteInfo& info, double confidence);

    std::unique_ptr<IAudioSource> m_source;
    std::deque<float> m_ring;          ///< 环形缓冲（用 deque 以便两端删除）
    std::vector<float> m_scratch;      ///< 连续内存副本：算法需要连续 span，而 deque 不保证连续
    std::size_t m_ringLimit = 0;       ///< 缓冲上限（约 16384 样点，够最大窗 + 余量）
    QString m_lastDir;                 ///< 上次选文件的目录（下次对话框从这里开始）
    EngineBuffers m_buffers;
    EngineConfig m_config;

    bool m_running = false;
    QString m_stateText = QStringLiteral("已停止");
    QString m_sourceDescription;
    QString m_unavailableReason;
    QString m_notice;
    int m_sampleRate = 44100;

    // 当前读数
    QString m_noteName = QStringLiteral("—");
    int m_octave = 0;
    double m_cents = 0.0;
    double m_frequency = 0.0;
    double m_confidence = 0.0;
    double m_rms = 0.0;
    double m_rmsFloor = 0.0;           ///< 当前帧的静音门槛（供界面画刻线）
    double m_peakRms = 0.0;            ///< 采集期间原始 RMS 峰值（诊断"麦克风有没有收到声音"）
    int m_callbackCount = 0;           ///< 收到音频回调的次数（诊断采集层是否在送数据）
    QTimer m_holdTimer;                ///< 读数保持（防闪断）
    QVariantList m_yinCurve;

    /// 最近若干帧的**达标**候选读数，用于"取众数"的稳定性过滤（见 kStableWindow）。
    struct RecentReading {
        int noteIndex;
        int octave;
        double cents;
        double freq;
        double confidence;
    };
    std::deque<RecentReading> m_recentReadings;

    // 音高曲线（近 5 秒）
    struct CurvePoint {
        double t;
        double freq;
    };
    std::deque<CurvePoint> m_curvePoints;
    QVariantList m_curve;

    // 音域统计
    QString m_highestNote = QStringLiteral("—");
    QString m_lowestNote = QStringLiteral("—");
    int m_highestMidi = -1;
    int m_lowestMidi = -1;
    int m_rangeSemitones = 0;
    double m_validSeconds = 0.0;
    QElapsedTimer m_clock;
    QElapsedTimer m_framePacer;        ///< 检测节流用：保证出帧率稳定，不随回调频率波动
    qint64 m_lastFrameMs = 0;
    int m_consecutiveHits = 0;

    /// 读数保持时长（ms）。上游实时页 700 ms。
    static constexpr int kHoldMs = 700;
    /// 曲线保留时长（秒）。上游实时页取近 5 秒。
    static constexpr double kCurveSeconds = 5.0;
    /// 音域极值所需的连续命中帧数（pitfalls #4）。
    static constexpr int kRangeMinConsecutive = 3;
    /// 检测节流间隔（ms）：约 25~30 次/秒。
    ///
    /// 为什么需要节流：出帧率原先跟着音频回调频率走，而回调频率受设备缓冲与系统调度影响，
    /// 实测低到约 7 次/秒 → 表现为"识别很不灵敏"（用户实测反馈）。
    /// 固定节奏后：① 界面刷新稳定 ② 低音用的 16384 长窗（单帧算力较高）不会把轮询饿死。
    static constexpr int kFrameIntervalMs = 35;

    /// **显示门槛**：置信度低于此值的帧不许改写读数。
    ///
    /// 为什么需要它（用户真机实测）：唱一个稳定的 C4 时，音名会来回跳到很多别的音上，
    /// 其中夹着 C4 且 C4 的置信度更高——根因就是原来**无条件**写入读数，噪声/辅音帧
    /// （置信度常只有 0.1~0.5）也照样改音名。原先只有"是否进曲线"用 0.75、
    /// 音域极值用 0.85，而**显示**这条路一个门槛都没有。
    /// 0.8 是有意的取舍：宁可短暂保持上一个可靠读数，也不显示低置信度的乱跳值。
    static constexpr double kDisplayMinConfidence = 0.8;

    /// 稳定性窗口：显示的必须是窗口内出现 ≥ kStableMinCount 次的音名。
    /// 单靠置信度门槛仍会跳——人声的换气/辅音帧偶尔也能过 0.8，且常偏一个八度。
    static constexpr int kStableWindow = 5;
    static constexpr int kStableMinCount = 3;
};

} // namespace pitch

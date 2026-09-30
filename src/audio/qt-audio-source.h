// 真实麦克风采集（Qt Multimedia）
//
// **当前环境未安装 Qt Multimedia**，因此本文件由 CMake 条件编译（见 src/audio/CMakeLists.txt）：
// 模块存在才编译，不存在则整体跳过，且 PitchSessionController 会以"模块缺失"为由禁用实时页。
// 这样做的理由：不引入"半死不活"的可选依赖——模块不在时，相关代码根本不参与构建，
// 而不是编译进去再在运行期失败。
//
// 与上游网页版的对应关系（`pitch.html` 的 getUserMedia 路径）：
//   · 上游必须显式请求关闭回声消除/降噪/自动增益（pitfalls #13：浏览器可以静默忽略）
//   · Qt 侧 QAudioSource 直接给出设备原始流，没有这三个开关，也就不存在"以为关了其实没关"的问题
//   · 但**采样格式未必如愿**：设备可能只给 Int16 或 UInt8，故必须按实际格式转换（坑 A3）

#pragma once

#include "i-audio-source.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QIODevice>
#include <QTimer>

namespace pitch {

/// 基于 Qt Multimedia 的麦克风采集。
///
/// **采集方式：定时轮询（pull），不是 readyRead 信号驱动。**
/// 理由（实测踩过）：`QAudioSource::start()` 返回的 `QIODevice` 默认是**拉取模型**，
/// 数据要由调用方主动 `read()`；只连 `readyRead` 会出现"什么都没发生"——
/// 不报错、状态也变成运行中，但一帧数据都收不到，极难定位。
/// 另外 `connect` 必须写在 `start()` **之前**，否则启动瞬间的信号会丢。
class QtAudioSource : public IAudioSource {
    Q_OBJECT

public:
    explicit QtAudioSource(QObject* parent = nullptr);
    ~QtAudioSource() override;

    void start(int sampleRate, int channels) override;
    void stop() override;
    int actualSampleRate() const override { return m_actualFormat.sampleRate(); }
    int actualChannels() const override { return m_actualFormat.channelCount(); }
    QString description() const override;
    bool isAvailable() const override;

    /// 采集统计的可读描述（供调试界面显示：轮询次数、收到样点数、最近一次读取字节数）。
    /// 有它才能区分"没启动"、"启动了但没有数据"、"有数据但读数不对"三种情况。
    QString statsDescription() const;

    /// 诊断：统计已收到的样点里"非零样点"的比例（0..1）。
    /// 用来区分两种情况：① 麦克风通道没通（全是 0）② 通道通但环境安静（有微小噪声）。
    double nonZeroRatio() const;

private slots:
    void pollDevice();

private:
    /// 把设备给的原生缓冲转成单声道 float。
    /// 按**实际格式**分派：设备可能给 Float / Int16 / Int32 / UInt8，不能假定。
    QVector<float> toMonoFloat(const char* data, qsizetype bytes) const;

    /// 按给定格式**真的去打开**设备，成功返回 true。
    /// 实际生效的格式一律取 `QAudioSource::format()`（后端可能改写请求值），失败时填 m_lastError。
    bool openDevice(const QAudioDevice& device, const QAudioFormat& format);

    QAudioSource* m_source = nullptr;
    QIODevice* m_device = nullptr;
    QAudioFormat m_requestedFormat;
    QAudioFormat m_actualFormat;
    bool m_formatAdjusted = false;  ///< 是否退让过格式（供界面区分"提示"与"正常"）

    /// 轮询定时器：间隔按"每次读约 512 样点"估算，过密无意义、过疏会积压
    QTimer m_pollTimer;
    qint64 m_pollCount = 0;        ///< 轮询次数
    qint64 m_bytesReceived = 0;    ///< 累计收到的字节数
    qint64 m_lastBytes = 0;        ///< 最近一次读到的字节数
    qint64 m_samplesSeen = 0;      ///< 累计样点数（单声道转换后）
    qint64 m_samplesNonZero = 0;   ///< 其中非零样点数（诊断通道是否真的在送数据）
    double m_peakChunkRms = 0.0;   ///< 单块 RMS 峰值（诊断"通道通了但电平极低"）
    double m_peakAbs = 0.0;        ///< 单样点幅度峰值
    QString m_lastError;           ///< 最近一次错误（QAudioSource::error）
};

} // namespace pitch

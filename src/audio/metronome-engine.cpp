#include "metronome-engine.h"

#include "audio-packer.h"   // src/core：最终打包（总增益/声道复制/硬夹紧），可被 --metrocheck 直接验证

#include <QIODevice>   // QtCore：拉模式 QIODevice 的基类

#include <algorithm>
#include <cmath>
#include <cstring>

#ifndef PITCH_HAVE_QT_MULTIMEDIA
// audio.pro 会定义它（1/0）。这里给个默认值，避免本文件被别的工程单独编译时"宏未定义"。
#define PITCH_HAVE_QT_MULTIMEDIA 0
#endif

// Qt Multimedia 的头**只在模块存在时**才包含：未安装该模块时这些头文件根本不存在，
// 无条件包含会让"缺少可选依赖"变成"编译不过"（audio.pro 的条件编译就是为此）。
#if PITCH_HAVE_QT_MULTIMEDIA
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QMediaDevices>
#endif

namespace pitch {
namespace {

/// 单块渲染上限（样点）：防止设备一次要得过多导致回调内出现过大的临时量。
constexpr int kMaxFramesPerPull = 16384;

} // namespace

#if PITCH_HAVE_QT_MULTIMEDIA
/// 拉模式的输出设备：Qt 音频线程按需调用 readData()，我们从 core 渲染器取样点。
class MetronomeEngine::RenderDevice : public QIODevice {
public:
    explicit RenderDevice(MetronomeEngine* engine) : m_engine(engine) {}

    bool isSequential() const override { return true; }
    /// 必须报"有数据可读"：QAudioSink 在拉模式下据此决定是否继续来取。
    qint64 bytesAvailable() const override { return 32768 + QIODevice::bytesAvailable(); }

protected:
    qint64 readData(char* data, qint64 maxSize) override {
        return m_engine->pullAudio(data, maxSize);
    }
    qint64 writeData(const char*, qint64) override { return 0; }

private:
    MetronomeEngine* m_engine = nullptr;
};
#else
/// 未安装 Qt Multimedia 时的占位实现：本类不会被实例化（start() 会直接失败并给出原因）。
class MetronomeEngine::RenderDevice {
public:
    explicit RenderDevice(MetronomeEngine*) {}
};
#endif

MetronomeEngine::MetronomeEngine(QObject* parent) : QObject(parent) {
    m_scratch.resize(static_cast<std::size_t>(kMaxFramesPerPull));
    m_packBuffer.resize(static_cast<std::size_t>(kMaxFramesPerPull) * 8);   // 最多 8 字节/帧
}

MetronomeEngine::~MetronomeEngine() {
    stop();
}

bool MetronomeEngine::available() const {
#if PITCH_HAVE_QT_MULTIMEDIA
    return true;
#else
    return false;
#endif
}

QString MetronomeEngine::unavailableReason() const {
#if PITCH_HAVE_QT_MULTIMEDIA
    return {};
#else
    return QStringLiteral("本二进制在构建时未包含 Qt Multimedia，节拍器无法发声"
                          "（用 Qt 安装目录里的 MaintenanceTool 补装后重新 qmake + 构建）");
#endif
}

int MetronomeEngine::bpm() const {
    return m_renderer.bpm();
}

void MetronomeEngine::setBpm(int bpm) {
    m_renderer.setBpm(bpm);
}

void MetronomeEngine::setPattern(const Pattern& p) {
    m_renderer.setPattern(p);
}

void MetronomeEngine::setVoice(int role, const ClickVoice& v) {
    m_renderer.setVoice(role, v);
}

void MetronomeEngine::preview(int role) {
    m_renderer.preview(role);
}

int MetronomeEngine::actualSampleRate() const {
    return m_actualSampleRate;
}

QString MetronomeEngine::description() const {
    return m_description;
}

long long MetronomeEngine::renderedFrames() const {
    return m_frames.load();
}

int MetronomeEngine::callbackCount() const {
    return m_callbacks.load();
}

TickInfo MetronomeEngine::pollTick() const {
    TickInfo info;
    info.counter = m_tickCounter.load();
    info.barIndex = m_tickBar.load();
    info.beatIndex = m_tickBeat.load();
    info.subIndex = m_tickSub.load();
    info.role = m_tickRole.load();
    info.accent = m_tickAccent.load();
    return info;
}

void MetronomeEngine::resetSink() {
#if PITCH_HAVE_QT_MULTIMEDIA
    if (m_sink != nullptr) {
        m_sink->stop();
        m_sink->deleteLater();
        m_sink = nullptr;
    }
    if (m_device != nullptr) {
        m_device->close();
        m_device->deleteLater();
        m_device = nullptr;
    }
#endif
    m_actualSampleRate = 0;
}

void MetronomeEngine::stop() {
    const bool wasRunning = m_running;
    m_running = false;
    resetSink();
    m_description.clear();
    if (wasRunning) {
        emit runningChanged();
    }
}

bool MetronomeEngine::start() {
    if (m_running) {
        return true;
    }
    m_lastError.clear();

    if (!available()) {
        m_lastError = unavailableReason();
        return false;
    }

#if PITCH_HAVE_QT_MULTIMEDIA
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull()) {
        m_lastError = QStringLiteral("没有可用的音频输出设备");
        return false;
    }

    // 首选"单声道 + Float32 + 设备首选采样率"：单声道省一半数据量，Float 免去一次转换。
    // 退让顺序与采集侧一致：设备不支持就退到设备首选格式，而不是直接放弃。
    QAudioFormat wanted;
    const int preferredRate = device.preferredFormat().sampleRate();
    wanted.setSampleRate(preferredRate > 0 ? preferredRate : 48000);
    wanted.setChannelCount(1);
    wanted.setSampleFormat(QAudioFormat::Float);

    QAudioFormat chosen = wanted;
    bool confirmed = device.isFormatSupported(chosen);
    if (!confirmed) {
        chosen = device.preferredFormat();
        // 注意：这里**不再用 isFormatSupported 的结论否决**——该查询在部分后端偏保守
        // （采集侧实测过"判不支持实际能开"，坑 A42）。真正的判据是 start() 之后的状态与错误码。
        confirmed = device.isFormatSupported(chosen);
    }

    const QAudioFormat::SampleFormat fmt = chosen.sampleFormat();
    if (fmt != QAudioFormat::Float && fmt != QAudioFormat::Int16) {
        m_lastError = QStringLiteral("输出设备的样点格式不受支持（格式码 %1）："
                                     "本引擎只写 Float32 与 Int16，请换一个输出设备")
                          .arg(static_cast<int>(fmt));
        return false;
    }

    m_deviceChannels = std::max(1, chosen.channelCount());
    m_deviceSampleFormat = static_cast<int>(fmt);
    m_bytesPerFrame = std::max(1, chosen.bytesPerFrame());
    m_renderer.setSampleRate(static_cast<double>(chosen.sampleRate()));

    m_sink = new QAudioSink(device, chosen, this);
    m_device = new RenderDevice(this);
    m_device->open(QIODevice::ReadOnly);
    m_sink->start(m_device);

    m_actualSampleRate = chosen.sampleRate();
    m_description = QStringLiteral("%1 ｜ 实际格式 %2 Hz / %3 声道 / %4%5")
                        .arg(device.description())
                        .arg(chosen.sampleRate())
                        .arg(m_deviceChannels)
                        .arg(fmt == QAudioFormat::Float ? QStringLiteral("Float32")
                                                        : QStringLiteral("Int16"))
                        .arg(confirmed ? QString() : QStringLiteral("（设备未显式确认，按首选格式尝试）"));

    if (m_sink->error() != QtAudio::NoError) {
        m_lastError = QStringLiteral("音频输出启动失败：错误码 %1（%2）")
                          .arg(static_cast<int>(m_sink->error()))
                          .arg(m_description);
        resetSink();
        return false;
    }

    m_running = true;
    emit runningChanged();
    return true;
#else
    m_lastError = unavailableReason();
    return false;
#endif
}

qint64 MetronomeEngine::pullAudio(char* data, qint64 maxBytes) {
#if !PITCH_HAVE_QT_MULTIMEDIA
    // 没有 Multimedia 时 start() 必定失败，本函数不会被调用；保留一个明确返回值以便编译
    Q_UNUSED(data)
    Q_UNUSED(maxBytes)
    return 0;
#else
    if (data == nullptr || maxBytes <= 0) {
        return 0;
    }
    const int bytesPerFrame = std::max(1, m_bytesPerFrame);
    int frames = static_cast<int>(maxBytes / bytesPerFrame);
    if (frames <= 0) {
        return 0;
    }
    // 设备一次要得过多时截断：渲染量可预期，回调耗时有上界
    frames = std::min(frames, kMaxFramesPerPull);

    // 渲染（core，纯逻辑）。m_scratch 已在构造时预分配，回调中不发生内存分配。
    m_renderer.render(m_scratch.data(), static_cast<std::size_t>(frames));

    const int channels = std::max(1, m_deviceChannels);
    const bool isFloat = (m_deviceSampleFormat == static_cast<int>(QAudioFormat::Float));
    const bool isInt16 = (m_deviceSampleFormat == static_cast<int>(QAudioFormat::Int16));
    if (!isFloat && !isInt16) {
        return 0;   // start() 已拦过；这里只是不让"未定义格式"把缓冲写坏
    }

    // 打包（总增益 + 声道复制 + 硬夹紧）在 core 里实现，`--metrocheck` 会直接测它——
    // 放在这里就只能靠听，而"削顶/声道交错错位"恰恰是最该自动验证的两件事。
    const PackFormat format = isFloat ? PackFormat::Float32 : PackFormat::Int16;
    const std::size_t written =
        packMono(m_scratch.data(), static_cast<std::size_t>(frames), channels, format,
                 kOutputMasterGain, m_packBuffer.data(), m_packBuffer.size());
    if (written == 0) {
        return 0;
    }
    std::memcpy(data, m_packBuffer.data(), written);

    // 把"最近一次拍点"发布给界面线程（原子量单向发布，音频回调里不加锁）
    const TickInfo tick = m_renderer.lastTick();
    if (tick.counter != m_tickCounter.load()) {
        m_tickBar.store(tick.barIndex);
        m_tickBeat.store(tick.beatIndex);
        m_tickSub.store(tick.subIndex);
        m_tickRole.store(tick.role);
        m_tickAccent.store(tick.accent);
        m_tickCounter.store(tick.counter);
    }
    m_frames.fetch_add(static_cast<long long>(frames));
    m_callbacks.fetch_add(1);

    return static_cast<qint64>(written);
#endif   // PITCH_HAVE_QT_MULTIMEDIA
}

} // namespace pitch

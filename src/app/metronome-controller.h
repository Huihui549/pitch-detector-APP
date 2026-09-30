// 节拍器控制器（QML 单例 `Metronome`）
//
// 职责边界：
//   · 拍号/细分/BPM 的**规则**在 src/core（纯逻辑、可单测）
//   · 发声与采样级精确调度在 src/audio::MetronomeEngine 与 core 的渲染器
//   · 本类只做"把两者接到界面"：参数校验、持久化、自定义音频的加载、拍点指示的轮询
//
// 自定义音频的加载分两级（**先 WAV 直读，再退回解码器**）：
//   ① 未压缩 PCM WAV → 直接用 src/io 的 readWavMono()（无 Qt Multimedia 也能用，最可靠）
//   ② 其它格式（mp3/m4a/ogg…）→ 用 QAudioDecoder 解码（仅在装了 Qt Multimedia 时可用）
//   两级都不行就明确报错，并提示"请换 WAV"——不做静默降级（用户会以为上传成功了）。
//
// 编码要求（坑 A20）：本文件必须存为 UTF-8 **无 BOM**。

#pragma once

#include "metronome-engine.h"
#include "metronome-pattern.h"
#include "tap-tempo.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

namespace pitch {

class MetronomeController : public QObject {
    Q_OBJECT

public:
    explicit MetronomeController(QObject* parent = nullptr);

    // ---------------- 速度 ----------------
    Q_PROPERTY(int bpm READ bpm WRITE setBpm NOTIFY bpmChanged)
    Q_PROPERTY(int minBpm READ minBpm CONSTANT)
    Q_PROPERTY(int maxBpm READ maxBpm CONSTANT)
    /// 速度术语（Largo / Andante / Moderato / Allegro / Presto…），按当前 BPM 给出
    Q_PROPERTY(QString tempoTerm READ tempoTerm NOTIFY bpmChanged)
    /// 点击测速的提示（"已点 N 下 → M BPM"）；未开始测速时为空
    Q_PROPERTY(QString tapInfo READ tapInfo NOTIFY statusChanged)

    // ---------------- 拍号与细分 ----------------
    Q_PROPERTY(int beats READ beats NOTIFY patternChanged)
    Q_PROPERTY(int unit READ unit NOTIFY patternChanged)
    Q_PROPERTY(QString meterLabel READ meterLabel NOTIFY patternChanged)
    /// 每拍的细分数（长度 = beats），界面用 Repeater 画成一排可点的"拍块"
    Q_PROPERTY(QVariantList subdivisions READ subdivisions NOTIFY patternChanged)
    /// 可读的布局摘要，例如 "4/4 ｜ 每拍：两个八分 / 整拍 / 整拍 / 整拍"
    Q_PROPERTY(QString patternSummary READ patternSummary NOTIFY patternChanged)
    /// 预置拍号列表：[{label, beats, unit}, …]
    Q_PROPERTY(QVariantList presetMeters READ presetMeters NOTIFY neverChanged)

    // ---------------- 播放状态 ----------------
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY statusChanged)
    /// 输出设备与**实际格式**（"没声音"时先看这一行）
    Q_PROPERTY(QString outputDescription READ outputDescription NOTIFY statusChanged)
    /// 交给声卡的帧数与回调次数（随每次拍点刷新，用于判定"设备真的在取数据"）
    Q_PROPERTY(QString engineStats READ engineStats NOTIFY tickChanged)

    // ---------------- 拍点指示（界面脉冲用）----------------
    /// 每次点击递增；QML 监听它的变化来触发一次脉冲动画
    Q_PROPERTY(int tickCount READ tickCount NOTIFY tickChanged)
    /// 当前拍（0 基）；停止时为 -1
    Q_PROPERTY(int activeBeat READ activeBeat NOTIFY tickChanged)
    /// 当前是该拍内的第几个细分点（0 = 拍点本身）
    Q_PROPERTY(int activeSub READ activeSub NOTIFY tickChanged)
    /// 当前是否是强拍（每小节第一拍）
    Q_PROPERTY(bool activeAccent READ activeAccent NOTIFY tickChanged)

    // ---------------- 音色 ----------------
    /// 三个角色的完整信息：[{role, title, name, custom}, …]
    /// **一处数据源**：显示名、是否自定义都从这里取，界面不用再各绑 6 个属性，
    /// 变更时随 voicesChanged 一起刷新（避免"文件名换了但界面没更新"这类不同步）。
    Q_PROPERTY(QVariantList soundRoles READ soundRoles NOTIFY voicesChanged)

    int bpm() const { return m_bpm; }
    int minBpm() const { return kMinBpm; }
    int maxBpm() const { return kMaxBpm; }
    QString tempoTerm() const;
    QString tapInfo() const { return m_tapInfo; }

    int beats() const { return m_pattern.meter.beats; }
    int unit() const { return m_pattern.meter.unit; }
    QString meterLabel() const;
    QVariantList subdivisions() const;
    QString patternSummary() const;
    QVariantList presetMeters() const;

    bool playing() const { return m_userPlaying; }
    QString statusText() const { return m_statusText; }
    QString notice() const { return m_notice; }
    QString outputDescription() const;
    QString engineStats() const;

    int tickCount() const { return m_tickCount; }
    int activeBeat() const { return m_activeBeat; }
    int activeSub() const { return m_activeSub; }
    bool activeAccent() const { return m_activeAccent; }

    QVariantList soundRoles() const;

    // ---------------- 操作 ----------------
    /// 开始/停止播放
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();

    /// 设置速度（会自动夹到 30..300 并持久化）
    void setBpm(int bpm);
    /// 相对调整速度，例如界面的 −1 / +1 按钮
    Q_INVOKABLE void nudgeBpm(int delta);
    /// **点击测速**：跟着感觉连点若干下（每下调用一次），程序按最近几次间隔的平均给出 BPM。
    /// 停顿超过 2 秒会自动重新开始；样本不足时只提示"再点几下"（见 src/core/tap-tempo.h）
    Q_INVOKABLE void tapTempo();
    /// 直接套用某个预置拍号（下标对应 presetMeters）
    Q_INVOKABLE void applyPreset(int index);
    /// 自定义拍号（分子/分母）
    Q_INVOKABLE void applyMeter(int beats, int unit);

    /// 把第 beatIndex 拍的细分数循环切换：1→2→3→4→1
    Q_INVOKABLE void cycleSubdivision(int beatIndex);
    /// 把某一拍直接设为指定细分数
    Q_INVOKABLE void setSubdivision(int beatIndex, int subdivision);
    /// 所有拍统一设为同一细分数（1=全部整拍）
    Q_INVOKABLE void setAllSubdivisions(int subdivision);

    /// 为某个角色挑选自定义音频（role：0=强拍 1=弱拍 2=细分）。返回选中的路径（取消则空串）
    Q_INVOKABLE QString chooseSound(int role);
    /// 恢复某个角色的内置音色
    Q_INVOKABLE void clearSound(int role);
    /// 试听某个角色（未在播放时会临时启动输出，约 0.8 秒后自动停）
    Q_INVOKABLE void previewSound(int role);
    /// 恢复出厂设置（速度、拍号、三个音色）
    Q_INVOKABLE void resetToDefaults();

signals:
    void bpmChanged();
    void patternChanged();
    void playingChanged();
    void statusChanged();
    void tickChanged();
    void voicesChanged();
    /// 用于 CONSTANT 语义的列表属性：它们只在构造后一次性确定，界面无需响应变化
    void neverChanged();

private slots:
    void pollTick();

private:
    void loadSettings();
    void savePattern();
    void saveVoices();
    /// 真正写速度（不碰点击测速的历史）：属性写入与点击测速都走这里
    void applyBpm(int bpm);
    void applyVoice(int role);
    void refreshStatus();
    void stopPreviewIfNeeded();
    /// 加载自定义音频：返回是否成功；失败时把原因写进 errorOut
    bool loadSample(const QString& path, int role, QString* errorOut);

    MetronomeEngine m_engine{};
    Pattern m_pattern{};
    int m_bpm = kDefaultBpm;
    TapTempo m_tap{};          ///< 点击测速（纯逻辑在 src/core）
    QElapsedTimer m_tapClock{};///< 单调时钟：只用于给 tap() 提供时间戳
    QString m_tapInfo{};
    QTimer m_tickTimer{};
    QTimer m_previewTimer{};

    QString m_voiceName[3]{};
    QString m_voicePath[3]{};
    bool m_voiceCustom[3]{false, false, false};

    QString m_statusText{};
    QString m_notice{};
    int m_tickCount = 0;
    int m_activeBeat = -1;
    int m_activeSub = 0;
    bool m_activeAccent = false;
    /// 用户的播放意图（与"引擎是否在出声"分开：试听会让引擎短暂运行，但那不算"在播放"）
    bool m_userPlaying = false;
    bool m_previewStartedEngine = false;
    long long m_lastTickCounter = -1;
};

} // namespace pitch

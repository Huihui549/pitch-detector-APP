#include "recorder-controller.h"

#include "file-analysis-controller.h"
#include "pitch-session-controller.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>

namespace pitch {

RecorderController::RecorderController(PitchSessionController* session,
                                     FileAnalysisController* fileAnalysis, QObject* parent)
    : QObject(parent), m_session(session), m_fileAnalysis(fileAnalysis) {
    // 计时显示：200 ms 刷新一次。
    // **两个信号都要发**：`elapsedText` 绑的是 tickChanged，而 `stateText`（"录音中 12.3 s"）绑的是
    // stateChanged——只发前者会让"秒数不刷新"（用户实测反馈：只有暂停时才跳一下，正是这个原因）。
    m_tickTimer.setInterval(200);
    connect(&m_tickTimer, &QTimer::timeout, this, [this]() {
        emit tickChanged();
        emit stateChanged();
    });
    connect(&m_recorder, &AudioRecorder::stateChanged, this, [this]() {
        if (m_recorder.state() == AudioRecorder::State::Recording) {
            m_tickTimer.start();
        } else {
            m_tickTimer.stop();
        }
        emit stateChanged();
    });
    connect(&m_recorder, &AudioRecorder::errorOccurred, this, [this](const QString& message) {
        m_notice = message;
        emit stateChanged();
    });

    // 录音期间顺带跑实时分析（**复用既有采集实现**，它已处理麦克风权限/设备选择/格式协商）。
    // 两条通道互不依赖：QMediaRecorder 只负责写文件（也只有它能编 mp3），采集负责给读数。
    // 若采集起不来（例如设备被独占），录音照常继续——界面会提示"仅录音"（见 QML 的提示行）。
    connect(&m_recorder, &AudioRecorder::stateChanged, this, [this]() {
        const bool wantLive = m_recorder.state() == AudioRecorder::State::Recording;
        if (wantLive && !m_liveAnalysisActive && m_session != nullptr) {
            m_session->startMicrophone();
            m_liveAnalysisActive = m_session->running();
        } else if (!wantLive && m_liveAnalysisActive && m_session != nullptr) {
            m_session->stop();
            m_liveAnalysisActive = false;
        }
    });

    if (!m_recorder.available()) {
        m_notice = m_recorder.unavailableReason();
    }
}

QString RecorderController::elapsedText() const {
    const double sec = m_recorder.elapsedSec();
    return QStringLiteral("%1 s").arg(QString::number(sec, 'f', 1));
}

QString RecorderController::stateText() const {
    switch (m_recorder.state()) {
    case AudioRecorder::State::Recording:
        return QStringLiteral("录音中 %1").arg(elapsedText());
    case AudioRecorder::State::Paused:
        return QStringLiteral("已暂停（%1，再点继续）").arg(elapsedText());
    case AudioRecorder::State::Saving:
        return QStringLiteral("保存中…");
    default:
        return m_recorder.hasTake() ? QStringLiteral("已停止（尚未保存）") : QStringLiteral("未在录音");
    }
}

void RecorderController::start() {
    if (m_recorder.state() != AudioRecorder::State::Idle) {
        return;
    }
    m_notice.clear();
    if (!m_recorder.start()) {
        m_notice = m_recorder.lastError();
    }
    emit stateChanged();
}

void RecorderController::pauseOrResume() {
    if (m_recorder.state() == AudioRecorder::State::Recording) {
        m_recorder.pause();
    } else if (m_recorder.state() == AudioRecorder::State::Paused) {
        m_recorder.resume();
    }
    emit stateChanged();
}

void RecorderController::stopAndDiscard() {
    m_recorder.stopAndDiscard();
    emit stateChanged();
}

QString RecorderController::save() {
    if (!m_recorder.hasTake()) {
        m_notice = QStringLiteral("还没有录音可保存（先录一段，再保存）");
        emit stateChanged();
        return {};
    }
    // 保存格式与**实际录制的容器**必须一致：本工程只能"把录好的文件换个位置"，
    // 没有转码能力（Qt 不带 WAV→MP3 的转换器），所以扩展名由容器决定，不让用户随意改。
    const QString ext = m_recorder.containerExtension();
    const QString path = QFileDialog::getSaveFileName(
        nullptr, QStringLiteral("保存录音（格式：%1）").arg(m_recorder.description()),
        QStringLiteral("recording.") + ext,
        QStringLiteral("%1 音频 (*.%2);;所有文件 (*)").arg(ext.toUpper()).arg(ext));
    if (path.isEmpty()) {
        return {};
    }
    // 用户可能手打了别的扩展名：替换成实际容器对应的那个，否则会得到一个"假扩展名"文件
    QString target = path;
    const QFileInfo chosen(path);
    if (chosen.suffix().compare(ext, Qt::CaseInsensitive) != 0) {
        target = chosen.absolutePath() + QLatin1Char('/') + chosen.completeBaseName() +
                 QLatin1Char('.') + ext;
    }
    QString error;
    if (!m_recorder.saveAs(target, &error)) {
        m_notice = error;
        emit stateChanged();
        return {};
    }
    m_notice = QStringLiteral("已保存：%1（%2）").arg(QFileInfo(target).fileName(), m_recorder.description());
    // 保存成功后**顺手把这段录音分析一遍**：这样"录完就能在钢琴卷帘里看到这一段"，
    // 否则录音产出的东西在页面上看不到任何结果（用户要的是"录音分析"，不只是录音）。
    if (m_fileAnalysis != nullptr) {
        m_fileAnalysis->analyze(path);
    }
    emit stateChanged();
    return path;
}

} // namespace pitch

// 实时反馈页
//
// 手机界面：竖屏单列，从上到下 = 状态/提示 → 大字音名 → 音分条 → 曲线 → 信号强度 → 控制按钮。
// 本文件**不含任何算法**：所有数值都取自 Session（C++），包括"是否有效"的判断。

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

import PitchDetector.App 1.0
import "../components"

Item {
    id: page

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing
        spacing: Theme.spacing

        // ---- 采集控制 ----
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            Button {
                text: Session.running ? qsTr("停止") : qsTr("开始监听")
                enabled: Session.running || Session.unavailableReason === ""
                Layout.fillWidth: true
                onClicked: Session.running ? Session.stop() : Session.startMicrophone()
            }

            Button {
                text: qsTr("选择音频文件…")
                Layout.fillWidth: true
                // 用 C++ 侧的原生 QFileDialog：QML 的 FileDialog 在 Windows 上返回的路径不可靠
                // （用户实测"选完提示无法载入"），改用原生对话框后行为确定
                onClicked: Session.chooseAudioFileAndPlay()
            }
        }

        // ---- 状态与提示 ----
        Text {
            Layout.fillWidth: true
            text: Session.stateText + (Session.sourceDescription.length > 0
                                      ? "　·　" + Session.sourceDescription : "")
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.fillWidth: true
            visible: Session.unavailableReason.length > 0
            implicitHeight: reasonText.implicitHeight + Theme.spacing
            radius: Theme.radius
            color: Theme.surface

            Text {
                id: reasonText
                anchors.fill: parent
                anchors.margins: Theme.spacing / 2
                text: Session.unavailableReason
                color: Theme.warn
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }
        }

        Rectangle {
            Layout.fillWidth: true
            visible: Session.notice.length > 0
            implicitHeight: noticeText.implicitHeight + Theme.spacing
            radius: Theme.radius
            color: Theme.surface

            Text {
                id: noticeText
                anchors.fill: parent
                anchors.margins: Theme.spacing / 2
                text: Session.notice
                color: Theme.warn
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }
        }

        // ---- 大字音名 ----
        NoteDisplay {
            Layout.fillWidth: true
            Layout.preferredHeight: 150
            noteText: Session.noteName
        }

        // ---- 音分条 ----
        CentsBar {
            Layout.fillWidth: true
            cents: Session.cents
            active: Session.noteName !== "—"
        }

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: qsTr("当前频率：%1 Hz").arg(Session.frequency > 0
                                              ? Session.frequency.toFixed(2) : "—")
                color: Theme.text
                font.pixelSize: Theme.fontNormal
            }
            Item { Layout.fillWidth: true }
            Text {
                text: qsTr("置信度：%1").arg(Session.confidence.toFixed(2))
                color: Session.confidence >= 0.75 ? Theme.ok : Theme.textDim
                font.pixelSize: Theme.fontNormal
            }
        }

        // ---- 曲线（近 5 秒） ----
        PitchCurve {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 110
            points: Session.curve
            referenceLines: []
            freqMin: {
                // 纵轴范围由界面按当前数据自适应：只是为了画得好看，不参与判定
                var pts = Session.curve;
                if (!pts || pts.length === 0) return 0;
                var lo = pts[0].freq, hi = pts[0].freq;
                for (var i = 1; i < pts.length; ++i) {
                    lo = Math.min(lo, pts[i].freq);
                    hi = Math.max(hi, pts[i].freq);
                }
                var pad = Math.max(1, (hi - lo) * 0.15);
                return lo - pad;
            }
            freqMax: {
                var pts = Session.curve;
                if (!pts || pts.length === 0) return 0;
                var lo = pts[0].freq, hi = pts[0].freq;
                for (var i = 1; i < pts.length; ++i) {
                    lo = Math.min(lo, pts[i].freq);
                    hi = Math.max(hi, pts[i].freq);
                }
                var pad = Math.max(1, (hi - lo) * 0.15);
                return hi + pad;
            }
        }

        // ---- 信号强度 ----
        LevelMeter {
            Layout.fillWidth: true
            rms: Session.rms
        }

        Text {
            Layout.fillWidth: true
            // 窗长与保持时长都用 C++ 给的值：界面不重复定义算法参数（分层门禁会检查）
            text: qsTr("采样率 %1 Hz　·　实时窗长 %2 样点　·　读数保持 700 ms")
                  .arg(Session.sampleRate)
                  .arg(Session.frameDescription)
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }

        // 采集诊断：能区分"没启动 / 收不到数据 / 有数据但读数不对"三种情况
        Text {
            Layout.fillWidth: true
            text: qsTr("采集统计：%1").arg(Session.captureStats)
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("原始信号峰值：%1（0 表示麦克风没收到任何声音）")
                  .arg(Session.peakRms.toFixed(6))
            color: Session.peakRms > 0 ? Theme.ok : Theme.textDim
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }
    }

    // 用一段 WAV 当作"实时流"喂进来：无麦克风时也能验证实时链路（FileAudioSource）
    // 注意：文件选择走 C++ 的原生 QFileDialog（见上方按钮），QML FileDialog 在 Windows 上不可靠
}

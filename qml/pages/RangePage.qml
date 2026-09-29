// 音域测量页
//
// 手机界面：三个指标卡（当前音 / 最高 / 最低）+ 跨度与时长 + 清零。
// 极值的判定条件（置信度门槛、连续命中帧数）在 C++ 里，本页只显示结果。

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import PitchDetector.App 1.0
import "../components"

Item {
    id: page

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing
        spacing: Theme.spacing

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
                text: qsTr("清零")
                Layout.fillWidth: true
                onClicked: Session.resetStatistics()
            }
        }

        Text {
            Layout.fillWidth: true
            text: Session.stateText
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
        }

        // 当前音（大字）
        StatCard {
            Layout.fillWidth: true
            label: qsTr("当前音")
            value: Session.noteName
            hint: Session.noteName !== "—"
                  ? qsTr("%1 Hz　·　%2 音分").arg(Session.frequency.toFixed(2))
                        .arg((Session.cents >= 0 ? "+" : "") + Session.cents.toFixed(1))
                  : qsTr("未检测到稳定音高")
            valueColor: Theme.accent
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            StatCard {
                Layout.fillWidth: true
                label: qsTr("本次最高音")
                value: Session.highestNote
                valueColor: Theme.warn
            }

            StatCard {
                Layout.fillWidth: true
                label: qsTr("本次最低音")
                value: Session.lowestNote
                valueColor: Theme.ok
            }
        }

        StatCard {
            Layout.fillWidth: true
            label: qsTr("音域跨度")
            value: Session.rangeSemitones > 0
                   ? qsTr("%1 个半音").arg(Session.rangeSemitones)
                   : "—"
            hint: Session.rangeSemitones > 0
                  ? qsTr("约 %1 个八度　·　累计有效时长 %2 s")
                        .arg((Session.rangeSemitones / 12).toFixed(2))
                        .arg(Session.validSeconds.toFixed(1))
                  : qsTr("从低到高唱/奏一遍，再看这里")
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("极值只在置信度达标且连续命中 ≥3 帧时更新，避免单帧误判把音域拉虚。")
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }

        Item { Layout.fillHeight: true }
    }
}

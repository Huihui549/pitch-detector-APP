// 录音分析页
//
// 手机界面：选择音频 → 分析（独立线程）→ 摘要 + 曲线 + 逐帧表格 + CSV 导出。
// 本文件不含任何算法与阈值判断。

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

        // ---- 选择与分析 ----
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            Button {
                text: qsTr("选择音频文件…")
                Layout.fillWidth: true
                enabled: !FileAnalysis.analyzing
                // 走 C++ 的原生 QFileDialog：QML FileDialog 在 Windows 上返回的路径不可靠
                onClicked: FileAnalysis.chooseAndAnalyze()
            }

            Button {
                text: qsTr("导出 CSV…")
                Layout.fillWidth: true
                enabled: FileAnalysis.frameCount > 0
                onClicked: saveDialog.open()
            }
        }

        // ---- 进度与状态 ----
        RowLayout {
            Layout.fillWidth: true
            visible: FileAnalysis.analyzing
            Text {
                text: qsTr("分析中 %1%").arg(Math.round(FileAnalysis.progress * 100))
                color: Theme.text
                font.pixelSize: Theme.fontNormal
            }
            ProgressBar {
                Layout.fillWidth: true
                from: 0
                to: 1
                value: FileAnalysis.progress
            }
            Button {
                text: qsTr("取消")
                onClicked: FileAnalysis.cancel()
            }
        }

        Text {
            Layout.fillWidth: true
            text: FileAnalysis.stateText
            color: FileAnalysis.errorText.length > 0 ? Theme.warn : Theme.textDim
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }

        // ---- 摘要 ----
        Rectangle {
            Layout.fillWidth: true
            visible: FileAnalysis.summary.length > 0
            implicitHeight: summaryText.implicitHeight + Theme.spacing
            radius: Theme.radius
            color: Theme.surface

            Text {
                id: summaryText
                anchors.fill: parent
                anchors.margins: Theme.spacing / 2
                text: FileAnalysis.summary
                color: Theme.text
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }
        }

        // ---- 曲线（整段） ----
        PitchCurve {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 120
            // 抽样与量程由 C++ 一次算完（QML 里按裸 role 号逐行取几万帧会卡界面，属脆弱写法）
            points: FileAnalysis.preview.points
            freqMin: FileAnalysis.preview.freqMin
            freqMax: FileAnalysis.preview.freqMax
        }

        // ---- 逐帧表格（手机上一屏放不下，给固定高度 + 可滚动） ----
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 150
            visible: FileAnalysis.frameCount > 0
            radius: Theme.radius
            color: Theme.surface
            clip: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 2

                RowLayout {
                    Layout.fillWidth: true
                    Text { text: qsTr("时间(s)");  color: Theme.textDim; font.pixelSize: Theme.fontSmall; Layout.preferredWidth: 60 }
                    Text { text: qsTr("音名");     color: Theme.textDim; font.pixelSize: Theme.fontSmall; Layout.preferredWidth: 50 }
                    Text { text: qsTr("频率(Hz)"); color: Theme.textDim; font.pixelSize: Theme.fontSmall; Layout.fillWidth: true }
                    Text { text: qsTr("音分");     color: Theme.textDim; font.pixelSize: Theme.fontSmall; Layout.preferredWidth: 55 }
                    Text { text: qsTr("置信");     color: Theme.textDim; font.pixelSize: Theme.fontSmall; Layout.preferredWidth: 45 }
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: FileAnalysis.frames
                    ScrollBar.vertical: ScrollBar { }

                    delegate: RowLayout {
                        width: ListView.view.width
                        spacing: 0
                        Text {
                            text: model.timeSec.toFixed(3)
                            color: Theme.text; font.pixelSize: Theme.fontSmall
                            Layout.preferredWidth: 60
                        }
                        Text {
                            text: model.note
                            color: Theme.accent; font.pixelSize: Theme.fontSmall; font.bold: true
                            Layout.preferredWidth: 50
                        }
                        Text {
                            text: model.freq.toFixed(2)
                            color: Theme.text; font.pixelSize: Theme.fontSmall
                            Layout.fillWidth: true
                        }
                        Text {
                            text: (model.cents >= 0 ? "+" : "") + model.cents.toFixed(1)
                            color: Math.abs(model.cents) <= 5 ? Theme.ok : Theme.text
                            font.pixelSize: Theme.fontSmall
                            Layout.preferredWidth: 55
                        }
                        Text {
                            text: model.confidence.toFixed(2)
                            color: Theme.textDim; font.pixelSize: Theme.fontSmall
                            Layout.preferredWidth: 45
                        }
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: FileAnalysis.frameCount === 0 && !FileAnalysis.analyzing
            text: qsTr("提示：只支持未压缩 PCM WAV。选一段乐器单音或音阶录音，几秒即可。")
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }
    }

    FileDialog {
        id: saveDialog
        title: qsTr("导出逐帧 CSV")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "csv"
        nameFilters: [qsTr("CSV 文件 (*.csv)")]
        onAccepted: FileAnalysis.exportCsv(selectedFile.toString())
    }
}

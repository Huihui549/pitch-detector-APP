// 实时反馈页（D+E 风格）
//
// 版式（自上而下）：控制 → 状态 → 提示 → **主读数卡** → 曲线卡 → 信号卡 → 运行信息卡。
// 骨架参照 Material 3 的手机单列布局（主信息居中放大、次要信息收进卡片、操作区留在拇指可达处），
// 视觉按 D 风格（近黑底 + 单一强调色 + 大字读数）由 Theme 令牌统一给出。
//
// 本文件**不含任何算法与阈值**：数值与"是否有效"全部来自 Session（C++）；
// 置信度门槛与读数保持时长也由 C++ 暴露（displayMinConfidence / holdMs），界面不得自己定。

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import PitchDetector.App 1.0
import "../components"

Item {
    id: page

    // 卡片：统一"面 + 1px 描边 + 内边距"三件套，避免每页各写一套（风格漂移的常见来源）
    component Card: Rectangle {
        id: card
        default property alias content: inner.data
        color: Theme.surface
        border.width: 1
        border.color: Theme.border
        radius: Theme.radius
        implicitHeight: inner.implicitHeight + Theme.spacing * 2

        ColumnLayout {
            id: inner
            anchors.fill: parent
            anchors.margins: Theme.spacing
            spacing: Theme.spaceSm
        }
    }

    // 内容区：**可上下滑动**（标题区与底部导航由 Main.qml 固定住，不参与滚动，ADR-0014）
    PageScroller {
        anchors.fill: parent

        // ---------- 1. 采集控制 ----------
        // 只有"开始/停止监听"一个按钮：文件选择属"录音分析"页，不在这里出现（2026-09-30 用户要求）
        ActionButton {
            Layout.fillWidth: true
            primary: true
            icon: Session.running ? "square" : "mic"
            text: Session.running ? qsTr("停止监听") : qsTr("开始监听")
            enabled: Session.running || Session.unavailableReason === ""
            onClicked: Session.running ? Session.stop() : Session.startMicrophone()
        }

        // ---------- 2. 状态行 ----------
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceSm

            Icon {
                name: Session.running ? "activity" : "mic-off"
                size: Theme.iconSm
                color: Session.running ? Theme.ok : Theme.textDim
            }
            Text {
                Layout.fillWidth: true
                text: Session.stateText + (Session.sourceDescription.length > 0
                                          ? "　·　" + Session.sourceDescription : "")
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }
        }

        // ---------- 3. 提示（不可用原因 / 格式协商提示） ----------
        Card {
            Layout.fillWidth: true
            visible: Session.unavailableReason.length > 0
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm
                Icon {
                    name: "circle-alert"
                    size: Theme.iconMd
                    color: Theme.warn
                }
                Text {
                    Layout.fillWidth: true
                    text: Session.unavailableReason
                    color: Theme.warn
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
            }
        }

        Card {
            Layout.fillWidth: true
            visible: Session.notice.length > 0
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm
                Icon {
                    name: "info"
                    size: Theme.iconMd
                    color: Theme.warn
                }
                Text {
                    Layout.fillWidth: true
                    text: Session.notice
                    color: Theme.warn
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
            }
        }

        // ---------- 4. 主读数 ----------
        Card {
            Layout.fillWidth: true

            NoteDisplay {
                Layout.fillWidth: true
                Layout.preferredHeight: 168
                noteText: Session.noteName
            }

            CentsBar {
                Layout.fillWidth: true
                cents: Session.cents
                active: Session.noteName !== "—"
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm

                Text {
                    text: qsTr("当前频率")
                    color: Theme.textDim
                    font.pixelSize: Theme.fontMicro
                    // Layout 管理的元素不能用 anchors（QML 明确报"未定义行为"），用 Layout.alignment
                    Layout.alignment: Qt.AlignBottom
                    Layout.bottomMargin: Theme.spaceXs
                }
                Text {
                    id: freqText
                    text: Session.frequency > 0 ? qsTr("%1 Hz").arg(Session.frequency.toFixed(2)) : "—"
                    color: Theme.text
                    font.pixelSize: Theme.fontDisplay
                    font.bold: true
                }

                Item { Layout.fillWidth: true }

                // 置信度胶囊：颜色按"是否达到界面门槛"分档（门槛来自 C++，界面不自己定）
                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: confText.implicitWidth + Theme.spacing
                    implicitHeight: confText.implicitHeight + Theme.spaceSm
                    radius: Theme.radiusPill
                    color: Theme.surfaceAlt
                    border.width: 1
                    border.color: Session.confidence >= Session.displayMinConfidence ? Theme.ok : Theme.border

                    Text {
                        id: confText
                        anchors.centerIn: parent
                        text: qsTr("置信度 %1").arg(Session.confidence.toFixed(2))
                        color: Session.confidence >= Session.displayMinConfidence ? Theme.ok : Theme.textDim
                        font.pixelSize: Theme.fontMicro
                    }
                }
            }
        }

        // ---------- 5. 音域（原「音域测量」页已并入本页，2026-09-30 用户要求） ----------
        // 原页的"当前音"就是上面的主读数，故这里只保留极值与跨度，避免同一信息出现两遍。
        Card {
            Layout.fillWidth: true

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm
                Icon {
                    name: "gauge"
                    size: Theme.iconSm
                    color: Theme.textDim
                }
                Text {
                    text: qsTr("本次音域")
                    color: Theme.textDim
                    font.pixelSize: Theme.fontMicro
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: Session.rangeSemitones > 0
                          ? qsTr("约 %1 个八度").arg((Session.rangeSemitones / 12).toFixed(2))
                          : qsTr("尚无数据")
                    color: Theme.textDim
                    font.pixelSize: Theme.fontMicro
                }
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

            Text {
                Layout.fillWidth: true
                text: Session.rangeSemitones > 0
                      ? qsTr("跨度 %1 个半音　·　累计有效时长 %2 s")
                            .arg(Session.rangeSemitones)
                            .arg(Session.validSeconds.toFixed(1))
                      : qsTr("从低到高唱/奏一遍，这里会记下最高与最低音")
                color: Theme.text
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }

            ActionButton {
                Layout.fillWidth: true
                primary: false
                icon: "refresh-cw"
                text: qsTr("清零音域统计")
                onClicked: Session.resetStatistics()
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("极值只在置信度达标且连续命中 ≥3 帧时更新，避免单帧误判把音域拉虚。")
                color: Theme.textDim
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }
        }

        // ---------- 6. 输入电平 ----------
        Card {
            Layout.fillWidth: true

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm
                Icon {
                    name: "mic"
                    size: Theme.iconSm
                    color: Theme.textDim
                }
                Text {
                    text: qsTr("输入电平")
                    color: Theme.textDim
                    font.pixelSize: Theme.fontMicro
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: qsTr("原始峰值 %1").arg(Session.peakRms.toFixed(6))
                    color: Session.peakRms > 0 ? Theme.ok : Theme.textDim
                    font.pixelSize: Theme.fontMicro
                }
            }

            LevelMeter {
                Layout.fillWidth: true
                rms: Session.rms
                // 门槛来自 C++（SSOT）：界面不写 0.008 这类算法常量
                floorValue: Session.rmsFloor
            }
        }

        // ---------- 7. 运行信息（次要信息收进卡片，避免抢主读数的视觉权重） ----------
        Card {
            Layout.fillWidth: true

            Text {
                Layout.fillWidth: true
                text: qsTr("采样率 %1 Hz　·　实时窗长 %2 样点　·　%3")
                      .arg(Session.sampleRate)
                      .arg(Session.frameDescription)
                      .arg(qsTr("读数保持 %1 ms").arg(Session.holdMs))
                color: Theme.textDim
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("采集统计：%1").arg(Session.captureStats)
                color: Theme.textDim
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }
        }
    }
}

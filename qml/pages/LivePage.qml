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
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            ActionButton {
                Layout.fillWidth: true
                primary: true
                icon: Session.running ? "square" : "mic"
                text: Session.running ? qsTr("停止监听") : qsTr("开始监听")
                enabled: Session.running || Session.unavailableReason === ""
                onClicked: Session.running ? Session.stop() : Session.startMicrophone()
            }

            ActionButton {
                Layout.fillWidth: true
                primary: false
                icon: "folder-open"
                text: qsTr("选择音频文件")
                // 走 C++ 的原生 QFileDialog：QML FileDialog 在 Windows 上返回的路径不可靠
                onClicked: Session.chooseAudioFileAndPlay()
            }
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

        // ---------- 5. 音高曲线 ----------
        Card {
            Layout.fillWidth: true
            // 滚动容器里 `fillHeight` 没有意义（高度由内容决定），给固定高度
            Layout.preferredHeight: 220

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm
                Icon {
                    name: "waves"
                    size: Theme.iconSm
                    color: Theme.textDim
                }
                Text {
                    text: qsTr("近 5 秒音高")
                    color: Theme.textDim
                    font.pixelSize: Theme.fontMicro
                }
                Item { Layout.fillWidth: true }
            }

            PitchCurve {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 90
                points: Session.curve
                referenceLines: []
                freqMin: {
                    // 纵轴范围由界面按当前数据自适应：只是为了画得好看，不参与判定
                    var pts = Session.curve;
                    if (!pts || pts.length === 0)
                        return 0;
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
                    if (!pts || pts.length === 0)
                        return 0;
                    var lo = pts[0].freq, hi = pts[0].freq;
                    for (var i = 1; i < pts.length; ++i) {
                        lo = Math.min(lo, pts[i].freq);
                        hi = Math.max(hi, pts[i].freq);
                    }
                    var pad = Math.max(1, (hi - lo) * 0.15);
                    return hi + pad;
                }
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

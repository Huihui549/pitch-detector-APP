// 更多页（设置与关于）
//
// 手机界面：A4 基准设置（首版为显示项，改动需重算音名——见下）、构建与运行信息、许可说明。
//
// 说明：A4 基准目前由 C++ 的 EngineConfig 持有，界面只读展示。
// 不在此处"假可改"（旋钮转了但算法没变，比没有更糟）。

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import PitchDetector.App 1.0
import "../components"

Item {
    id: page

    // 内容区：**可上下滑动**（标题区与底部导航由 Main.qml 固定住，不参与滚动，ADR-0014）
    PageScroller {
        anchors.fill: parent

        // ---------- 外观：主题切换（深色 / 浅色）----------
        // 写入 Theme.mode 即持久化（QSettings `ui/themeMode`），重启后保持（ADR-0013）。
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: appearance.implicitHeight + Theme.spacing * 2
            radius: Theme.radius
            color: Theme.surface
            border.width: 1
            border.color: Theme.border

            ColumnLayout {
                id: appearance
                anchors.fill: parent
                anchors.margins: Theme.spacing
                spacing: Theme.spaceSm

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceSm

                    Icon {
                        name: "palette"
                        size: Theme.iconSm
                        color: Theme.textDim
                    }
                    Text {
                        text: qsTr("外观")
                        color: Theme.textDim
                        font.pixelSize: Theme.fontMicro
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: Theme.dark ? qsTr("当前：深色") : qsTr("当前：浅色")
                        color: Theme.textDim
                        font.pixelSize: Theme.fontMicro
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceSm

                    ActionButton {
                        Layout.fillWidth: true
                        primary: Theme.dark
                        icon: "moon"
                        text: qsTr("深色")
                        onClicked: Theme.mode = "dark"
                    }
                    ActionButton {
                        Layout.fillWidth: true
                        primary: !Theme.dark
                        icon: "sun"
                        text: qsTr("浅色")
                        onClicked: Theme.mode = "light"
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("深色为默认（弱光下读数更清晰）；浅色适合强光环境。选择会被记住。")
                    color: Theme.textDim
                    font.pixelSize: Theme.fontMicro
                    wrapMode: Text.WordWrap
                }
            }
        }

        StatCard {
            Layout.fillWidth: true
            label: qsTr("A4 基准")
            value: qsTr("440 Hz")
            hint: qsTr("十二平均律基准。乐器按 442 Hz 定音时的可调版本列入后续迭代（ADR 记录在案）")
        }

        StatCard {
            Layout.fillWidth: true
            label: qsTr("算法")
            value: qsTr("基频估计 + 级联窗长")
            // 音域数值由 C++ 提供（Session.rangeDescription）：界面不重复定义算法参数
            hint: qsTr("音域 %1（钢琴全 88 键）；C++ 实现与网页版逐帧一致（见验收记录）")
                  .arg(Session.rangeDescription)
        }

        StatCard {
            Layout.fillWidth: true
            label: qsTr("运行信息")
            value: qsTr("采样率 %1 Hz").arg(Session.sampleRate)
            hint: qsTr("采集：%1").arg(Session.sourceDescription.length > 0
                                      ? Session.sourceDescription : qsTr("未启动"))
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: aboutText.implicitHeight + Theme.spacing
            radius: Theme.radius
            color: Theme.surface

            Text {
                id: aboutText
                anchors.fill: parent
                anchors.margins: Theme.spacing / 2
                text: qsTr("pitch-detector-APP 0.1.0\n"
                           + "Qt 6.8.3 / C++20 / QML\n\n"
                           + "界面为纯 QML，音高算法与流程全在 C++（src/core 不依赖 Qt）。\n"
                           + "调试入口：在页面标题区连点 7 次。")
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }
        }
    }
}

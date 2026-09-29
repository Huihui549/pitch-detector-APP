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

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing
        spacing: Theme.spacing

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

        Item { Layout.fillHeight: true }
    }
}

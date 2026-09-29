// 信号强度条（RMS）
//
// 界面层组件：只画输入电平与阈值位置。阈值由 C++ 给出（Session 的静音门槛），
// 组件不自己判定"多少算子声"。

import QtQuick
import PitchDetector.App 1.0

Item {
    id: root

    /// 当前 RMS（0..1）
    property real rms: 0
    /// 静音门槛（画一根刻线）
    property real floorValue: 0.008
    /// 是否削顶
    property bool clipped: false

    implicitHeight: 26

    Rectangle {
        id: track
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        height: 12
        radius: 6
        color: Theme.surfaceAlt

        Rectangle {
            id: fill
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            height: parent.height
            radius: parent.radius
            // 取对数刻度：线性刻度下 0.01 与 0.1 挤在一起看不出差别
            width: parent.width * Math.max(0, Math.min(1, Math.log(1 + root.rms * 99) / Math.log(100)))
            color: root.clipped ? Theme.danger : (root.rms >= root.floorValue ? Theme.ok : Theme.textDim)
        }

        // 静音门槛刻线
        Rectangle {
            x: parent.width * Math.max(0, Math.min(1, Math.log(1 + root.floorValue * 99) / Math.log(100)))
            anchors.verticalCenter: parent.verticalCenter
            width: 2
            height: parent.height + 8
            color: Theme.warn
        }
    }

    Text {
        anchors.left: parent.left
        anchors.bottom: track.top
        text: qsTr("信号强度 %1").arg(root.rms.toFixed(4))
        color: Theme.textDim
        font.pixelSize: Theme.fontSmall
    }

    Text {
        anchors.right: parent.right
        anchors.bottom: track.top
        text: root.clipped ? qsTr("已削顶！") : qsTr("橙线 = 静音门槛")
        color: root.clipped ? Theme.danger : Theme.textDim
        font.pixelSize: Theme.fontSmall
    }
}

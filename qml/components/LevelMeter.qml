// 信号强度条（RMS）—— D 风格：像电平表
//
// 界面层组件：只画输入电平与门槛刻线。**门槛值由 C++ 给出**（Session.rmsFloor），
// 界面不自己写 0.008 这类算法常量（SSOT：阈值只有 src/core 与控制器一处定义）。

import QtQuick
import PitchDetector.App 1.0

Item {
    id: root

    /// 当前 RMS（0..1）
    property real rms: 0
    /// 静音门槛（由 Session.rmsFloor 传入；0 表示未知，则不画刻线）
    property real floorValue: 0
    /// 是否削顶
    property bool clipped: false

    implicitHeight: 34

    Rectangle {
        id: track
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        height: 12
        radius: Theme.radiusPill
        color: Theme.surfaceAlt
        border.width: 1
        border.color: Theme.border

        Rectangle {
            id: fill
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            height: parent.height - 2
            radius: Theme.radiusPill
            // 取对数刻度：线性刻度下 0.01 与 0.1 挤在一起看不出差别
            width: Math.max(0, (parent.width - 2) * Math.max(0, Math.min(1, Math.log(1 + root.rms * 99) / Math.log(100))))
            color: root.clipped ? Theme.danger : (root.rms >= root.floorValue ? Theme.ok : Theme.textDim)
        }

        // 静音门槛刻线（只有拿到 C++ 给的门槛时才画）
        Rectangle {
            visible: root.floorValue > 0
            x: parent.width * Math.max(0, Math.min(1, Math.log(1 + root.floorValue * 99) / Math.log(100)))
            anchors.verticalCenter: parent.verticalCenter
            width: 2
            height: parent.height + Theme.spaceSm
            color: Theme.warn
        }
    }

    Text {
        anchors.left: parent.left
        anchors.bottom: track.top
        anchors.bottomMargin: Theme.spaceXs
        text: qsTr("信号强度 %1").arg(root.rms.toFixed(4))
        color: Theme.textDim
        font.pixelSize: Theme.fontMicro
    }

    Text {
        anchors.right: parent.right
        anchors.bottom: track.top
        anchors.bottomMargin: Theme.spaceXs
        text: root.clipped ? qsTr("已削顶！") : qsTr("橙线 = 静音门槛")
        color: root.clipped ? Theme.danger : Theme.textDim
        font.pixelSize: Theme.fontMicro
    }
}

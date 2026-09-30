// 音分偏差条（D 风格：像调音器的指针表）
//
// 界面层组件：把 -50..+50 音分画成一根带指针的横条。
// **不含音分换算**——输入就是音分；±50 只是显示范围，不是算法参数。

import QtQuick
import QtQuick.Controls
import PitchDetector.App 1.0

Item {
    id: root

    /// 音分偏差（-50..+50 之外会被夹住显示）
    property real cents: 0
    /// 是否有效（无效时不画指针）
    property bool active: false

    implicitHeight: 64

    Rectangle {
        id: track
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: 12
        radius: Theme.radiusPill
        color: Theme.surfaceAlt
        border.width: 1
        border.color: Theme.border

        // 容差区（±5 音分，练琴时"准"的范围）：淡绿底 + 中心刻线
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width * (10.0 / 100.0)
            height: parent.height - 2
            radius: Theme.radiusPill
            color: Theme.ok
            opacity: 0.22
        }

        // 中心刻线（0 音分）
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            width: 1
            height: parent.height + Theme.spaceSm
            color: Theme.textDim
        }
    }

    // 端点标注
    Text {
        anchors.left: parent.left
        anchors.bottom: track.top
        anchors.bottomMargin: Theme.spaceXs
        text: "-50"
        color: Theme.textDim
        font.pixelSize: Theme.fontMicro
    }
    Text {
        anchors.right: parent.right
        anchors.bottom: track.top
        anchors.bottomMargin: Theme.spaceXs
        text: "+50"
        color: Theme.textDim
        font.pixelSize: Theme.fontMicro
    }

    // 指针：外层柔光 + 内层实针（仪表指针的观感，弱光下也看得见）
    Item {
        id: needle
        visible: root.active
        width: 16
        height: 34
        // 夹到显示范围内：|cents| > 50 时指针贴边，不越界
        x: track.x + track.width * (0.5 + Math.max(-50, Math.min(50, root.cents)) / 100.0) - width / 2
        anchors.verticalCenter: track.verticalCenter

        readonly property color stateColor: Math.abs(root.cents) <= 5 ? Theme.ok : Theme.warn

        Behavior on x {
            NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutQuad }
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            width: 10
            height: parent.height
            radius: Theme.radiusPill
            color: needle.stateColor
            opacity: 0.25
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            width: 4
            height: parent.height
            radius: Theme.radiusPill
            color: needle.stateColor
        }
    }

    Text {
        id: centsText
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        text: root.active ? (root.cents >= 0 ? "+" : "") + root.cents.toFixed(1) + " 音分" : "—"
        color: root.active ? Theme.text : Theme.textDim
        font.pixelSize: Theme.fontNormal
        font.bold: true
    }
}

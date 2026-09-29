// 音分偏差条
//
// 界面层组件：把 -50..+50 音分画成一根带指针的横条。
// **不含音分换算**——输入就是音分，阈值（±50）只是显示范围，不是算法参数。

import QtQuick
import QtQuick.Controls
import PitchDetector.App 1.0

Item {
    id: root

    /// 音分偏差（-50..+50 之外会被夹住显示）
    property real cents: 0
    /// 是否有效（无效时不画指针）
    property bool active: false

    implicitHeight: 54

    Rectangle {
        id: track
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: 10
        radius: 5
        color: Theme.surfaceAlt

        // 中心刻线（0 音分）
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            width: 2
            height: parent.height + 10
            color: Theme.textDim
        }

        // 容差区（±5 音分，练琴时"准"的范围）
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width * (10.0 / 100.0)
            height: parent.height
            color: Theme.ok
            opacity: 0.35
        }
    }

    // 端点标注
    Text {
        anchors.left: parent.left
        anchors.bottom: track.top
        text: "-50"
        color: Theme.textDim
        font.pixelSize: Theme.fontSmall
    }
    Text {
        anchors.right: parent.right
        anchors.bottom: track.top
        text: "+50"
        color: Theme.textDim
        font.pixelSize: Theme.fontSmall
    }

    // 指针
    Rectangle {
        id: pointer
        visible: root.active
        width: 4
        height: 26
        radius: 2
        color: Math.abs(root.cents) <= 5 ? Theme.ok : Theme.warn
        // 夹到显示范围内：|cents| > 50 时指针贴边，不越界
        x: track.x + track.width * (0.5 + Math.max(-50, Math.min(50, root.cents)) / 100.0) - width / 2
        anchors.verticalCenter: track.verticalCenter
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

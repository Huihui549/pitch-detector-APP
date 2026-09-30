// 选择胶囊（Chip）
//
// 为什么做成组件而不是在页面里写 Rectangle+Text：拍号预置、快捷项都要用同一形态，
// 各写一份会让圆角/描边/选中态很快漂移（与 ActionButton 同一理由：风格统一靠"只有一处定义"）。
//
// 用法：Chip { text: "4/4"; selected: Metronome.meterLabel === "4/4"; onClicked: … }

import QtQuick
import PitchDetector.App 1.0

Rectangle {
    id: root

    property string text: ""
    property bool selected: false

    signal clicked()

    implicitWidth: label.implicitWidth + Theme.spacing * 2
    implicitHeight: Math.max(Theme.touchTarget - Theme.spaceSm, label.implicitHeight + Theme.spaceSm * 2)
    radius: Theme.radiusPill
    color: root.selected ? Theme.accentDim : Theme.surfaceAlt
    border.width: 1
    border.color: root.selected ? Theme.accent : Theme.border

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: root.selected ? Theme.text : Theme.textDim
        font.pixelSize: Theme.fontSmall
        font.bold: root.selected
    }

    MouseArea {
        anchors.fill: parent
        onClicked: root.clicked()
    }
}

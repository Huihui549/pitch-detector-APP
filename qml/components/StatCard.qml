// 指标卡片（标签 + 值）
//
// 界面层组件：纯展示。用于音域/统计等只读数据的排布。

import QtQuick
import PitchDetector.App 1.0

Rectangle {
    id: root

    /// 卡片标题
    property string label: ""
    /// 主值
    property string value: "—"
    /// 值下面的补充说明（可空）
    property string hint: ""
    /// 值的强调色
    property color valueColor: Theme.text

    implicitHeight: content.implicitHeight + Theme.spacing * 2
    radius: Theme.radius
    color: Theme.surface

    Column {
        id: content
        anchors.fill: parent
        anchors.margins: Theme.spacing
        spacing: 4

        Text {
            text: root.label
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
        }

        Text {
            text: root.value
            color: root.valueColor
            font.pixelSize: Theme.fontTitle
            font.bold: true
        }

        Text {
            visible: root.hint.length > 0
            text: root.hint
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
            width: parent.width
        }
    }
}

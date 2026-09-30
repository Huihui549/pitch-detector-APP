// 按钮（**所有按钮的唯一入口**）
//
// 为什么要做成组件而不是各处写 Button/Rectangle：风格统一靠"只有一处定义按钮长什么样"。
// 两种形态：
//   · primary   = 强调色实心（页面主操作，如"开始监听"）
//   · secondary = 透明底 + 1px 描边（次要操作，如"选择音频文件"）
// 颜色/圆角/高度全部取 Theme 令牌；图标走 Icon.qml（SVG + 着色）。
//
// 用法：ActionButton { text: qsTr("开始监听"); icon: "mic"; primary: true; onClicked: ... }

import QtQuick
import PitchDetector.App 1.0

Rectangle {
    id: root

    property string text: ""
    /// 图标名（resources/icons 下的文件名），留空则不画图标
    property string icon: ""
    /// 主操作（强调色实心）还是次操作（描边）
    property bool primary: true

    signal clicked()

    readonly property bool hovered: mouse.containsMouse
    readonly property color contentColor: {
        if (!root.enabled)
            return Theme.textDim;
        if (root.primary)
            return Theme.background;          // 强调色底上用深色字，对比度最高
        return mouse.pressed ? Theme.accent : Theme.text;
    }

    implicitHeight: Math.max(Theme.touchTarget, content.implicitHeight + Theme.spacing)
    radius: Theme.radius
    opacity: root.enabled ? 1.0 : 0.45

    color: {
        if (!root.enabled)
            return Theme.surfaceAlt;
        if (root.primary)
            return mouse.pressed ? Qt.darker(Theme.accent, 1.12) : Theme.accent;
        return mouse.pressed ? Theme.surfaceAlt : "transparent";
    }

    border.width: root.primary ? 0 : 1
    border.color: mouse.containsMouse ? Theme.accent : Theme.border

    Behavior on color {
        ColorAnimation { duration: Theme.durationFast }
    }

    Row {
        id: content
        anchors.centerIn: parent
        spacing: Theme.spaceSm

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.icon.length > 0
            name: root.icon
            size: Theme.iconMd
            color: root.contentColor
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: root.contentColor
            font.pixelSize: Theme.fontNormal
            font.bold: root.primary
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        onClicked: {
            if (root.enabled) {
                root.clicked();
            }
        }
    }
}

// 图标按钮（只有图标、无文字）
//
// 用途：一行里要放多个"轻操作"时（例如音色行的 试听 / 选择音频 / 恢复内置），
// 用带文字的 ActionButton 会让一行挤到放不下。点击区仍按 Theme.touchTarget（48）保证好点。
//
// 用法：IconButton { icon: "play"; onClicked: … }

import QtQuick
import PitchDetector.App 1.0

Rectangle {
    id: root

    /// 图标名（resources/icons 下的文件名，不含扩展名）
    property string icon: ""
    /// 主操作（强调色实心）还是次操作（描边）
    property bool primary: false

    signal clicked()

    readonly property bool hovered: mouse.containsMouse

    implicitWidth: Theme.touchTarget
    implicitHeight: Theme.touchTarget
    radius: Theme.radiusPill
    opacity: root.enabled ? 1.0 : 0.45

    color: {
        if (!root.enabled)
            return Theme.surfaceAlt;
        if (root.primary)
            return mouse.pressed ? Qt.darker(Theme.accent, 1.12) : Theme.accent;
        return mouse.pressed ? Theme.surfaceAlt : "transparent";
    }
    border.width: root.primary ? 0 : 1
    border.color: (mouse.containsMouse && root.enabled) ? Theme.accent : Theme.border

    Icon {
        anchors.centerIn: parent
        name: root.icon
        size: Theme.iconMd
        color: {
            if (!root.enabled)
                return Theme.textDim;
            if (root.primary)
                return Theme.background;
            return mouse.pressed ? Theme.accent : Theme.text;
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

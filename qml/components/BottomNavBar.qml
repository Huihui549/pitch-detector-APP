// 底部导航栏
//
// 界面层组件：只做"选中项切换"的呈现与点击转发，不决定有哪些页面（由 Main.qml 传入）。
// 按手机习惯：四等分、图标位置用文字代替（不引入图标资源，保持零外部依赖）。

import QtQuick
import QtQuick.Controls
import PitchDetector.App 1.0

Rectangle {
    id: root

    /// 导航项：形如 [{ key: "live", label: "实时", badge: "" }, ...]
    property var items: []
    /// 当前选中项的 key
    property string current: ""

    signal selected(string key)

    color: Theme.surface
    height: Theme.navHeight

    Row {
        anchors.fill: parent

        Repeater {
            model: root.items

            delegate: Item {
                id: cell
                width: root.width / Math.max(1, root.items.length)
                height: root.height
                // 让委托拿到当前项
                required property var modelData
                readonly property bool active: root.current === modelData.key

                Rectangle {
                    anchors.fill: parent
                    color: cell.active ? Theme.surfaceAlt : "transparent"
                }

                Column {
                    anchors.centerIn: parent
                    spacing: 3

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: cell.modelData.label
                        color: cell.active ? Theme.accent : Theme.textDim
                        font.pixelSize: Theme.fontNormal
                        font.bold: cell.active
                    }

                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: cell.active
                        width: 20
                        height: 2
                        radius: 1
                        color: Theme.accent
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: root.selected(cell.modelData.key)
                }
            }
        }
    }

    // 顶部细分隔线
    Rectangle {
        anchors.top: parent.top
        width: parent.width
        height: 1
        color: Theme.surfaceAlt
    }
}

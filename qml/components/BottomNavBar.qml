// 底部导航栏（D+E 风格）
//
// 界面层组件：只做"选中项切换"的呈现与点击转发，不决定有哪些页面（由 Main.qml 传入）。
//
// 版式参照：Material 3 的 Navigation bar 惯例（图标在上、标签在下、选中项有胶囊指示底），
// 配色与尺寸全部取 Theme 令牌（D 风格：深底 + 单一强调色）。
// 图标来自 resources/icons（Lucide），由 Icon.qml 着色——**不用符号/emoji 代替图标**。

import QtQuick
import QtQuick.Controls
import PitchDetector.App 1.0

Rectangle {
    id: root

    /// 导航项：形如 [{ key: "live", label: "实时", icon: "activity" }, ...]
    property var items: []
    /// 当前选中项的 key
    property string current: ""

    signal selected(string key)

    color: Theme.surface
    height: Theme.navHeight

    // 顶部细分隔线（Carbon 的"层与层之间用 1px 描边而不是阴影"）
    Rectangle {
        anchors.top: parent.top
        width: parent.width
        height: 1
        color: Theme.border
    }

    Row {
        anchors.fill: parent

        Repeater {
            model: root.items

            delegate: Item {
                id: cell
                required property var modelData
                width: root.width / Math.max(1, root.items.length)
                height: root.height

                readonly property bool active: root.current === modelData.key

                // 选中指示：胶囊底（M3 惯例），颜色用 accent 的低亮态，避免喧宾夺主
                Rectangle {
                    id: pill
                    anchors.horizontalCenter: parent.horizontalCenter
                    // 图标区垂直居中于上半部：给下方标签留位
                    y: Math.round(root.height * 0.18)
                    width: 56
                    height: 30
                    radius: Theme.radiusPill
                    color: cell.active ? Theme.accentDim : "transparent"

                    Behavior on color {
                        ColorAnimation { duration: Theme.durationFast }
                    }
                }

                Column {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: Math.round(root.height * 0.18)
                    spacing: 2

                    Icon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        name: cell.modelData.icon !== undefined ? cell.modelData.icon : ""
                        size: Theme.iconMd
                        color: cell.active ? Theme.accent : Theme.textDim
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: cell.modelData.label
                        color: cell.active ? Theme.accent : Theme.textDim
                        font.pixelSize: Theme.fontMicro
                        font.bold: cell.active
                    }
                }

                // 点击区覆盖整格（≥ Theme.touchTarget 的可点高度）
                MouseArea {
                    anchors.fill: parent
                    onClicked: root.selected(cell.modelData.key)
                }
            }
        }
    }
}

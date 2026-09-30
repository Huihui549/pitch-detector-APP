// 页面滚动容器 —— **所有页面的内容区都必须用它**
//
// 三段结构（ADR-0014）：
//   标题区（Main.qml 的 PageHeader）  ← 固定，不滚动
//   内容区（本组件）                  ← **可上下滑动**
//   底部导航（Main.qml 的 BottomNavBar）← 固定，不滚动
//
// 为什么做成组件：滚动容器要配"细滚动条 + 停止回弹 + 自动隐藏"这几件事，
// 每页手写一遍必然各有差异（风格漂移的常见来源）。这里收口一次，页面只管往里放卡片：
//
//   PageScroller {
//       Card { ... }          // 默认属性直接进内部 ColumnLayout
//       StatCard { ... }
//   }
//
// 说明：内容用 `ColumnLayout`，所以放进去的子项可以照常用 `Layout.fillWidth` 等附加属性。

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PitchDetector.App 1.0

Flickable {
    id: root

    /// 子项默认进内容列（页面里直接写卡片即可）
    default property alias content: column.data

    /// 内容四周留白（与全局栅格一致）
    readonly property int padding: Theme.spacing

    contentWidth: width
    contentHeight: column.implicitHeight + padding * 2
    // 手机上不要橡皮筋回弹：松手后停在原地更符合"看读数"的操作习惯
    boundsBehavior: Flickable.StopAtBounds
    // 内容装得下时禁止拖动（否则会把内容拖出去，露出空白）
    interactive: contentHeight > height
    clip: true

    ColumnLayout {
        id: column
        x: root.padding
        y: root.padding
        // 宽度减掉两侧留白；滚动条可见时再让出它的宽度，避免压住文字
        width: root.width - root.padding * 2 - (bar.visible ? bar.width : 0)
        spacing: Theme.spacing
    }

    ScrollBar.vertical: ScrollBar {
        id: bar
        // 内容装得下就不显示；装不下才出现（细条，不抢视觉）
        policy: root.contentHeight > root.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
        width: Theme.spaceXs

        contentItem: Rectangle {
            implicitWidth: Theme.spaceXs
            radius: Theme.radiusPill
            color: bar.pressed ? Theme.accent : Theme.textDim
            opacity: bar.pressed ? 1.0 : 0.5
        }

        // 不要轨道底色：深色/浅色两套主题下都更干净
        background: Item { }
    }
}

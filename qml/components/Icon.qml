// 图标 —— **所有图标的唯一入口**
//
// 为什么必须有这一层（不是多此一举）：
//   1. **图标必须是白色描边**：Lucide 原生用 `stroke="currentColor"`，而 Qt 的 SVG 渲染器
//      不解析 `currentColor`（会渲染成黑色）。更坑的是 `MultiEffect.colorization` 是
//      **按源亮度**参与混合的：黑色源乘任何颜色仍然是黑——深色主题下表现为"图标几乎看不见"
//      （用户实测反馈）。故入库时已把 `currentColor` 统一替换为 `#ffffff`
//      （见 resources/ATTRIBUTION.md 的"本地修改"一节），白色源才能被染成目标色。
//      这条由 tools/check-theme.ps1 机械把关：resources/icons/*.svg 只允许纯白、不得有其它颜色。
//   2. 图标颜色只允许取 `Theme.*` 令牌——把颜色选择收在一处，风格才可能统一。
//
// 用法：Icon { name: "mic"; size: Theme.iconMd; color: Theme.accent }
// 图标名即 resources/icons/<name>.svg 的文件名（清单见 resources/ATTRIBUTION.md）。

import QtQuick
import QtQuick.Effects
import PitchDetector.App 1.0

Item {
    id: root

    /// 图标名（不含扩展名），对应 qrc:/resources/icons/<name>.svg
    property string name: ""
    /// 边长（正方形）
    property int size: Theme.iconMd
    /// 着色
    property color color: Theme.text

    implicitWidth: size
    implicitHeight: size

    Image {
        id: img
        anchors.fill: parent
        source: root.name.length > 0 ? "qrc:/resources/icons/" + root.name + ".svg" : ""
        // 2× 采样：小尺寸下矢量描边才不会发虚（显示尺寸仍是 root.size）
        sourceSize.width: Math.max(2, root.size * 2)
        sourceSize.height: Math.max(2, root.size * 2)
        fillMode: Image.PreserveAspectFit
        smooth: true
        visible: false
    }

    MultiEffect {
        anchors.fill: img
        source: img
        colorization: 1.0
        colorizationColor: root.color
    }
}

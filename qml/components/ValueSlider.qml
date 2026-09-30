// 数值滑块（令牌化）
//
// 为什么需要它：QtQuick Controls 的 Basic 风格滑块自带一套灰蓝配色与 6px 轨道，
// 与本项目 D+E 令牌（强调色 + 8px 栅格 + 圆角胶囊）不一致；而界面里禁止出现裸色值，
// 所以只能在这一处把 background / handle 换成令牌——其余行为（拖动、键盘、步进）沿用控件实现。
//
// 用法：ValueSlider { from: 30; to: 300; stepSize: 1; value: Metronome.bpm; onMoved: Metronome.bpm = value }

import QtQuick
import QtQuick.Controls
import PitchDetector.App 1.0

Slider {
    id: control

    // 高度给到整块触控区：拖动时手指不会滑到轨道之外
    implicitHeight: Theme.touchTarget
    // 值随拖动实时生效（默认只在松手时发 moved 的语义会让人以为卡住）
    live: true

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        width: control.availableWidth
        height: Theme.spaceXs
        radius: Theme.radiusPill
        color: Theme.surfaceAlt

        // 已填充部分：唯一强调色
        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: Theme.radiusPill
            color: Theme.accent
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + (control.availableHeight - height) / 2
        implicitWidth: Theme.iconMd + Theme.spaceSm
        implicitHeight: Theme.iconMd + Theme.spaceSm
        width: Theme.iconMd + Theme.spaceSm
        height: Theme.iconMd + Theme.spaceSm
        radius: Theme.radiusPill
        color: control.pressed ? Qt.darker(Theme.accent, 1.12) : Theme.accent
        border.width: 2
        border.color: Theme.background
    }
}

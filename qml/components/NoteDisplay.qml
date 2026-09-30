// 主读数（大字音名）—— D 风格的核心信息
//
// 界面层组件：只负责把 C++ 传来的音名画大，不含任何音高判断（分层铁律）。
// 无有效音高时显示 "—" 并转灰——本组件不做"什么算有效"的判断，Session 给什么就画什么。
//
// 版式要点（D：仪表/DAW）
//   · 字号随可用宽度自适应，但**下限取 Theme.fontHuge**：保证任何机型上都是"一眼可读"
//   · 不指定字体族：Android 是 Roboto、Windows 是 Segoe UI，写死会在另一端回退成怪字体
//   · 有效读数时用强调色 + 一个"锁定"状态胶囊（图标 + 文字），弱光下也能分辨"有数/没数"

import QtQuick
import QtQuick.Controls
import PitchDetector.App 1.0

Item {
    id: root

    /// 音名文本（含八度，如 "A4"；无有效音高时为 "—"）
    property string noteText: "—"
    /// 是否处于有效读数状态（决定配色）
    property bool active: noteText !== "—"

    implicitHeight: noteLabel.implicitHeight + hintRow.implicitHeight + Theme.spaceSm
    height: implicitHeight

    Text {
        id: noteLabel
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        text: root.noteText
        color: root.active ? Theme.accent : Theme.textDim
        // 自适应但不小于令牌下限（Theme.fontHuge），上限避免窄屏溢出
        font.pixelSize: Math.max(Theme.fontHuge, Math.min(112, Math.round(root.width * 0.28)))
        font.bold: true

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast }
        }
    }

    Row {
        id: hintRow
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: noteLabel.bottom
        anchors.topMargin: Theme.spaceSm
        spacing: Theme.spaceSm / 2

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: root.active ? "check" : "circle-alert"
            size: Theme.iconSm
            color: root.active ? Theme.ok : Theme.textDim
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.active ? qsTr("已锁定音高（科学音高记号，中央 C = C4）")
                              : qsTr("未检测到稳定音高")
            color: Theme.textDim
            font.pixelSize: Theme.fontMicro
        }
    }
}

// 大字音名显示
//
// 界面层组件：只负责把 C++ 传来的音名画大，不含任何音高判断（分层铁律）。
// 无有效音高时显示 "—"，颜色转灰——本组件不做"什么算有效"的判断，由 Session 给什么就画什么。

import QtQuick
import QtQuick.Controls
import PitchDetector.App 1.0

Item {
    id: root

    /// 音名文本（含八度，如 "A4"；无有效音高时为 "—"）
    property string noteText: "—"
    /// 是否处于有效读数状态（决定配色）
    property bool active: noteText !== "—"

    implicitHeight: noteLabel.implicitHeight + hintLabel.implicitHeight

    Text {
        id: noteLabel
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        text: root.noteText
        color: root.active ? Theme.accent : Theme.textDim
        font.pixelSize: Math.round(root.width * 0.38)
        font.bold: true
        // 音名只有 2–4 个字符，缩小到合适即可
        font.family: "Segoe UI"
    }

    Text {
        id: hintLabel
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: noteLabel.bottom
        text: root.active ? qsTr("科学音高记号（中央 C = C4）") : qsTr("未检测到稳定音高")
        color: Theme.textDim
        font.pixelSize: Theme.fontSmall
    }
}

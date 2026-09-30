// 页头（D+E 风格）
//
// 界面层组件：显示页面标题与可选的状态行。标题区连点 7 次可进调试页（隐藏入口，ADR-0006）。
// 版式：左侧一道强调色短竖条（仪表面板的"指示条"）+ 标题 + 状态行，底部 1px 描边分层。
// 颜色/尺寸全部取 Theme 令牌。

import QtQuick
import PitchDetector.App 1.0

Rectangle {
    id: root

    property string title: ""
    property string subtitle: ""
    /// 连点达到阈值时发出（由 Main.qml 决定做什么）
    signal secretTapped()

    /// 需要连点的次数（ADR-0006：7 次）
    readonly property int secretCount: 7

    property int tapCount: 0

    implicitHeight: content.implicitHeight + Theme.spaceLg
    color: Theme.background

    Column {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Theme.spacing
        anchors.rightMargin: Theme.spacing
        spacing: Theme.spaceXs

        Row {
            spacing: Theme.spaceSm

            // 指示条：D 风格的视觉签名（像仪表面板上的刻度指示）
            Rectangle {
                width: 3
                height: titleText.implicitHeight
                radius: Theme.radiusPill
                color: Theme.accent
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                id: titleText
                text: root.title
                color: Theme.text
                font.pixelSize: Theme.fontTitle
                font.bold: true
            }
        }

        Text {
            visible: root.subtitle.length > 0
            text: root.subtitle
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
            width: parent.width
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: {
            root.tapCount += 1;
            if (root.tapCount >= root.secretCount) {
                root.tapCount = 0;
                root.secretTapped();
            }
        }
    }

    // 连点进度提示：给用户"再点几下就能进"的反馈，否则隐藏入口等于不存在
    Rectangle {
        visible: root.tapCount > 0 && root.tapCount < root.secretCount
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.rightMargin: Theme.spacing
        width: progressText.implicitWidth + Theme.spacing
        height: progressText.implicitHeight + Theme.spaceSm
        radius: Theme.radiusPill
        color: Theme.surfaceAlt
        border.width: 1
        border.color: Theme.border

        Text {
            id: progressText
            anchors.centerIn: parent
            text: qsTr("再点 %1 次").arg(root.secretCount - root.tapCount)
            color: Theme.textDim
            font.pixelSize: Theme.fontMicro
        }
    }

    // 底部分层描边（Carbon 思路：用 1px 线而不是阴影分隔）
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.border
    }
}

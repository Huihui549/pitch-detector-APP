// 页头
//
// 界面层组件：显示页面标题与可选的状态行。标题区连点 7 次可进调试页（隐藏入口，ADR-0006）。

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

    implicitHeight: content.implicitHeight + Theme.spacing * 1.6
    color: Theme.background

    property int tapCount: 0

    Column {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Theme.spacing
        anchors.rightMargin: Theme.spacing
        spacing: 2

        Text {
            text: root.title
            color: Theme.text
            font.pixelSize: Theme.fontTitle
            font.bold: true
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
        width: progressText.implicitWidth + 12
        height: progressText.implicitHeight + 6
        radius: 4
        color: Theme.surfaceAlt

        Text {
            id: progressText
            anchors.centerIn: parent
            text: qsTr("再点 %1 次").arg(root.secretCount - root.tapCount)
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
        }
    }
}

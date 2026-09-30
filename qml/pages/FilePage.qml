// 录音分析页
//
// 两条入口：**选择已有音频**（任意主流格式）或**现场录音**（可暂停/继续/停止清空/保存 mp3）。
// 分析结果用**钢琴卷帘**呈现：左侧真实键位图，右侧音高曲线（纵轴音高、横轴时间/秒），
// 与"导出长图"共用 C++ 里的同一份渲染实现（piano-roll-renderer.cpp）。
//
// 按钮统一用 ActionButton（与实时页同一种风格，只有 primary/次要的区别），文字前带图标。
// 本文件不含任何算法与阈值判断。

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import PitchDetector.App 1.0
import "../components"

Item {
    id: page

    // 卡片：统一"面 + 1px 描边 + 内边距"三件套（与其它页面同一写法）
    component Card: Rectangle {
        id: card
        default property alias content: inner.data
        color: Theme.surface
        border.width: 1
        border.color: Theme.border
        radius: Theme.radius
        implicitHeight: inner.implicitHeight + Theme.spacing * 2

        ColumnLayout {
            id: inner
            anchors.fill: parent
            anchors.margins: Theme.spacing
            spacing: Theme.spaceSm
        }
    }

    PageScroller {
        anchors.fill: parent

        // ---------- 1. 录音 ----------
        Card {
            Layout.fillWidth: true

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm
                Icon {
                    name: "mic"
                    size: Theme.iconSm
                    color: Recorder.recording ? Theme.danger : Theme.textDim
                }
                Text {
                    text: qsTr("录音")
                    color: Theme.textDim
                    font.pixelSize: Theme.fontMicro
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: Recorder.stateText
                    color: Recorder.recording ? Theme.danger : Theme.textDim
                    font.pixelSize: Theme.fontMicro
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm

                ActionButton {
                    Layout.fillWidth: true
                    primary: true
                    icon: "mic"
                    text: qsTr("开始录音")
                    enabled: Recorder.available && !Recorder.recording && !Recorder.paused
                    onClicked: Recorder.start()
                }
                ActionButton {
                    Layout.fillWidth: true
                    primary: false
                    // 暂停中显示"继续"（同一个按钮两种语义，符合用户要求：再点一次继续）
                    icon: Recorder.paused ? "play" : "square"
                    text: Recorder.paused ? qsTr("继续录音") : qsTr("暂停录音")
                    enabled: Recorder.recording || Recorder.paused
                    onClicked: Recorder.pauseOrResume()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm

                ActionButton {
                    Layout.fillWidth: true
                    primary: false
                    icon: "trash-2"
                    text: qsTr("停止并清空")
                    enabled: Recorder.recording || Recorder.paused || Recorder.hasTake
                    // 有未保存的录音时先确认（用户要求：弹出"当前录音未保存，是否清除该录音"）
                    onClicked: Recorder.hasTake ? discardDialog.open() : Recorder.stopAndDiscard()
                }
                ActionButton {
                    Layout.fillWidth: true
                    primary: false
                    icon: "save"
                    text: qsTr("保存录音")
                    // 录音中也可点：会先把文件收尾再复制（见 AudioRecorder::saveAs）
                    enabled: Recorder.hasTake
                    onClicked: Recorder.save()
                }
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("录制格式：%1").arg(Recorder.formatDescription)
                color: Theme.textDim
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                visible: Recorder.notice.length > 0
                text: Recorder.notice
                color: Theme.warn
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }

            // 录音期间的实时读数：**直接绑既有的 Session**（它已处理麦克风权限/设备选择/格式协商），
            // 不在这里再转发一层属性——少一层转发就少一处不同步（R8）。
            RowLayout {
                Layout.fillWidth: true
                visible: Recorder.recording || Recorder.paused
                spacing: Theme.spaceSm

                Icon {
                    name: "activity"
                    size: Theme.iconSm
                    color: Recorder.liveAnalysisActive ? Theme.ok : Theme.warn
                }
                Text {
                    Layout.fillWidth: true
                    text: Recorder.liveAnalysisActive
                          ? qsTr("实时分析：%1　%2 Hz　置信度 %3")
                                .arg(Session.noteName)
                                .arg(Session.frequency > 0 ? Session.frequency.toFixed(1) : "—")
                                .arg(Session.confidence.toFixed(2))
                          : qsTr("实时分析未启动（麦克风可能被独占）：录音继续进行，保存后会自动分析整段")
                    color: Recorder.liveAnalysisActive ? Theme.text : Theme.warn
                    font.pixelSize: Theme.fontMicro
                    wrapMode: Text.WordWrap
                }
            }
        }

        // ---------- 2. 选择音频文件 / 导出 ----------
        Card {
            Layout.fillWidth: true

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm

                ActionButton {
                    Layout.fillWidth: true
                    primary: true
                    icon: "folder-open"
                    text: qsTr("选择音频文件")
                    enabled: !FileAnalysis.analyzing
                    // 走 C++ 的原生 QFileDialog：QML FileDialog 在 Windows 上返回的路径不可靠
                    onClicked: FileAnalysis.chooseAndAnalyze()
                }
                ActionButton {
                    Layout.fillWidth: true
                    primary: false
                    icon: "download"
                    text: qsTr("导出长图")
                    enabled: FileAnalysis.hasResult && !FileAnalysis.analyzing
                    onClicked: FileAnalysis.exportRollImage()
                }
            }

            ActionButton {
                Layout.fillWidth: true
                primary: false
                icon: "x"
                text: qsTr("清空当前结果")
                enabled: FileAnalysis.hasResult && !FileAnalysis.analyzing
                onClicked: FileAnalysis.clearResult()
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("支持 mp3 / wav / m4a / aac / flac 等主流格式（未压缩 WAV 直读，其余交给解码器）。")
                color: Theme.textDim
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }
        }

        // ---------- 3. 进度与状态 ----------
        RowLayout {
            Layout.fillWidth: true
            visible: FileAnalysis.analyzing
            spacing: Theme.spaceSm

            Text {
                text: qsTr("分析中 %1%").arg(Math.round(FileAnalysis.progress * 100))
                color: Theme.text
                font.pixelSize: Theme.fontSmall
            }
            ProgressBar {
                Layout.fillWidth: true
                from: 0
                to: 1
                value: FileAnalysis.progress
            }
            ActionButton {
                primary: false
                icon: "x"
                text: qsTr("取消")
                onClicked: FileAnalysis.cancel()
            }
        }

        Text {
            Layout.fillWidth: true
            visible: !FileAnalysis.analyzing
            text: FileAnalysis.stateText
            color: FileAnalysis.errorText.length > 0 ? Theme.warn : Theme.textDim
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }

        // ---------- 4. 钢琴卷帘（左键位图 + 右音高曲线，横轴时间/秒） ----------
        Card {
            Layout.fillWidth: true

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm
                Icon {
                    name: "waves"
                    size: Theme.iconSm
                    color: Theme.textDim
                }
                Text {
                    text: qsTr("钢琴卷帘")
                    color: Theme.textDim
                    font.pixelSize: Theme.fontMicro
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: FileAnalysis.hasResult
                          ? qsTr("%1 s　·　%2 个半音")
                                .arg(FileAnalysis.durationSec.toFixed(1))
                                .arg(FileAnalysis.highestMidi - FileAnalysis.lowestMidi + 1)
                          : qsTr("尚无数据")
                    color: Theme.textDim
                    font.pixelSize: Theme.fontMicro
                }
            }

            // 横向滚动：卷帘的时间轴通常比屏幕宽（纵向滚动由 PageScroller 负责）
            Flickable {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(FileAnalysis.rollHeightPx, 420)
                contentWidth: FileAnalysis.rollWidthPx
                contentHeight: height
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.HorizontalFlick

                Image {
                    // 图像由 C++ 渲染（image://pianoroll/<版本号>）：QML 不做任何绘制。
                    // **始终显示**：没有数据时 C++ 也会画一张只有键盘/网格/时间轴的空卷帘
                    // （用户要求"卷帘区域应该常显"），这里不做条件分支。
                    source: "image://pianoroll/" + FileAnalysis.rollRevision
                    width: FileAnalysis.rollWidthPx
                    height: FileAnalysis.rollHeightPx
                    fillMode: Image.PreserveAspectFit
                    sourceSize.width: FileAnalysis.rollWidthPx
                    sourceSize.height: FileAnalysis.rollHeightPx
                    smooth: false
                }

                Text {
                    anchors.centerIn: parent
                    visible: !FileAnalysis.hasResult
                    text: qsTr("选择音频文件或录一段音，这里会画出音高曲线")
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall
                }
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("纵轴为音高（左侧是钢琴键位图，C 键带音名），横轴为时间（秒）。"
                           + "中间断开表示那一小段没有检测到音高。左右拖动可看整段。")
                color: Theme.textDim
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }
        }

        // ---------- 5. 摘要 ----------
        Card {
            Layout.fillWidth: true
            visible: FileAnalysis.summary.length > 0

            Text {
                Layout.fillWidth: true
                text: FileAnalysis.summary
                color: Theme.text
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }
        }

        // ---------- 6. 逐帧数据（手机上一屏放不下，给固定高度 + 可滚动） ----------
        Card {
            Layout.fillWidth: true
            visible: FileAnalysis.frameCount > 0

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm
                Icon {
                    name: "file-text"
                    size: Theme.iconSm
                    color: Theme.textDim
                }
                Text {
                    text: qsTr("逐帧数据（%1 帧）").arg(FileAnalysis.frameCount)
                    color: Theme.textDim
                    font.pixelSize: Theme.fontMicro
                }
                Item { Layout.fillWidth: true }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 0
                Text { text: qsTr("时间(s)");  color: Theme.textDim; font.pixelSize: Theme.fontMicro; Layout.preferredWidth: 56 }
                Text { text: qsTr("音名");     color: Theme.textDim; font.pixelSize: Theme.fontMicro; Layout.preferredWidth: 46 }
                Text { text: qsTr("频率(Hz)"); color: Theme.textDim; font.pixelSize: Theme.fontMicro; Layout.fillWidth: true }
                Text { text: qsTr("音分");     color: Theme.textDim; font.pixelSize: Theme.fontMicro; Layout.preferredWidth: 50 }
                Text { text: qsTr("置信");     color: Theme.textDim; font.pixelSize: Theme.fontMicro; Layout.preferredWidth: 42 }
            }

            ListView {
                Layout.fillWidth: true
                Layout.preferredHeight: 150
                clip: true
                model: FileAnalysis.frames
                ScrollBar.vertical: ScrollBar { }

                delegate: RowLayout {
                    width: ListView.view.width
                    spacing: 0
                    Text {
                        text: model.timeSec.toFixed(3)
                        color: Theme.text; font.pixelSize: Theme.fontMicro
                        Layout.preferredWidth: 56
                    }
                    Text {
                        text: model.note
                        color: Theme.accent; font.pixelSize: Theme.fontMicro; font.bold: true
                        Layout.preferredWidth: 46
                    }
                    Text {
                        text: model.freq.toFixed(2)
                        color: Theme.text; font.pixelSize: Theme.fontMicro
                        Layout.fillWidth: true
                    }
                    Text {
                        text: (model.cents >= 0 ? "+" : "") + model.cents.toFixed(1)
                        color: Math.abs(model.cents) <= 5 ? Theme.ok : Theme.text
                        font.pixelSize: Theme.fontMicro
                        Layout.preferredWidth: 50
                    }
                    Text {
                        text: model.confidence.toFixed(2)
                        color: Theme.textDim; font.pixelSize: Theme.fontMicro
                        Layout.preferredWidth: 42
                    }
                }
            }
        }
    }

    // 清除录音前的确认（用户要求：当前录音未保存时，先问一句再清）
    Dialog {
        id: discardDialog

        anchors.centerIn: parent
        modal: true
        title: qsTr("清除录音")
        standardButtons: Dialog.NoButton
        width: Math.min(page.width - Theme.spacing * 4, 320)

        background: Rectangle {
            color: Theme.surface
            radius: Theme.radius
            border.width: 1
            border.color: Theme.border
        }

        contentItem: ColumnLayout {
            spacing: Theme.spacing

            Text {
                Layout.fillWidth: true
                text: qsTr("当前录音未保存，是否清除该录音？")
                color: Theme.text
                font.pixelSize: Theme.fontNormal
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm

                ActionButton {
                    Layout.fillWidth: true
                    primary: false
                    text: qsTr("取消")
                    onClicked: discardDialog.close()
                }
                ActionButton {
                    Layout.fillWidth: true
                    primary: true
                    icon: "trash-2"
                    text: qsTr("清除")
                    onClicked: {
                        Recorder.stopAndDiscard();
                        discardDialog.close();
                    }
                }
            }
        }
    }
}

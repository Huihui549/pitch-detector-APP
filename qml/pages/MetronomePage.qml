// 节拍器页（D+E 风格）
//
// 版式（自上而下）：主控（拍号 + 大号 BPM + 播放） → 拍点与细分 → 速度 → 拍号 → 节拍音 → 说明。
//
// 与市面节拍器的对应与差异（设计取舍）：
//   · 都有：BPM 大号显示 + 滑块调速、拍号预置、播放/停止、当前拍可视指示、强拍与弱拍不同音色
//   · 本项目的差异：**细分是"逐拍"的**（每拍可以各自设 1/2/3/4 等分），
//     而多数 App 只能整小节统一细分。为这条能力，拍点指示块同时充当细分编辑器（点一下切换）。
//   · BPM 的口径：指"每拍"的速度，而"一拍"就是拍号分母那个时值（6/8 时即八分音符）。
//     想要 6/8 的二拍感，就设 2 拍 + 每拍 3 细分——详见说明卡。
//
// 本页**不含任何算法与音频逻辑**：拍号/细分/BPM 的规则、点击声与采样级调度全在 C++（Metronome 单例）。

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import PitchDetector.App 1.0
import "../components"

Item {
    id: page

    // 卡片：统一"面 + 1px 描边 + 内边距"三件套（与其它页面同一写法，避免风格漂移）
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

    // 拍块：**一个控件两种用途**——显示当前拍（指示灯），同时点一下切换该拍的细分（编辑器）。
    // 合在一起的理由：手机上再挤一行专门用于编辑的控制会明显变乱，而"点到哪一拍"正是用户想改的那一拍。
    component BeatBlock: Rectangle {
        id: block

        property int beatIndex: 0
        property int subdivision: 1

        readonly property bool active: Metronome.playing && Metronome.activeBeat === block.beatIndex
        readonly property bool downbeat: block.beatIndex === 0

        implicitWidth: Theme.touchTarget
        implicitHeight: Theme.touchTarget + Theme.spaceSm
        radius: Theme.radiusSm
        color: block.active ? (block.downbeat ? Theme.accent : Theme.accentDim)
                            : Theme.surfaceAlt
        border.width: 1
        border.color: block.active ? Theme.accent : Theme.border

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast }
        }

        Column {
            anchors.centerIn: parent
            spacing: Theme.spaceXs

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: block.beatIndex + 1
                color: block.active ? Theme.background : Theme.text
                font.pixelSize: Theme.fontNormal
                font.bold: true
            }

            // 细分点：几个点 = 这一拍被等分成几份；正在响的那一份用底色反白
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.spaceXs

                Repeater {
                    model: block.subdivision

                    Rectangle {
                        width: Theme.spaceXs
                        height: Theme.spaceXs
                        radius: Theme.radiusPill
                        color: (block.active && index === Metronome.activeSub) ? Theme.background
                                                                              : Theme.textDim
                    }
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: Metronome.cycleSubdivision(block.beatIndex)
        }
    }

    // 内容区：**可上下滑动**（标题区与底部导航由 Main.qml 固定住，ADR-0014）
    PageScroller {
        anchors.fill: parent

        // ---------- 1. 主控 ----------
        Card {
            Layout.fillWidth: true

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm

                Text {
                    text: Metronome.meterLabel
                    color: Theme.text
                    font.pixelSize: Theme.fontTitle
                    font.bold: true
                }
                Text {
                    text: Metronome.tempoTerm
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall
                    Layout.alignment: Qt.AlignBottom
                    Layout.bottomMargin: Theme.spaceXs
                }
                Item { Layout.fillWidth: true }

                // 运行指示：播放中才亮（颜色取自 Theme.ok，与实时页的"采集中"同一语义）
                RowLayout {
                    spacing: Theme.spaceXs
                    Icon {
                        name: Metronome.playing ? "activity" : "square"
                        size: Theme.iconSm
                        color: Metronome.playing ? Theme.ok : Theme.textDim
                    }
                    Text {
                        text: Metronome.playing ? qsTr("播放中") : qsTr("已停止")
                        color: Metronome.playing ? Theme.ok : Theme.textDim
                        font.pixelSize: Theme.fontMicro
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: Metronome.bpm
                color: Theme.text
                font.pixelSize: Theme.fontHuge
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("每分钟拍数（BPM）")
                color: Theme.textDim
                font.pixelSize: Theme.fontMicro
            }

            ActionButton {
                Layout.fillWidth: true
                primary: true
                icon: Metronome.playing ? "square" : "play"
                text: Metronome.playing ? qsTr("停止") : qsTr("开始")
                onClicked: Metronome.toggle()
            }

            Text {
                Layout.fillWidth: true
                text: Metronome.statusText
                color: Theme.textDim
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            // 提示（音色加载失败 / 未装 Multimedia / 上次的自定义音频不见了）
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm
                visible: Metronome.notice.length > 0
                Icon {
                    name: "circle-alert"
                    size: Theme.iconMd
                    color: Theme.warn
                }
                Text {
                    Layout.fillWidth: true
                    text: Metronome.notice
                    color: Theme.warn
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
            }
        }

        // ---------- 2. 拍点与逐拍细分 ----------
        Card {
            Layout.fillWidth: true

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm
                Icon {
                    name: "metronome"
                    size: Theme.iconSm
                    color: Theme.textDim
                }
                Text {
                    text: qsTr("拍点与细分")
                    color: Theme.textDim
                    font.pixelSize: Theme.fontMicro
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: qsTr("点一下切换该拍细分")
                    color: Theme.textDim
                    font.pixelSize: Theme.fontMicro
                }
            }

            Flow {
                Layout.fillWidth: true
                spacing: Theme.spaceSm

                Repeater {
                    model: Metronome.beats

                    BeatBlock {
                        beatIndex: index
                        subdivision: Metronome.subdivisions[index]
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                text: Metronome.patternSummary
                color: Theme.text
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm

                ActionButton {
                    Layout.fillWidth: true
                    primary: false
                    icon: "sliders-horizontal"
                    text: qsTr("全部整拍")
                    onClicked: Metronome.setAllSubdivisions(1)
                }
                ActionButton {
                    Layout.fillWidth: true
                    primary: false
                    icon: "sliders-horizontal"
                    text: qsTr("全部八分")
                    onClicked: Metronome.setAllSubdivisions(2)
                }
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("每个拍块下面的点数 = 这一拍被等分成几份：1 整拍、2 两个八分、3 三连音、4 四个十六分。"
                           + "例如 4/4 想让第 1 拍拆成两个八分、其余保持四分，就把第 1 块点到 2 点。")
                color: Theme.textDim
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }
        }

        // ---------- 3. 速度 ----------
        Card {
            Layout.fillWidth: true

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm
                Icon {
                    name: "activity"
                    size: Theme.iconSm
                    color: Theme.textDim
                }
                Text {
                    text: qsTr("速度")
                    color: Theme.textDim
                    font.pixelSize: Theme.fontMicro
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: qsTr("%1 BPM　%2").arg(Metronome.bpm).arg(Metronome.tempoTerm)
                    color: Theme.text
                    font.pixelSize: Theme.fontMicro
                }
            }

            ValueSlider {
                Layout.fillWidth: true
                from: Metronome.minBpm
                to: Metronome.maxBpm
                stepSize: 1
                snapMode: Slider.SnapAlways
                value: Metronome.bpm
                onMoved: Metronome.bpm = Math.round(value)
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm

                ActionButton {
                    Layout.fillWidth: true
                    primary: false
                    text: qsTr("−5")
                    onClicked: Metronome.nudgeBpm(-5)
                }
                ActionButton {
                    Layout.fillWidth: true
                    primary: false
                    text: qsTr("−1")
                    onClicked: Metronome.nudgeBpm(-1)
                }
                ActionButton {
                    Layout.fillWidth: true
                    primary: false
                    text: qsTr("+1")
                    onClicked: Metronome.nudgeBpm(1)
                }
                ActionButton {
                    Layout.fillWidth: true
                    primary: false
                    text: qsTr("+5")
                    onClicked: Metronome.nudgeBpm(5)
                }
            }

            // 点击测速：跟着感觉连点，程序按最近几次间隔的平均给出速度（市面节拍器的标配功能）
            ActionButton {
                Layout.fillWidth: true
                primary: false
                icon: "pointer"
                text: qsTr("点击测速（跟着感觉连点）")
                onClicked: Metronome.tapTempo()
            }

            Text {
                Layout.fillWidth: true
                visible: Metronome.tapInfo.length > 0
                text: Metronome.tapInfo
                color: Theme.accent
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("范围 %1–%2 BPM。BPM 指「每拍」的速度，而一拍就是拍号分母那个时值。")
                      .arg(Metronome.minBpm).arg(Metronome.maxBpm)
                color: Theme.textDim
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }
        }

        // ---------- 4. 拍号 ----------
        Card {
            Layout.fillWidth: true

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm
                Icon {
                    name: "gauge"
                    size: Theme.iconSm
                    color: Theme.textDim
                }
                Text {
                    text: qsTr("拍号")
                    color: Theme.textDim
                    font.pixelSize: Theme.fontMicro
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: qsTr("当前 %1").arg(Metronome.meterLabel)
                    color: Theme.text
                    font.pixelSize: Theme.fontMicro
                }
            }

            Flow {
                Layout.fillWidth: true
                spacing: Theme.spaceSm

                Repeater {
                    model: Metronome.presetMeters

                    Chip {
                        text: modelData.label
                        selected: Metronome.beats === modelData.beats
                                  && Metronome.unit === modelData.unit
                        onClicked: Metronome.applyMeter(modelData.beats, modelData.unit)
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("换拍号会把每拍细分重置为整拍（避免「新拍数 + 旧细分」的随机组合）。"
                           + "复合拍号如 6/8 默认按八分音符计拍；想要二拍感就用 2 拍 + 每拍 3 细分。")
                color: Theme.textDim
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }
        }

        // ---------- 5. 节拍音 ----------
        Card {
            Layout.fillWidth: true

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm
                Icon {
                    name: "music"
                    size: Theme.iconSm
                    color: Theme.textDim
                }
                Text {
                    text: qsTr("节拍音")
                    color: Theme.textDim
                    font.pixelSize: Theme.fontMicro
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: qsTr("内置音色，或上传自己的音频")
                    color: Theme.textDim
                    font.pixelSize: Theme.fontMicro
                }
            }

            Repeater {
                model: Metronome.soundRoles

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceSm

                    Text {
                        text: modelData.title
                        color: Theme.text
                        font.pixelSize: Theme.fontSmall
                        Layout.preferredWidth: Theme.touchTarget
                    }
                    Text {
                        Layout.fillWidth: true
                        text: modelData.name
                        color: modelData.custom ? Theme.accent : Theme.textDim
                        font.pixelSize: Theme.fontMicro
                        elide: Text.ElideMiddle
                    }

                    IconButton {
                        icon: "play"
                        onClicked: Metronome.previewSound(modelData.role)
                    }
                    IconButton {
                        icon: "folder-open"
                        onClicked: Metronome.chooseSound(modelData.role)
                    }
                    IconButton {
                        icon: "refresh-cw"
                        enabled: modelData.custom
                        onClicked: Metronome.clearSound(modelData.role)
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("三个角色可分别设置：强拍（每小节第一拍）、弱拍（其余拍点）、细分。"
                           + "自定义音频建议用未压缩 WAV；mp3/m4a 需要 Qt Multimedia 解码。"
                           + "手机上请先把文件放到 Download/Music 等公共目录，否则选不到。")
                color: Theme.textDim
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }
        }

        // ---------- 6. 说明与重置 ----------
        Card {
            Layout.fillWidth: true

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceSm
                Icon {
                    name: "info"
                    size: Theme.iconSm
                    color: Theme.textDim
                }
                Text {
                    text: qsTr("说明")
                    color: Theme.textDim
                    font.pixelSize: Theme.fontMicro
                }
                Item { Layout.fillWidth: true }
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("· 点击时刻由音频线程按采样点摆放（48 kHz 下精度约 21 微秒），不依赖界面定时器，"
                           + "因此不会随播放时长漂移。\n"
                           + "· 播放中改速度不会丢拍；换拍号或改细分会从当前拍位立即起一小节。\n"
                           + "· 设置（速度、拍号、细分、自定义音频路径）会记住，重启后保持。")
                color: Theme.textDim
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            // 输出链路诊断：没声音时先看这两行（设备是否打开、数据有没有真的交给声卡）
            Text {
                Layout.fillWidth: true
                text: qsTr("输出：%1").arg(Metronome.outputDescription)
                color: Theme.textDim
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                text: Metronome.engineStats
                color: Theme.textDim
                font.pixelSize: Theme.fontMicro
                wrapMode: Text.WordWrap
            }

            ActionButton {
                Layout.fillWidth: true
                primary: false
                icon: "trash-2"
                text: qsTr("恢复默认设置")
                onClicked: Metronome.resetToDefaults()
            }
        }
    }
}

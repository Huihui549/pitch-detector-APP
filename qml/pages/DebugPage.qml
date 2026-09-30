// 调试页（隐藏入口：标题区连点 7 次）
//
// 配套上游 debug.md 的排查思路：先看"信号有没有问题"（强度、格式），再看"判定有没有问题"
// （置信度、差分曲线谷形）。没有这一页，"为什么测不准"就无从下手。
// 本页只画 C++ 给的数据，不做任何判定；阈值参考线的数值也由 C++ 提供。

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import PitchDetector.App 1.0
import "../components"

Item {
    id: page

    // 内容区：**可上下滑动**（标题区与底部导航由 Main.qml 固定住，不参与滚动，ADR-0014）
    PageScroller {
        anchors.fill: parent

        // ---- 运行环境与采集格式 ----
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: envText.implicitHeight + Theme.spacing
            radius: Theme.radius
            color: Theme.surface

            Text {
                id: envText
                anchors.fill: parent
                anchors.margins: Theme.spacing / 2
                color: Theme.text
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
                text: qsTr("状态：%1\n采集实现：%2\n实际采样率：%3 Hz\n请求/实际格式不一致：%4")
                      .arg(Session.stateText)
                      .arg(Session.sourceDescription.length > 0
                           ? Session.sourceDescription : qsTr("未启动"))
                      .arg(Session.sampleRate)
                      .arg(Session.notice.length > 0 ? Session.notice : qsTr("无"))
            }
        }

        // ---- 存储访问：回答"为什么选不到某些目录里的音频" ----
        // 数据全部来自 C++（Storage 单例）：界面不自己判断权限，也不拼路径字符串。
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: storageCard.implicitHeight + Theme.spacing * 2
            radius: Theme.radius
            color: Theme.surface
            border.width: 1
            border.color: Theme.border

            ColumnLayout {
                id: storageCard
                anchors.fill: parent
                anchors.margins: Theme.spacing
                spacing: Theme.spaceSm

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceSm

                    Icon {
                        name: "folder-open"
                        size: Theme.iconSm
                        color: Theme.textDim
                    }
                    Text {
                        text: qsTr("存储访问")
                        color: Theme.textDim
                        font.pixelSize: Theme.fontMicro
                    }
                    Item { Layout.fillWidth: true }
                }

                ActionButton {
                    Layout.fillWidth: true
                    primary: false
                    icon: "refresh-cw"
                    text: qsTr("重新探测")
                    onClicked: Storage.refresh()
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Android 11 起系统对第三方应用封锁 Android/data 与 Android/obb"
                               + "（实测连「所有文件访问权限」也打不开）。要分析的音频请放在"
                               + "Download / Music / Documents 等公共目录。")
                    color: Theme.textDim
                    font.pixelSize: Theme.fontMicro
                    wrapMode: Text.WordWrap
                }

                Text {
                    Layout.fillWidth: true
                    text: Storage.report
                    color: Theme.textDim
                    font.pixelSize: Theme.fontMicro
                    wrapMode: Text.WrapAnywhere
                }
            }
        }

        // ---- 数值区 ----
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            StatCard {
                Layout.fillWidth: true
                label: qsTr("置信度")
                value: Session.confidence.toFixed(3)
                hint: Session.confidence >= 0.75 ? qsTr("达标（≥0.75 才刷新读数）")
                                                 : qsTr("未达标：信号太弱或不是单音")
                valueColor: Session.confidence >= 0.75 ? Theme.ok : Theme.warn
            }

            StatCard {
                Layout.fillWidth: true
                label: qsTr("RMS")
                value: Session.rms.toFixed(5)
                hint: qsTr("低于门槛时读数显示 —")
            }
        }

        LevelMeter {
            Layout.fillWidth: true
            rms: Session.rms
        }

        // ---- 谷值曲线：判断"是否跳八度"的决定性视图 ----
        Text {
            Layout.fillWidth: true
            text: qsTr("归一化差分曲线（纵轴越低越像周期；谷底对应所选周期 τ）")
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }

        Canvas {
            id: yinCanvas
            Layout.fillWidth: true
            // 滚动容器里 `fillHeight` 无意义（高度由内容决定），给固定高度
            Layout.preferredHeight: 190

            // 界面只做"画"：数据来自 Session.yinCurve（[{tau, cmnd}, ...]）
            property var curve: Session.yinCurve

            onCurveChanged: requestPaint()
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()

            onPaint: {
                var ctx = getContext("2d");
                ctx.reset();
                ctx.fillStyle = Theme.surface;
                ctx.fillRect(0, 0, width, height);

                var data = curve;
                if (!data || data.length < 2) {
                    ctx.fillStyle = Theme.textDim;
                    ctx.font = (Theme.fontSmall + 2) + "px sans-serif";
                    ctx.textAlign = "center";
                    ctx.fillText(qsTr("暂无数据（开始监听后出现）"), width / 2, height / 2);
                    return;
                }

                // 阈值参考线：数值来自 C++（Session.yinThreshold），界面不自己定义阈值
                var threshold = Session.yinThreshold;
                var maxY = 0.6;   // 画到 0.6 足够看清谷形（纯显示量程，不是判定参数）
                ctx.strokeStyle = Theme.warn;
                ctx.setLineDash([4, 4]);
                ctx.beginPath();
                var ty = height - (threshold / maxY) * height;
                ctx.moveTo(0, ty);
                ctx.lineTo(width, ty);
                ctx.stroke();
                ctx.setLineDash([]);

                var tauMin = data[0].tau;
                var tauMax = data[data.length - 1].tau;
                var span = Math.max(1, tauMax - tauMin);

                ctx.strokeStyle = Theme.accent;
                ctx.lineWidth = 1.5;
                ctx.beginPath();
                for (var i = 0; i < data.length; ++i) {
                    var x = (data[i].tau - tauMin) / span * width;
                    var v = Math.min(maxY, Math.max(0, data[i].cmnd));
                    var y = height - (v / maxY) * height;
                    if (i === 0) ctx.moveTo(x, y);
                    else ctx.lineTo(x, y);
                }
                ctx.stroke();

                ctx.fillStyle = Theme.textDim;
                ctx.font = Theme.fontSmall + "px sans-serif";
                ctx.textAlign = "left";
                ctx.fillText("τ = " + tauMin, 4, height - 4);
                ctx.textAlign = "right";
                ctx.fillText("τ = " + tauMax, width - 4, height - 4);
            }
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("怎么看：选中 τ 应落在最深谷；若其右侧还有更深的谷，说明选的是倍周期（频率被判低一个八度）。")
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }
    }
}

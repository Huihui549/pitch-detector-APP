// 主窗口：单页 + 底部导航（手机形态）
//
// 布局决策（用户 2026-09-28 拍板，ADR-0006；2026-09-30 加入节拍器页，共 5 个导航位）：
//   · 单页 + 底部导航：实时 / 节拍器 / 文件 / 音域 / 更多
//   · 调试页是**隐藏入口**（标题区连点 7 次），不占导航位
//   · 窗口按手机比例（400×860），桌面运行时看到的就是手机布局；
//     宽屏适配属第二优先，首版只保证手机布局在桌面可用
//
// 本文件不含任何算法与判定，只做导航与状态显示。

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import PitchDetector
import PitchDetector.App 1.0

ApplicationWindow {
    id: window

    visible: true
    title: qsTr("音高检测")
    width: Theme.phoneWidth
    height: Theme.phoneHeight
    minimumWidth: 360
    minimumHeight: 620
    color: Theme.background

    // 导航项：icon 取 resources/icons 里的文件名（Lucide 图标集，清单见 ATTRIBUTION.md）
    readonly property var navItems: [
        { key: "live", label: qsTr("实时"), icon: "activity" },
        { key: "metro", label: qsTr("节拍器"), icon: "metronome" },
        { key: "file", label: qsTr("文件"), icon: "file-audio" },
        { key: "range", label: qsTr("音域"), icon: "gauge" },
        { key: "more", label: qsTr("更多"), icon: "ellipsis" }
    ]

    property string currentPage: "live"
    /// 进调试页前的页面，用于返回
    property string pageBeforeDebug: "live"

    /// 顶部全局提示：任何页面都看得到（例如"未装 Multimedia"）
    function globalNotice() {
        if (Session.unavailableReason.length > 0 && !Session.running)
            return Session.unavailableReason;
        return "";
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PageHeader {
            id: header
            Layout.fillWidth: true
            title: window.titleForPage(window.currentPage)
            subtitle: window.globalNotice()
            onSecretTapped: window.openDebug()
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: window.indexForPage(window.currentPage)

            LivePage { }
            MetronomePage { }
            FilePage { }
            RangePage { }
            MorePage { }
            DebugPage { }
        }

        BottomNavBar {
            Layout.fillWidth: true
            items: window.navItems
            current: window.currentPage
            onSelected: function (key) {
                window.currentPage = key;
            }
        }
    }

    function indexForPage(key) {
        switch (key) {
        case "live": return 0;
        case "metro": return 1;
        case "file": return 2;
        case "range": return 3;
        case "more": return 4;
        case "debug": return 5;
        default: return 0;
        }
    }

    function titleForPage(key) {
        switch (key) {
        case "live": return qsTr("实时音高");
        case "metro": return qsTr("节拍器");
        case "file": return qsTr("录音分析");
        case "range": return qsTr("音域测量");
        case "more": return qsTr("更多");
        case "debug": return qsTr("调试（长按退出）");
        default: return qsTr("音高检测");
        }
    }

    function openDebug() {
        if (window.currentPage !== "debug") {
            window.pageBeforeDebug = window.currentPage;
        }
        window.currentPage = "debug";
    }
}

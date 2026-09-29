// 音高轨迹曲线
//
// 界面层组件：把 (t, freq) 点列画成折线。**不含任何音高判断**——
// 参考线位置由调用方（页面）从 C++ 给的音名/频率算出，组件只负责画。

import QtQuick
import PitchDetector.App 1.0

Canvas {
    id: root

    /// 点列：[{ t: 秒, freq: Hz }, ...]
    property var points: []
    /// 纵轴范围（Hz）
    property real freqMin: 0
    property real freqMax: 0
    /// 参考线频率列表（如当前音符的等程律频率）
    property var referenceLines: []
    /// 折线颜色
    property color lineColor: Theme.accent

    onPointsChanged: requestPaint()
    onReferenceLinesChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    /// 内部：把频率映射到画布 y
    function yOf(freq) {
        var lo = freqMin;
        var hi = freqMax;
        if (!(hi > lo)) {
            return height / 2;
        }
        var ratio = (freq - lo) / (hi - lo);
        ratio = Math.max(0, Math.min(1, ratio));
        // 留 6 px 上下边距，避免线贴边看不清
        return height - 6 - ratio * (height - 12);
    }

    /// 内部：按点数与画布宽度决定横轴步长（点多时抽稀，避免每像素画几千段）
    function stepFor(count) {
        if (count <= 1) {
            return 1;
        }
        return Math.max(1, Math.ceil(count / Math.max(1, width)));
    }

    onPaint: {
        var ctx = getContext("2d");
        ctx.reset();
        ctx.clearRect(0, 0, width, height);

        // 背景
        ctx.fillStyle = Theme.surface;
        ctx.fillRect(0, 0, width, height);

        // 参考线（虚线）
        if (referenceLines && referenceLines.length > 0) {
            ctx.strokeStyle = Theme.textDim;
            ctx.lineWidth = 1;
            ctx.setLineDash([4, 4]);
            for (var r = 0; r < referenceLines.length; ++r) {
                var ry = yOf(referenceLines[r]);
                ctx.beginPath();
                ctx.moveTo(0, ry);
                ctx.lineTo(width, ry);
                ctx.stroke();
            }
            ctx.setLineDash([]);
        }

        if (!points || points.length < 2) {
            // 无数据时给一行提示，而不是空白画布（避免看起来像坏了）
            ctx.fillStyle = Theme.textDim;
            ctx.font = (Theme.fontSmall + 2) + "px sans-serif";
            ctx.textAlign = "center";
            ctx.fillText(qsTr("暂无曲线数据"), width / 2, height / 2);
            return;
        }

        var tMin = points[0].t;
        var tMax = points[points.length - 1].t;
        var tSpan = Math.max(1e-6, tMax - tMin);

        ctx.strokeStyle = lineColor;
        ctx.lineWidth = 2;
        ctx.beginPath();
        var step = stepFor(points.length);
        var started = false;
        for (var i = 0; i < points.length; i += step) {
            var px = (points[i].t - tMin) / tSpan * width;
            var py = yOf(points[i].freq);
            if (!started) {
                ctx.moveTo(px, py);
                started = true;
            } else {
                ctx.lineTo(px, py);
            }
        }
        // 保证末点一定画上（抽稀可能跳过）
        if (started) {
            var last = points[points.length - 1];
            ctx.lineTo((last.t - tMin) / tSpan * width, yOf(last.freq));
        }
        ctx.stroke();
    }
}

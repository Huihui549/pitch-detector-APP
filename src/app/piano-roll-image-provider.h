// 钢琴卷帘的图像提供者（把 C++ 渲染结果交给 QML）
//
// 为什么走 QQuickImageProvider 而不是在 QML 里画：
//   · 卷帘要**同时**用于"屏幕显示"和"导出长图"，两套绘制必然漂移；这里 C++ 只画一份，
//     QML 拿到同一张图（导出时只是把宽度上限放大，见 FileAnalysisController::renderRollImage）。
//   · 长图（上万像素宽）在 QML 的 Canvas/grabToImage 下既慢又受纹理尺寸限制。
//
// URL 形如 `image://pianoroll/<revision>`：revision 由控制器在结果变化时自增，
// 于是 QML 只写一条绑定就能"结果变了自动换图"，不需要任何手动刷新逻辑。
//
// 编码要求（坑 A20）：本文件必须存为 UTF-8 **无 BOM**。

#pragma once

#include <QQuickImageProvider>

namespace pitch {

class FileAnalysisController;

class PianoRollImageProvider : public QQuickImageProvider {
public:
    /// @param source 结果来源（生命周期由 main.cpp 保证长于引擎）
    explicit PianoRollImageProvider(FileAnalysisController* source);

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
    FileAnalysisController* m_source = nullptr;
};

} // namespace pitch

#include "piano-roll-image-provider.h"

#include "file-analysis-controller.h"

namespace pitch {

PianoRollImageProvider::PianoRollImageProvider(FileAnalysisController* source)
    : QQuickImageProvider(QQuickImageProvider::Image), m_source(source) {
    // 图像只取决于控制器的结果与主题，不做按需缩放：requestedSize 一律忽略（见 requestImage）
}

QImage PianoRollImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize) {
    Q_UNUSED(id)
    Q_UNUSED(requestedSize)

    if (m_source == nullptr) {
        return {};
    }
    // 用**显示档**的宽度上限渲染：屏幕上的图最终是一张 GPU 纹理，手机上纹理上限常为 4096，
    // 超过会直接不显示（比"糊"更糟）。导出走的是另一档上限（见 exportRollImage）。
    const QImage image = m_source->renderRollImage(FileAnalysisController::kDisplayMaxWidthPx,
                                                  FileAnalysisController::kDisplayPxPerSecond);
    if (size != nullptr) {
        *size = image.size();
    }
    return image;
}

} // namespace pitch

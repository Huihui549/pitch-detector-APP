// 应用图标生成器（SVG → 多尺寸 PNG → Windows 多尺寸 ICO）
//
// 为什么要真做一个工具：`resources/branding/app-icon.svg` 是"给人看"的源文件，
// 而 Windows 的可执行文件只认 `.ico`（多尺寸位图容器）。此前 ATTRIBUTION 里承诺的
// `tools/gen-app-icons.ps1` **并不存在**，于是 exe 一直带着默认图标——留承诺不如留工具。
//
// 为什么自己组装 ICO 而不依赖 Qt 的 ico 写入插件：Qt 的 ICO 写入只写单张图，
// 而桌面图标需要在 16/24/32/48/64/128/256 每个尺寸下都清晰（Explorer 按场景各取一档）。
// ICO 容器本身很简单（头 + 目录项 + 各尺寸图像数据），这里用 **PNG 载荷**（Vista+ 支持），
// 于是每一档都是本工具从 SVG 重新渲染的，不是把大图缩小——小尺寸才不糊。
//
// 用法（在仓库根执行）：
//   bin\icon-gen.exe --svg resources\branding\app-icon.svg --ico resources\branding\app-icon.ico
//                    [--png-dir <目录>]        # 额外导出各尺寸 PNG（备用素材）
//
// 编码要求（坑 A20）：本文件必须存为 UTF-8 **无 BOM**。

#include <QBuffer>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <QTextStream>

#include <array>
#include <vector>

namespace {

/// 需要生成的尺寸：前几个是 Windows 常见档位，最后两个给高 DPI 与"大图标"视图
constexpr std::array<int, 7> kSizes{16, 24, 32, 48, 64, 128, 256};

/// 把一张 QImage 编码成 PNG 字节
QByteArray encodePng(const QImage& image) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

/// 在指定尺寸下重新渲染 SVG（**不是**把大图缩放，小尺寸才锐利）
QImage renderSvg(const QString& svgPath, int size) {
    QSvgRenderer renderer(svgPath);
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (renderer.isValid()) {
        renderer.render(&painter, QRectF(0, 0, size, size));
    }
    painter.end();
    image.setDevicePixelRatio(1.0);
    return image;
}

void putU16(QByteArray& out, quint16 value) {
    out.append(static_cast<char>(value & 0xFF));
    out.append(static_cast<char>((value >> 8) & 0xFF));
}

void putU32(QByteArray& out, quint32 value) {
    out.append(static_cast<char>(value & 0xFF));
    out.append(static_cast<char>((value >> 8) & 0xFF));
    out.append(static_cast<char>((value >> 16) & 0xFF));
    out.append(static_cast<char>((value >> 24) & 0xFF));
}

/// 组装多尺寸 ICO（PNG 载荷）。宽/高为 256 时目录项里写 0（ICO 格式约定）。
bool writeIco(const QString& path, const std::vector<QImage>& images, QString* errorOut) {
    QByteArray payloads;
    QByteArray dir;
    for (const QImage& image : images) {
        const QByteArray png = encodePng(image);
        if (png.isEmpty()) {
            if (errorOut != nullptr) {
                *errorOut = QStringLiteral("PNG 编码失败（尺寸 %1）").arg(image.width());
            }
            return false;
        }
        const int size = image.width();
        dir.append(static_cast<char>(size >= 256 ? 0 : size));   // 宽
        dir.append(static_cast<char>(size >= 256 ? 0 : size));   // 高
        dir.append(static_cast<char>(0));                        // 调色板数
        dir.append(static_cast<char>(0));                        // 保留
        putU16(dir, 1);                                          // 色彩平面
        putU16(dir, 32);                                         // 位深
        putU32(dir, static_cast<quint32>(png.size()));
        putU32(dir, static_cast<quint32>(6 + 16 * images.size() + payloads.size()));
        payloads.append(png);
    }

    QByteArray file;
    putU16(file, 0);                                       // 保留
    putU16(file, 1);                                       // 类型 1 = 图标
    putU16(file, static_cast<quint16>(images.size()));     // 图像数
    file.append(dir);
    file.append(payloads);

    QFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("无法写入 %1：%2").arg(path, out.errorString());
        }
        return false;
    }
    const qint64 written = out.write(file);
    out.close();
    if (written != file.size()) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("写入不完整：%1/%2 字节").arg(written).arg(file.size());
        }
        return false;
    }
    if (errorOut != nullptr) {
        errorOut->clear();
    }
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("icon-gen"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("把品牌 SVG 渲染成多尺寸 PNG 与 Windows 多尺寸 ICO"));
    parser.addHelpOption();
    QCommandLineOption svgOption(QStringLiteral("svg"), QStringLiteral("源 SVG 路径"),
                                 QStringLiteral("path"));
    QCommandLineOption icoOption(QStringLiteral("ico"), QStringLiteral("输出 ICO 路径"),
                                 QStringLiteral("path"));
    QCommandLineOption pngDirOption(QStringLiteral("png-dir"),
                                    QStringLiteral("额外导出各尺寸 PNG 的目录（可选）"),
                                    QStringLiteral("dir"));
    parser.addOption(svgOption);
    parser.addOption(icoOption);
    parser.addOption(pngDirOption);
    parser.process(app);

    QTextStream out(stdout);
    const QString svgPath = parser.value(svgOption);
    const QString icoPath = parser.value(icoOption);
    if (svgPath.isEmpty() || !QFileInfo::exists(svgPath)) {
        out << "[FAIL] 源 SVG 不存在：" << svgPath << "\n";
        return 2;
    }

    std::vector<QImage> images;
    images.reserve(kSizes.size());
    for (const int size : kSizes) {
        const QImage image = renderSvg(svgPath, size);
        if (image.isNull()) {
            out << "[FAIL] 渲染失败（尺寸 " << size << "）\n";
            return 1;
        }
        images.push_back(image);
    }

    // 自检：至少要有一档画出了内容（全透明说明 SVG 没解析成功）
    const QImage& largest = images.back();
    int opaque = 0;
    for (int y = 0; y < largest.height(); y += 4) {
        for (int x = 0; x < largest.width(); x += 4) {
            if (qAlpha(largest.pixel(x, y)) > 8) {
                ++opaque;
            }
        }
    }
    out << "源：" << svgPath << "（" << largest.width() << "×" << largest.height() << "）\n";
    out << "不透明采样点数：" << opaque << "（0 = SVG 没画出来）\n";
    if (opaque == 0) {
        out << "[FAIL] 渲染结果是空的（检查 SVG 是否合法）\n";
        return 1;
    }

    if (!parser.value(pngDirOption).isEmpty()) {
        const QString dir = parser.value(pngDirOption);
        QDir().mkpath(dir);
        for (const QImage& image : images) {
            const QString p = QStringLiteral("%1/app-icon-%2.png").arg(dir).arg(image.width());
            if (!image.save(p, "PNG")) {
                out << "[FAIL] PNG 写出失败：" << p << "\n";
                return 1;
            }
        }
        out << "已导出 PNG：" << images.size() << " 个 → " << dir << "\n";
    }

    if (icoPath.isEmpty()) {
        out << "[FAIL] 未指定 --ico 输出路径\n";
        return 2;
    }
    QDir().mkpath(QFileInfo(icoPath).absolutePath());
    QString error;
    if (!writeIco(icoPath, images, &error)) {
        out << "[FAIL] " << error << "\n";
        return 1;
    }
    const QFileInfo icoInfo(icoPath);
    out << "已写出 ICO：" << icoPath << "（" << kSizes.size() << " 个尺寸，"
        << icoInfo.size() << " 字节）\n";
    out << "[PASS] 图标生成完成\n";
    return 0;
}

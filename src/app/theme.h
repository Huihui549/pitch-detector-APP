// 界面主题（配色与尺寸）—— 以 C++ 单例暴露给 QML
//
// 为什么用 C++ 单例而不是 QML 的 `pragma Singleton` + qmldir：
//   本项目的构建系统是 qmake（没有 qt_add_qml_module）。qmake 配置下 QML 模块的
//   命名类型（含单例声明）在**资源 + 手写 qmldir** 这条路上无法被解析：
//   实测界面能创建、但整份 QML 里 `Theme.xxx` 全部报 ReferenceError（230 行），
//   而同样的 qmldir 写法在 CMake 的 qt_add_qml_module 下是正常的。
//   改为 C++ 单例后与 Session / FileAnalysis 走同一条注册路径（qmlRegisterSingletonInstance），
//   不再依赖 qmldir 的模块类型表，事实来源唯一且已验证可用。
//
// 分层说明：这里只有**排版与配色常量**，不含任何算法参数（算法参数在 src/core）。
// 界面里出现算法数值（音域、阈值、窗长）同样违规，由 tools/check-layering.ps1 把关。

#pragma once

#include <QColor>
#include <QObject>

namespace pitch {

/// 界面主题单例：QML 中以 `Theme.xxx` 访问。
class ThemeProvider : public QObject {
    Q_OBJECT

public:
    explicit ThemeProvider(QObject* parent = nullptr) : QObject(parent) {}

    // ---- 配色（深色底：练琴环境多为室内，深色不刺眼且大字对比强）----
    Q_PROPERTY(QColor background READ background CONSTANT)
    Q_PROPERTY(QColor surface READ surface CONSTANT)
    Q_PROPERTY(QColor surfaceAlt READ surfaceAlt CONSTANT)
    Q_PROPERTY(QColor text READ text CONSTANT)
    Q_PROPERTY(QColor textDim READ textDim CONSTANT)
    Q_PROPERTY(QColor accent READ accent CONSTANT)
    Q_PROPERTY(QColor ok READ ok CONSTANT)
    Q_PROPERTY(QColor warn READ warn CONSTANT)
    Q_PROPERTY(QColor danger READ danger CONSTANT)

    // ---- 字号 ----
    Q_PROPERTY(int fontSmall READ fontSmall CONSTANT)
    Q_PROPERTY(int fontNormal READ fontNormal CONSTANT)
    Q_PROPERTY(int fontTitle READ fontTitle CONSTANT)
    Q_PROPERTY(int fontHuge READ fontHuge CONSTANT)

    // ---- 尺寸 ----
    Q_PROPERTY(int radius READ radius CONSTANT)
    Q_PROPERTY(int spacing READ spacing CONSTANT)
    Q_PROPERTY(int navHeight READ navHeight CONSTANT)
    Q_PROPERTY(int phoneWidth READ phoneWidth CONSTANT)
    Q_PROPERTY(int phoneHeight READ phoneHeight CONSTANT)

    QColor background() const { return QColor(QStringLiteral("#11141a")); }
    QColor surface() const { return QColor(QStringLiteral("#1b2028")); }
    QColor surfaceAlt() const { return QColor(QStringLiteral("#252c37")); }
    QColor text() const { return QColor(QStringLiteral("#e8edf4")); }
    QColor textDim() const { return QColor(QStringLiteral("#8b96a5")); }
    QColor accent() const { return QColor(QStringLiteral("#4cc2ff")); }
    QColor ok() const { return QColor(QStringLiteral("#4cd97b")); }
    QColor warn() const { return QColor(QStringLiteral("#ffb454")); }
    QColor danger() const { return QColor(QStringLiteral("#ff6b6b")); }

    int fontSmall() const { return 12; }
    int fontNormal() const { return 15; }
    int fontTitle() const { return 19; }
    int fontHuge() const { return 64; }

    int radius() const { return 10; }
    int spacing() const { return 12; }
    int navHeight() const { return 60; }

    /// 手机基准尺寸：桌面运行时窗口取此比例，保证看到的就是手机布局（ADR-0010）
    int phoneWidth() const { return 400; }
    int phoneHeight() const { return 860; }
};

} // namespace pitch

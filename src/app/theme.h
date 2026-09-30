// 界面主题：配色 / 排版 / 尺寸令牌 —— 以 C++ 单例暴露给 QML
//
// ===================== 风格定稿：D + E（ADR-0012） =====================
//   · D = 仪表/DAW **深色**高对比：近黑底 + 单一强调色 + 超大读数；弱光可读、一眼看清
//   · E = 借用 IBM Carbon 的**规则**：8px 栅格、克制的灰阶、状态色有明确语义
//   参照：https://carbondesignsystem.com/elements/color/overview/
//         https://m3.material.io/  （仅取"底部导航/触控尺寸"这类平台惯例）
//
// ===================== 两套配色（ADR-0013） =====================
//   · `dark`（默认）：上面的 D 风格深色
//   · `light`：白底浅色，用于强光/白天场景；**同一套令牌名**，只换取值
//   切换入口在「更多 → 外观」；选择记进 QSettings（`ui/themeMode`），重启后保持。
//   为什么颜色属性是 `NOTIFY modeChanged` 而不是 CONSTANT：QML 里所有 `Theme.xxx`
//   都是绑定，带 NOTIFY 才能在切换时自动重算刷新——CONSTANT 属性切了不会重绘。
//
// ===================== 铁律：QML 里没有第二处颜色 =====================
//   风格统一不能靠记性。规则是：**所有颜色/字号/间距/圆角只能引用这里的令牌**，
//   `qml/` 里禁止出现十六进制色值、`Qt.rgba(`、裸像素常量 —— 由 tools/check-theme.ps1
//   机械把关（新增令牌后要更新该脚本的说明与白名单）。
//
// ===================== 为什么用 C++ 单例而不是 QML 单例 =====================
//   本项目的构建系统是 qmake（没有 qt_add_qml_module）。qmake 配置下 QML 模块的
//   命名类型（含 `pragma Singleton`）在"资源 + 手写 qmldir"这条路上无法被解析：
//   实测界面能创建、但整份 QML 里 `Theme.xxx` 全部报 ReferenceError（230 行），
//   而同样的 qmldir 写法在 CMake 的 qt_add_qml_module 下是正常的。
//   改为 C++ 单例后与 Session / FileAnalysis 走同一条注册路径（qmlRegisterSingletonInstance）。
//
//   分层说明：这里只有**排版与配色常量**，不含任何算法参数（算法参数在 src/core）。

#pragma once

#include <QColor>
#include <QObject>
#include <QSettings>
#include <QString>

namespace pitch {

/// 界面主题单例：QML 中以 `Theme.xxx` 访问；`Theme.mode = "light"` 可切换（会持久化）。
class ThemeProvider : public QObject {
    Q_OBJECT

public:
    ThemeProvider(QObject* parent = nullptr) : QObject(parent) {
        // 读回上次的选择（QSettings 用 main() 里设置的 organizationName / applicationName）
        QSettings settings;
        setModeInternal(settings.value(QStringLiteral("ui/themeMode"), QStringLiteral("dark")).toString(),
                        false);
    }

    // ---------------- 主题模式 ----------------
    /// "dark"（默认）或 "light"；写入即持久化（QSettings `ui/themeMode`）
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    /// 便捷判断（QML 里 `Theme.dark` 比比较字符串更清爽）
    Q_PROPERTY(bool dark READ dark NOTIFY modeChanged)

    // ---------------- 配色（随主题切换，故带 NOTIFY）----------------
    Q_PROPERTY(QColor background READ background NOTIFY modeChanged)   ///< 页面底
    Q_PROPERTY(QColor surface READ surface NOTIFY modeChanged)         ///< 卡片 / 导航底
    Q_PROPERTY(QColor surfaceAlt READ surfaceAlt NOTIFY modeChanged)   ///< 次级面（选中态、输入框、轨道）
    Q_PROPERTY(QColor border READ border NOTIFY modeChanged)           ///< 描边与分隔线
    Q_PROPERTY(QColor text READ text NOTIFY modeChanged)               ///< 主文字
    Q_PROPERTY(QColor textDim READ textDim NOTIFY modeChanged)         ///< 次要文字
    Q_PROPERTY(QColor accent READ accent NOTIFY modeChanged)           ///< **唯一**强调色
    Q_PROPERTY(QColor accentDim READ accentDim NOTIFY modeChanged)     ///< 强调色的低亮态（选中底/描边）
    Q_PROPERTY(QColor ok READ ok NOTIFY modeChanged)
    Q_PROPERTY(QColor warn READ warn NOTIFY modeChanged)
    Q_PROPERTY(QColor danger READ danger NOTIFY modeChanged)

    // ---------------- 字号（与主题无关，保持 CONSTANT）----------------
    Q_PROPERTY(int fontMicro READ fontMicro CONSTANT)        ///< 11 角标/单位
    Q_PROPERTY(int fontSmall READ fontSmall CONSTANT)        ///< 13 说明文字
    Q_PROPERTY(int fontNormal READ fontNormal CONSTANT)      ///< 15 正文/按钮
    Q_PROPERTY(int fontTitle READ fontTitle CONSTANT)        ///< 20 页面标题、卡片标题
    Q_PROPERTY(int fontDisplay READ fontDisplay CONSTANT)    ///< 30 次级读数（Hz）
    Q_PROPERTY(int fontHuge READ fontHuge CONSTANT)          ///< 72 主读数（音名）

    // ---------------- 间距（8px 栅格 + 4）----------------
    Q_PROPERTY(int spaceXs READ spaceXs CONSTANT)            ///< 4
    Q_PROPERTY(int spaceSm READ spaceSm CONSTANT)            ///< 8
    Q_PROPERTY(int spacing READ spacing CONSTANT)            ///< 12（默认栅格，保留原名字）
    Q_PROPERTY(int spaceLg READ spaceLg CONSTANT)            ///< 24
    Q_PROPERTY(int spaceXl READ spaceXl CONSTANT)            ///< 32

    // ---------------- 圆角 ----------------
    Q_PROPERTY(int radiusSm READ radiusSm CONSTANT)          ///< 8 小控件
    Q_PROPERTY(int radius READ radius CONSTANT)              ///< 12 卡片（保留原名字）
    Q_PROPERTY(int radiusLg READ radiusLg CONSTANT)          ///< 18 大面板
    Q_PROPERTY(int radiusPill READ radiusPill CONSTANT)      ///< 999 胶囊

    // ---------------- 尺寸 ----------------
    Q_PROPERTY(int navHeight READ navHeight CONSTANT)        ///< 64 底部导航高度
    Q_PROPERTY(int touchTarget READ touchTarget CONSTANT)    ///< 48 最小点击区
    Q_PROPERTY(int iconSm READ iconSm CONSTANT)              ///< 18
    Q_PROPERTY(int iconMd READ iconMd CONSTANT)              ///< 24（图标默认尺寸）
    Q_PROPERTY(int iconLg READ iconLg CONSTANT)              ///< 32
    Q_PROPERTY(int phoneWidth READ phoneWidth CONSTANT)      ///< 400（ADR-0010）
    Q_PROPERTY(int phoneHeight READ phoneHeight CONSTANT)    ///< 860（ADR-0010）

    // ---------------- 动效时长（毫秒）----------------
    Q_PROPERTY(int durationFast READ durationFast CONSTANT)  ///< 120 状态色/透明度
    Q_PROPERTY(int durationBase READ durationBase CONSTANT)  ///< 200 位移/展开

    QString mode() const { return m_dark ? QStringLiteral("dark") : QStringLiteral("light"); }
    bool dark() const { return m_dark; }
    void setMode(const QString& mode) { setModeInternal(mode, true); }

    /// 只改内存、**不落盘**：供命令行（`--theme`）与自动化截图使用，
    /// 避免测试脚本悄悄改掉用户的外观偏好。
    Q_INVOKABLE void applyMode(const QString& mode) { setModeInternal(mode, false); }

    // ---------------- dark（D 风格：近黑 + 青绿）----------------
    // ---------------- light（白底 + 深青绿，同一套令牌名）----------------
    QColor background() const { return m_dark ? QColor(QStringLiteral("#0b0f14"))
                                             : QColor(QStringLiteral("#f6f8fa")); }
    QColor surface() const { return m_dark ? QColor(QStringLiteral("#121821"))
                                          : QColor(QStringLiteral("#ffffff")); }
    QColor surfaceAlt() const { return m_dark ? QColor(QStringLiteral("#1a222c"))
                                             : QColor(QStringLiteral("#eef1f6")); }
    QColor border() const { return m_dark ? QColor(QStringLiteral("#26303c"))
                                         : QColor(QStringLiteral("#d7dee8")); }
    QColor text() const { return m_dark ? QColor(QStringLiteral("#f2f6fa"))
                                        : QColor(QStringLiteral("#0f1720")); }
    QColor textDim() const { return m_dark ? QColor(QStringLiteral("#93a2b4"))
                                           : QColor(QStringLiteral("#5a6675")); }
    QColor accent() const { return m_dark ? QColor(QStringLiteral("#2ed3b7"))
                                          : QColor(QStringLiteral("#0f8b7c")); }
    QColor accentDim() const { return m_dark ? QColor(QStringLiteral("#17564c"))
                                             : QColor(QStringLiteral("#cfeae5")); }
    QColor ok() const { return m_dark ? QColor(QStringLiteral("#42d392"))
                                      : QColor(QStringLiteral("#12805c")); }
    QColor warn() const { return m_dark ? QColor(QStringLiteral("#f5a524"))
                                        : QColor(QStringLiteral("#a15c00")); }
    QColor danger() const { return m_dark ? QColor(QStringLiteral("#f0616d"))
                                          : QColor(QStringLiteral("#c0362f")); }

    int fontMicro() const { return 11; }
    int fontSmall() const { return 13; }
    int fontNormal() const { return 15; }
    int fontTitle() const { return 20; }
    int fontDisplay() const { return 30; }
    int fontHuge() const { return 72; }

    int spaceXs() const { return 4; }
    int spaceSm() const { return 8; }
    int spacing() const { return 12; }
    int spaceLg() const { return 24; }
    int spaceXl() const { return 32; }

    int radiusSm() const { return 8; }
    int radius() const { return 12; }
    int radiusLg() const { return 18; }
    int radiusPill() const { return 999; }

    int navHeight() const { return 64; }
    int touchTarget() const { return 48; }
    int iconSm() const { return 18; }
    int iconMd() const { return 24; }
    int iconLg() const { return 32; }

    /// 手机基准尺寸：桌面运行时窗口取此比例，保证看到的就是手机布局（ADR-0010）
    int phoneWidth() const { return 400; }
    int phoneHeight() const { return 860; }

    int durationFast() const { return 120; }
    int durationBase() const { return 200; }

signals:
    /// 主题模式变化：颜色令牌会随之全部刷新（QML 绑定自动重算）
    void modeChanged();

private:
    /// 统一入口：normalize 模式 → 可选落盘 → 变了才发信号。
    /// 只认 "light"（大小写不敏感），其余一律当 "dark"，避免脏值把界面弄成半黑半白。
    void setModeInternal(const QString& mode, bool persist) {
        const QString normalized =
            (QString::compare(mode, QStringLiteral("light"), Qt::CaseInsensitive) == 0)
                ? QStringLiteral("light")
                : QStringLiteral("dark");
        if (persist) {
            QSettings settings;
            settings.setValue(QStringLiteral("ui/themeMode"), normalized);
        }
        const bool newDark = (normalized == QStringLiteral("dark"));
        if (newDark == m_dark) {
            return;   // 没变化就不发信号，避免无谓的整树重算
        }
        m_dark = newDark;
        emit modeChanged();
    }

    bool m_dark = true;
};

} // namespace pitch

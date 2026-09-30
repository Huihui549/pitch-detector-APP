# 界面风格与实现规范（D + E）—— 复现记录

> **状态：草案**（界面效果待用户在桌面/真机上确认；确认后把状态改成"已定稿"）
> **用途**：任何 QML 界面工作开始前先读本页。**新会话开局也读本页 + `adr/0001-架构与选型.md` 的 ADR-0012。**
> **事实源分工**：本页讲"**怎么用**"，`[仓库] src/app/theme.h` 讲"**值是多少**"；
> 两者冲突时以 `theme.h` 为准（SSOT）。决策出处：**ADR-0012**。

## 一、风格一句话

**近黑底 + 单一青绿强调色 + 超大读数 + 8px 栅格 + 1px 描边分层（不用阴影）。**
（D = 仪表/DAW 深色高对比，E = IBM Carbon 的色板/栅格/字号规则。）

参照：<https://carbondesignsystem.com/elements/color/overview/> ・ <https://m3.material.io/>（只借底部导航/触控尺寸等平台惯例）。

## 二、令牌表（唯一来源 `src/app/theme.h`）

> **两套配色（ADR-0013）**：同一套令牌名，`Theme.mode` 为 `dark`（默认）或 `light`。
> 切换入口在「更多 → 外观」，选择写 `QSettings`（`ui/themeMode`）并重启保持。
> 因此**颜色属性是带 `NOTIFY modeChanged` 的**（不是 `CONSTANT`）——用 `CONSTANT` 时切换不会重绘。
> 命令行 `--theme dark|light` 只改内存不落盘，供截图/排查。

| 类别 | 令牌 | dark（默认） | light | 用在哪 |
|---|---|---|---|---|
| 底色 | `Theme.background` | `#0b0f14` | `#f6f8fa` | 页面底、页头 |
| 面 | `Theme.surface` | `#121821` | `#ffffff` | 卡片、底部导航 |
| 次面 | `Theme.surfaceAlt` | `#1a222c` | `#eef1f6` | 选中态、输入框、轨道底 |
| 描边 | `Theme.border` | `#26303c` | `#d7dee8` | 1px 描边与分隔线（**分层靠描边，不靠阴影**） |
| 文字 | `Theme.text` | `#f2f6fa` | `#0f1720` | 主文字 |
| 次要文字 | `Theme.textDim` | `#93a2b4` | `#5a6675` | 说明、未选中图标 |
| 强调 | `Theme.accent` | `#2ed3b7` | `#0f8b7c` | **唯一**强调色（主读数、选中态） |
| 强调低亮 | `Theme.accentDim` | `#17564c` | `#cfeae5` | 选中胶囊底 |
| 状态 | `Theme.ok` / `warn` / `danger` | `#42d392` / `#f5a524` / `#f0616d` | `#12805c` / `#a15c00` / `#c0362f` | 准 / 需注意 / 错误 |
| 字号 | `fontMicro` 11 · `fontSmall` 13 · `fontNormal` 15 · `fontTitle` 20 · `fontDisplay` 30 · `fontHuge` 72 | 同 | 同 | 角标 / 说明 / 正文按钮 / 页面与卡片标题 / Hz 读数 / **主读数（音名）** |
| 间距 | `spaceXs` 4 · `spaceSm` 8 · `spacing` 12 · `spaceLg` 24 · `spaceXl` 32 | 同 | 同 | 8px 栅格（+4 的半步） |
| 圆角 | `radiusSm` 8 · `radius` 12 · `radiusLg` 18 · `radiusPill` 999 | 同 | 同 | 小控件 / 卡片 / 大面板 / 胶囊 |
| 尺寸 | `navHeight` 64 · `touchTarget` 48 · `iconSm` 18 · `iconMd` 24 · `iconLg` 32 · `phoneWidth` 400 · `phoneHeight` 860 | 同 | 同 | 底部导航 / 最小点区 / 图标 / 手机基准窗口 |
| 动效 | `durationFast` 120 · `durationBase` 200 | ms | ms | 状态色与透明度 / 位移与展开 |

**配色的两条硬规则**：① 深色态用**浅**内容、浅色态用**深**内容（不要出现"深色底 + 深图标"）；
② 浅色态的 `accent`/状态色必须比深色态**更深**，否则白底上对比度不够。两套取值都由像素抽样验证过。

## 三之上、页面三段结构（ADR-0014，**每个页面都必须这样**）

```
┌─────────────────────────────┐
│ 标题区  PageHeader           │  ← 固定，不滚动（Main.qml 提供）
├─────────────────────────────┤
│ 内容区  PageScroller         │  ← **可上下滑动**（页面自己提供）
│   Card / StatCard / …        │
├─────────────────────────────┤
│ 底部导航 BottomNavBar        │  ← 固定，不滚动（Main.qml 提供）
└─────────────────────────────┘
```

- 页面文件里**只写内容区**：`Item { PageScroller { anchors.fill: parent; Card { … } … } }`
- **不要在内容区用 `Layout.fillHeight`**（高度由内容决定，fillHeight 无意义）→ 需要高度就给固定值
- 滚动条样式、回弹行为、自动隐藏都在 `PageScroller` 里，页面不要自带 `Flickable`/`ScrollBar`
- 文件页那种"表格自己滚"的嵌套滚动属特例，不要再增加嵌套层

## 三、组件配方（**都用现成的，不要各页重写**）

| 组件 | 用途 | 关键点 |
|---|---|---|
| `components/Icon.qml` | **所有图标** | SVG + `MultiEffect` 着色；颜色传 `Theme.*`。**图标必须是白描边**（`#ffffff`）：`colorization` 按源亮度混合，黑色源染不上色、深底下等于看不见（坑 A44） |
| `components/ActionButton.qml` | 所有按钮 | `primary`（强调色实心）/ 非 primary（透明 + 1px 描边）；高度 ≥ `Theme.touchTarget` |
| `components/BottomNavBar.qml` | 底部导航（现为 5 个入口） | M3 惯例：图标在上、标签在下、选中项有胶囊底（`accentDim`） |
| `components/NoteDisplay.qml` | 主读数（音名） | 字号自适应但**下限 `Theme.fontHuge`**；有效时强调色 + "已锁定"状态行 |
| `components/CentsBar.qml` | 音分条 | 指针外层柔光 + 内层实针；|音分| ≤5 用 `ok`，否则 `warn` |
| `components/LevelMeter.qml` | 输入电平 | 对数刻度；门槛刻线取 `Session.rmsFloor`（**界面不写 0.008**） |
| `components/PageHeader.qml` | 页头 | 左侧 3px 强调色指示条 + 标题 + 副标题；底部 1px 描边 |
| `components/StatCard.qml` | 指标卡 | 标签 / 主值 / 提示三行 |
| `components/Chip.qml` | 选择胶囊（拍号预置、快选项） | `selected` 时用 `accentDim` 底 + `accent` 描边 + 加粗；高度 ≥ 40 |
| `components/IconButton.qml` | **只有图标**的按钮（一行里放多个轻操作） | 与 `ActionButton` 同一套着色规则；点击区仍为 `Theme.touchTarget` |
| `components/ValueSlider.qml` | 数值滑块（如 BPM） | 只重写 `background`/`handle` 两处为令牌；`live: true` 让拖动实时生效 |
| `component Card`（**页面内联**） | 页面卡片 | 照抄 `pages/LivePage.qml` 顶部那段：面 + 1px 描边 + 内边距三件套 |

## 四、新增一个页面：照抄这个顺序

1. 建 `qml/pages/XxxPage.qml`，头部 import：`QtQuick` / `QtQuick.Controls` / `QtQuick.Layouts` / `PitchDetector.App 1.0` / `"../components"`
2. 复制 `LivePage.qml` 顶部的 `component Card` 定义（卡片是页面级配方，不做成公共组件）
3. 外层 `ColumnLayout`，`anchors.margins` 与 `spacing` 都用 `Theme.spacing`
4. **数据只从 `Session` / `FileAnalysis` 取**；阈值也用 C++ 暴露的属性（`displayMinConfidence` / `holdMs` / `rmsFloor`）
5. 登记 `qml/qml.qrc`；若要在导航中占位，再去 `Main.qml` 的 `navItems` 与页面栈登记
6. 验证：构建 → `--qmlcheck`（**要求零警告**）→ `check-theme.ps1` → `check-layering.ps1`

## 五、新增图标 / 资源

1. 下载到 `[仓库] resources/icons/<name>.svg`（只收 MIT / Apache-2.0 / ISC / CC0）
2. **把描边改成纯白**：`stroke="currentColor"` → `stroke="#ffffff"`（图标集原文件在 Qt 下会渲染成黑色、且染不上色，坑 A44）
3. 登记两处：`resources/resources.qrc`（否则不进二进制）+ `resources/ATTRIBUTION.md`（来源/版本/许可/日期）
4. 使用：`Icon { name: "<name>"; size: Theme.iconMd; color: Theme.text }`
5. 应用桌面图标源文件在 `resources/branding/app-icon.svg`（品牌资产允许自带颜色）

## 六、每次改界面都要跑（三条门禁）

| 命令 | 期望 |
|---|---|
| `bin/pitch-detector-APP.exe --qmlcheck` | `[PASS]` 且**零警告**（QML 警告会被这里抓到） |
| `powershell -ExecutionPolicy Bypass -File tools/check-theme.ps1` | `[PASS]`：qml/ 里不得出现十六进制色值、`Qt.rgba(`/`Qt.hsla(`、裸 `font.pixelSize`、裸 `radius` |
| `powershell -ExecutionPolicy Bypass -File tools/check-layering.ps1` | 三项全 PASS（界面不得出现算法参数） |
| `bin/pitch-detector-APP.exe --uishot <png>` | 渲染界面成 PNG，用于**像素抽样核对**配色/图标是否真的生效（我读不了图，这是唯一的客观验证手段） |
| `bin/pitch-detector-APP.exe --uishot <png> --page metro [--theme light]` | 核对**非首屏页面**与浅色主题：指定页面 + 指定主题各截一张，再按颜色距离统计像素 |

### 已知交互配方（新页面照抄）

- **一拍一块的指示器**：见 `pages/MetronomePage.qml` 的 `component BeatBlock` ——
  一个控件同时是"当前拍指示灯"和"这一拍的细分数编辑器"（点一下循环切换）。
  手机上别再挤一行专门用于编辑的控制；`Flow { Layout.fillWidth: true; Repeater { … } }` 让 8–16 个块自动换行。
- **行内多操作**：标题 + 名称（`Layout.fillWidth` + `elide: Text.ElideMiddle`）+ 多个 `IconButton`；
  用带文字的 `ActionButton` 会挤到放不下。
- **不可用/失败提示**：统一 `Icon("circle-alert") + Text(Theme.warn)` 的卡片内 `RowLayout`，
  文案直接来自 C++（`notice` / `unavailableReason`），界面不改写不判断。

> `check-theme.ps1` 必须保持**纯 ASCII**（坑 A12）且改动后要**做负向验证**（坑 A13：构造违规必须能 FAIL）。

## 七、反例清单（评审发现即退回）

- QML 里写死颜色：`color: "#2ed3b7"`、`Qt.rgba(0,0,0,0.5)`
- 各页面自己写一套按钮/卡片/导航（应复用 `ActionButton` / `Card` / `BottomNavBar`）
- 用文字符号或 emoji 当图标（**必须**用 `resources/icons` 的真实图标文件）
- 把图标集原文件直接入库（`currentColor` → 渲染成黑、且染不上色；必须先改成 `#ffffff`）
- 直接 `Image { source: "qrc:/resources/icons/x.svg" }`（拿不到令牌色）
- 在 `Layout` 里给子项加 `anchors`（QML 明确报"未定义行为"）→ 用 `Layout.alignment`
- 界面里出现算法阈值/窗长/音域数字，或自己定义置信度门槛
- 引入模糊/大阴影（低端机掉帧，且深色下阴影几乎不可见）

## 八、新会话开局自检单（4 步）

1. 读：本页 + `ADR-0012` + `src/app/theme.h`
2. 跑：构建一次 + 三条门禁（第六条）——确认基线是绿的
3. 要新令牌：**先改 ADR 或本页** → 再改 `theme.h`（顺序不能反）
4. 要新图标：按第五条登记两处 + 本页第三节用法

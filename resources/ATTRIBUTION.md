# resources/ — 第三方资源登记（授权与来源）

> **本目录只放"人写不了/不该手画"的资源**：图标、品牌图、以后可能的音效。
> 每条必须写清**来源 URL、版本、许可**——否则后续无法合法分发（这是留档，不是可选装饰）。
> 资源如何被引用：`resources/resources.qrc` 以 `/resources` 前缀编入可执行文件，
> QML 里用 `qrc:/resources/icons/xxx.svg`（详见 `dev-docs/pitch-detector-APP/design/ui-style.md`）。

## 一、图标（`icons/`）

| 项 | 值 |
|---|---|
| 图标集 | **Lucide**（lucide-static） |
| 版本 | v1.49.0（文件头有 `@license lucide-static v1.49.0 - ISC`） |
| 许可 | **ISC License**（宽松，允许商用与修改；保留版权声明即可） |
| 来源 | `https://cdn.jsdelivr.net/npm/lucide-static@latest/icons/<name>.svg`（官方 npm 包 CDN） |
| 官网 | https://lucide.dev/ |
| 登记日期 | 2026-09-30 |
| 本地形态 | 32 个 SVG，24×24 viewBox，描边为 **`#ffffff`**（原文是 `currentColor`，见下方"本地修改"） |

**已收录**（文件名即 Lucide 图标名）：

- 导航：`activity`（实时）・`file-audio`（文件）・`gauge`（音域）・`metronome`（节拍器）・`ellipsis`（更多）
- 采集/播放：`mic`・`mic-off`・`play`・`square`
- 文件操作：`folder-open`・`save`・`file-text`・`trash-2`・`download`・`upload`
- 其它动作：`refresh-cw`・`share-2`・`search`・`settings`・`sliders-horizontal`・`pointer`・`x`
- 状态：`check`・`circle-alert`・`triangle-alert`・`circle-x`・`info`・`bug`
- 领域：`waves`・`music`
- 方向：`chevron-left`・`chevron-right`

### 本地修改（**升级图标时必须重做**）

Lucide 原生用 `stroke="currentColor"` 表示"颜色交给使用方"，但两个坑叠在一起：

1. Qt 的 SVG 渲染器**不解析 `currentColor`** → 渲染成**黑色**；
2. `MultiEffect.colorization` 是**按源亮度**参与混合的 → 黑色源乘任何颜色**仍然是黑**。
   在深色主题下表现为"未选中时图标几乎看不见"（**用户实测反馈**）。

故**入库时已把 `currentColor` 统一替换为 `#ffffff`**（32/32 个文件）：

```
stroke="currentColor"   ->   stroke="#ffffff"
```

白色源才能被染成目标色：`Icon.qml` 用
`MultiEffect { colorization: 1.0; colorizationColor: <Theme 令牌> }`。
**重新下载 Lucide 图标后必须重做这一步**，否则又会变黑；`tools/check-theme.ps1`
会检查 `icons/*.svg` 只含纯白、不得出现其它颜色（忘记改就被门禁拦下）。

**图标必须在 QML 侧着色**——统一走 `qml/components/Icon.qml`。**不要**在 QML 里直接
`Image { source: "...svg" }`：既拿不到令牌色，也绕过了风格门禁。

## 二、品牌图 / 应用图标（`branding/`）

| 文件 | 来源 | 许可 | 说明 |
|---|---|---|---|
| `app-icon.svg` | **本项目原创**（按 D 风格绘制：深底 + 青绿波形 + 音叉元素） | 随本项目 | 应用桌面图标源文件；由 `tools/gen-app-icons.ps1` 用 Qt 渲染成各密度 PNG |

## 三、以后新增资源时的规矩

1. 先查许可：**只收 MIT / Apache-2.0 / ISC / CC0 / 公有领域**；GPL 类与"来源不明"的一律不收
2. 在本文件登记：来源 URL、版本、许可、登记日期、用途
3. 文件名用 `kebab-case`，与图标集中原名保持一致（便于日后按名升级）
4. 资源**进版本库**（人选的、决定外观的、需要回溯的 → 进库；`R4` 的 `.gitignore` 只排除生成物）

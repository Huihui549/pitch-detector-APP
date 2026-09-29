# pitch-detector-APP — AI 协作须知（AGENTS.md）

> 只写 **AI 猜不到的硬事实**（命令、路径职责、硬约定、禁区）。通用行为规范在 `ai-rules/`，不在此重复（R7 SSOT）。

## 协作规则

- 行为规则路径：`[PC] D:\dev_doc\ai-rules`（`rules/R1`–`R11`、`roles/`）
  - 注：本机存在两个副本 `D:\dev_doc\ai-rules` 与 `D:\dev_project\ai-rules`，**以 `D:\dev_doc\ai-rules` 为准**
- 该库不可见时，先让用户粘贴 `templates/dialogue-opening.md` 规则段再开工
- 项目文档位置与维护方式见 `ai-rules/rules/R6-feature-docs.md`
- 本仓库 git 边界：**不做任何 git 写操作**，改动留工作区（R4）；只读白名单见 R4

## 项目一句话

`pitch-detector` 网页版（单文件 HTML + Web Audio + JS/YIN）的 **Qt 重写版**，目标形态：手机 APP + 电脑桌面客户端 + 嵌入式设备预留。UI 用 QML，算法与流程用 C++，跨平台同一份代码。

## 上游项目（只读，勿改）

| 项 | 值 |
|---|---|
| 原项目根 | `[PC] D:\dev_project\pitch-detector`（**只读参考，禁止修改**） |
| 唯一算法真值（JS） | `[PC] D:\dev_project\pitch-detector\tools\pitch-engine.js`（492 行，UMD） |
| 原项目文档 | `[PC] D:\dev_project\pitch-detector\dev-docs\pitch-detector\`（context / pitfalls #1–#29 / ADR-0001 / spec / verify / dev / debug / audio-test-report） |
| **钢琴素材（回归基准）** | **已缺失**（2026-09-28 实测：`D:\dev_project\pitch-detector\resource_audio\` 不存在，全盘搜 `870f3-main` 无结果）→ A1/A2/B2 暂时无法执行，替代口径见 `verify.md` A1'/A2'、坑 A9 |
| Node 参考工具 | `tools/verify-engine.mjs`、`tools/test-single-note.mjs`、`tools/wav-read.mjs`（**跨语言一致性验证的对照端**） |

- **算法参数不得凭记忆或推测落地**：一律回查 `pitch-engine.js` 原文（R2：数值须有实据）
- 本项目的测试真值由 `node tools/gen-test-fixtures.mjs` 生成（读上游引擎算真值），产物在 `tests/data/`
  - 逐帧数值为 **float64 原始字节**（`*.f64`，已 gitignore，可重新生成）；音名真值 `*.note` 与清单 `reference.txt` 为文本，**保留跟踪**

## 构建 / 运行 / 测试

> **构建系统：qmake**（用户指定，不用 CMake；工程文件为根目录 `pitch-detector-APP.pro` + 各子目录 `.pro`）。
> **工具链（2026-09-29 实测可用）**：
> · Qt 6.8.3 `[PC] D:\Qt\6.8.3\mingw_64`（含 Android arm64-v8a 套件）
> · **编译器用 Qt 自带的 MinGW 13.1.0** `[PC] D:\Qt\Tools\mingw1310_64\bin`
> · 并行 make 用 jom `[PC] D:\Qt\Tools\QtCreator\bin\jom\jom.exe`
> · **不要用 WinLibs 的 g++ 编 Qt 部分**（UCRT 堆不一致，实测崩溃在 `QString::toStdString` 的 `RtlFreeHeap`）
> · Qt 未加入系统 PATH，命令行执行前先：
>   `$env:PATH = "D:\Qt\6.8.3\mingw_64\bin;D:\Qt\Tools\mingw1310_64\bin;$env:PATH"`

| 用途 | 命令 | 状态 |
|---|---|---|
| **一键构建+部署+运行** | 双击 `[PC] D:\dev_project\pitch-detector-APP\build-and-run.bat` | **已实测** |
| **一键运行（已部署版）** | 双击 `[PC] D:\dev_project\pitch-detector-APP\run-app.bat` | **已实测**（自包含，不需 Qt 在 PATH） |
| 生成工程 | `qmake pitch-detector-APP.pro CONFIG+=release` | **已实测通过** |
| 构建 | `jom`（或 `mingw32-make`） | **已实测通过**：0 error / 0 warning |
| 生成测试真值 | `node tools/gen-test-fixtures.mjs` | **已实测通过**（4 素材自检全 PASS） |
| 生成 88 键矩阵真值 | `node tools/gen-88key-matrix.mjs` | **已实测通过**（176 用例，上游命中 174/176；约 253 s） |
| 参数同源检查 | `node tools/compare-params.mjs` | **已实测通过**（16/16 一致） |
| 分层门禁 | `powershell -ExecutionPolicy Bypass -File tools/check-layering.ps1` | **已实测通过**（三项全 PASS） |
| 单元测试 | `bin\core-tests.exe --data tests\data` | **已实测通过**：109 项 / 失败 0（约 267 s） |
| 跨语言对拍 | `bin\cross-check.exe --data tests\data --mode=analysis`（或 `--mode=realtime`） | **已实测通过**：4/4 且逐位一致 |
| 界面自检（无头） | `bin\pitch-detector-APP.exe --qmlcheck` | **已实测通过**：QML 根对象创建成功、零 QML 错误 |
| 音频设备自检 | `bin\pitch-detector-APP.exe --devices` | **已实测通过**：2 个输入设备，FFmpeg 7.1 后端 |
| 文件分析自检（无头） | `bin\pitch-detector-APP.exe --selftest <wav> --expect <音名>` | **已实测通过**（A4 / A3 素材均命中） |
| 部署 | `windeployqt --release --qmldir qml ... run\pitch-detector-APP.exe` **＋ 手工补 Qt Multimedia** | **已实测通过**（孤岛运行 PASS，含设备枚举） |

- **Qt Multimedia 已安装**（2026-09-29 用 MaintenanceTool headless 装入 Qt 6.8.3，含 mingw_64 / Android / wasm 三条链）：
  组件名 `qt.qt6.683.addons.qtmultimedia`，装法：
  `D:\Qt\MaintenanceTool.exe install qt.qt6.683.addons.qtmultimedia --root D:\Qt --accept-licenses --accept-obligations --accept-messages --confirm-command`
  - **装完必须重新 qmake 再构建**（`CONFIG` 里的 `qtHaveModule(multimedia)` 在 qmake 阶段求值）
  - **`windeployqt` 不会自动带上 Multimedia**（它是运行期按需加载的插件式后端，不在直接依赖里）：
    部署时必须手工拷贝 `Qt6Multimedia.dll`、`Qt6Network.dll`、`Qt6MultimediaQuick.dll` 与 `plugins\multimedia\*.dll`
    ——`build-and-run.bat` 已包含这一步
- **本机音频设备实测（`--devices`）**：默认设备「麦克风 (Redmi 电脑音箱)」首选 **48000 Hz / 2 声道 / Float**，
  不支持 44.1 kHz 单声道 Float → 采集实现会退到设备首选格式并显式提示（坑 A3）；另一设备「Steam Streaming Microphone」为 44.1 kHz/单声道

- **改了 `.pro` 或增删源文件后必须重新 qmake**：qmake 用 `-incremental` 不重读 `.pro`；
  只改 `.pro` 内容而不增删文件时，Makefile **不会**自动重生成（实测踩过，表现为"改了没生效"）
- **`.pro` 与 `.qrc` 必须存为 UTF-8 无 BOM**（坑 A20：带 BOM 时 qmake 静默忽略文件开头的一批赋值）
- **C++20 标志写在各子工程自己的 `QMAKE_CXXFLAGS`**：subdirs 顶层变量不传子工程，
  且只有 `QMAKE_CXXFLAGS` 会进最终命令（`QMAKE_CXXFLAGS_RELEASE/_DEBUG` 会被 mkspec 覆盖）
- **测试很慢的原因（勿误判为卡死）**：低频段（<110 Hz）的差分函数代价按窗长平方增长，单个 16384 样点窗约 1.6 亿次运算；
  故单测里的合成信号时长用 0.7 s 而非 3 s（严格逐帧对拍由 `cross-check` 用 3 s 素材负责）
- **辅助脚本的编码规则**（踩过两次，坑 A12）：机器执行的 `.ps1` **只写 ASCII**（中文用 `[char]0xXXXX` 拼接），且只用 Windows PowerShell 5.1 也支持的参数（本机无 `pwsh`）
- **界面当前状态（2026-09-29）**：Qt Multimedia **已安装** → 麦克风实时链路可用；
  文件分析、音域测量、调试页均可用（音域页也可直接用麦克风，不必再用文件回放）

## 目录职责

| 路径 | 职责 | 层 |
|---|---|---|
| `src/core/` | 音高算法（YIN、级联窗长、精修、谐波复核、八度校正）、音乐换算、纯数据结构 | **纯逻辑，零 Qt 依赖**（可被任意宿主复用/单测） |
| `src/audio/` | 音频采集抽象与实现（`IAudioSource` + `QtAudioSource`；嵌入式实现预留） | 依赖 Qt Multimedia，不依赖 UI |
| `src/io/` | WAV 解析、CSV 导出、文件分析调度 | 依赖 core |
| `src/app/` | 控制器（会话状态、平滑/去抖、音域统计）、QML 类型注册、`main.cpp` | 依赖 core/audio/io，向 QML 暴露属性与信号 |
| `qml/` | **纯界面**：`Main.qml`、`pages/`、`components/`；不含任何算法、不做阈值判断 | 只与 `src/app` 暴露的属性/信号交互 |
| `tests/` | Qt Test 单测与跨语言一致性验证脚本 | 依赖 core/io |
| `tools/` | 辅助脚本（素材核对、报告生成、JS 对照桥） | — |

## 硬约定

- **分层铁律**：`qml/` 里禁止出现任何音高算法痕迹（无阈值、无 τ、无窗长、无音名换算公式）；算法只在 `src/core/`，**全仓库一份实现**
- C++ 标准：**C++20**（ADR-0008：算法层用 `std::span` 表达样点视图；Qt 6.8.3 要求 C++17 或更高，故不冲突）；编译 0 warning
- 命名：文件 `kebab-case`；C++ 类型 `PascalCase`、函数/变量 `camelCase`、成员变量 `m_` 前缀、常量 `kPascalCase`
- 浮点：算法内部用 `double`（对齐 JS Number 的双精度），采样数据用 `float`；**不得为"省内存"降精度**（会改变判定结果）
- 音名一律科学音高记号（中央 C = C4），不显示唱名
- 所有魔数（阈值、窗长、τ 区间）必须带来源注释，指向 `pitch-engine.js` 的对应行或原项目实测结论
- 提交规范（供用户参考，AI 不代执行）：Conventional Commits，`<type>: <简短中文描述>`

## 禁区（踩过的坑，勿再犯）

- **不做**和弦/多音高检测、评分排名、云端与账号（上游 spec 明确排除）
- 不得在 `qml/` 里复制算法、不得为"以后可能用到"预留接口（R8 YAGNI）
- 不得改动上游 `pitch-detector` 项目任何文件
- 不得在未实测的情况下声称某个平台/命令可用（R2）
- 不得用 PowerShell `Get-Content`/`Set-Content` 回写含中文的源文件（上游 pitfalls #20，实测致文件报废）

## 本项目文档位置

- 规格 / 日志 / 验收：`[PC] D:\dev_project\pitch-detector-APP\dev-docs\pitch-detector-APP\features\pitch-detector\`
- 项目上下文 / 坑 / 决策：`[PC] D:\dev_project\pitch-detector-APP\dev-docs\pitch-detector-APP\`

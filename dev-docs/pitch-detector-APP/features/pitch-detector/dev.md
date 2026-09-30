# dev.md — 音高检测 Qt 应用 开发日志（可重放）

> 按功能单元分节（依赖序）。每单元含：目标 / 改动 / 事实 / 验证 / 依赖。
> 引用坑与决策用编号：`坑 A3`、`ADR-0002`（定义在项目级 `pitfalls.md` / `adr/`）。

## 单元 0：项目骨架与文档（开发前准备）

- **目标**：在写任何代码前，把项目边界、算法真值、验收口径与坑固化下来，避免凭记忆重写（R2/R6）。
- **改动**：新建 `<repo>\`，含 `AGENTS.md`、`.gitignore`、目录骨架（`src/{core,audio,io,app}`、`qml/{pages,components}`、`tests/data`、`tools`）、`dev-docs/pitch-detector-APP/{context.md,pitfalls.md,adr/0001-架构与选型.md,design/architecture.md,features/pitch-detector/{spec.md,verify.md,dev.md}}`。
- **事实（全部实测或上游实证）**：
  1. 上游项目 `[PC] [上游]` 是 git 仓库（`git rev-parse --show-toplevel` = `[上游]`），当前工作区**干净**，仅 1 个提交 `9cb3830 feat: 音高检测工具首版（单音识别 84/88）`，**只读参考，本次未改动其任何文件**。
  2. 上游算法真值文件 `tools/pitch-engine.js` 共 492 行，已全文读取；关键常数：`A4=440`、`F_MIN=27`、`F_MAX=4300`、`FRAME_LADDER=[1024,2048,4096,8192,16384]`、`YIN_THRESHOLD=0.3`、`RMS_MIN=0.008`、`RMS_REL_MIN=0.02`、`REFINE_MIN_HZ=500`、`REFINE_STEPS=240`、`REFINE_SPAN=0.03`、`HARMONIC_MAX=8`、`SUBHARMONIC_MIN_HZ=40`、改判倍数 `1.15`、`SHALLOW=0.15`。
  3. 上游引擎使用**模块级静态缓冲**（`yinD`/`cmndBuf`/`workBuf`，按最大窗长预分配）与"最近一次中间量"（`lastCmnd`/`lastTauMin`/`lastTauMax`）——C++ 侧改为显式传入缓冲以保证可重入，语义等价（记入 `design/architecture.md` 3.1）。
  4. 环境实测（2026-09-28）：`<QtRoot>` 目录**存在但为空**；`qmake`/`qtpaths`/`g++`/`cl`/`cmake`/`ninja` 均不在 PATH；未检出 Visual Studio 安装目录。→ Qt 安装仍在进行，构建与测试命令**尚不可执行**。
  5. 本仓库 `<repo>` 为**新目录**，不在任何既有 git 仓库内；按 R4「无 git 仓库禁止修改，先询问用户是否初始化」，已取得用户授权后执行 `git init`，仓库根确认为 `<repo>`。
  6. 上游 `AGENTS.md` 记载的规则库路径（`<rules-repo>`，跨项目通用、不在本仓库内）在本机实测**不存在**；实际存在 `<rules-repo>`（本次会话已读取）。已在本项目 `AGENTS.md` 中定为准 `<rules-repo>`。注意：规则库路径本身是机器相关的，因此**不写进仓库**，见坑 A32。
- **验证**：`git rev-parse --show-toplevel` 返回新仓库根；`git status --short` 可见全部新增文件为未跟踪状态；上游 `git status --short` 为空（未被污染）。
- **依赖**：无。

## 单元 1：`src/core/` 算法内核移植（写入完成，**编译与单测待环境**）

- **目标**：把上游唯一算法实现（`pitch-engine.js`，492 行）移植为纯 C++17 模块，零 Qt 依赖，可独立单测（ADR-0007）。
- **改动**：新建 `src/core/` 共 9 个文件 ——
  `pitch-types.h`（22 项参数 + 纯数据类型）、`pitch-engine.h/.cpp`（YIN 主判据、τ 精修、频域精修、谐波复核、级联窗长）、`note-converter.h/.cpp`（频率↔音名/音分）、`octave-unifier.h/.cpp`（八度轨迹校正）、`analysis-runner.h/.cpp`（整段分析调度）、`CMakeLists.txt`；顶层 `CMakeLists.txt`。
- **事实（移植中确认的关键点，全部来自上游源码逐行对照）**：
  1. 上游 `analyzeBuffer` 的主循环条件是 `pos + MAX_FRAME <= n`，而 `MAX_FRAME = 16384` → **短于 16384 样点（44.1 kHz 下约 371 ms）的音频不会产生任何帧**。这是原有行为，已写入 `analysis-runner.h` 的接口注释，要求界面显式提示而非显示"未检测到有效音高"。
  2. 上游用模块级静态缓冲（`yinD`/`cmndBuf`）与"最近一次中间量"（`lastCmnd`/`lastTauMin`/`lastTauMax`）。C++ 侧改为**显式传入 `EngineBuffers`**，并把曲线通过出参返回（`detect(..., outCurve, outTauMin, outTauMax)`），做到可重入且不丢调试能力。
  3. 上游 `preferFundamental` 里的分频下限用的是**模块常量 `F_MIN`（27 Hz）**，不是 `detectPitch` 的参数 `fMin`。C++ 侧保持同一行为（用 `kDefaultFMin`），并在代码注释中标注：若将来要为窄音域调用方收紧，必须同步改上游并重跑对拍。
  4. 上游 `magAtLut` 的相位增量取整（`Math.round(freq/sr*1024) || 1`）与 `idx & 1023` 查表是本实现必须保留的细节——为对齐数值，C++ 侧同样用整数索引查表，而非改用连续相位三角函数。
  5. `M_PI` 是 POSIX 扩展（MSVC 需 `_USE_MATH_DEFINES`），已改为项目内 `constexpr double kPi`，避免跨编译器陷阱。
- **验证结果**：**未编译、未运行**。本机无任何 C++ 编译器（实测 `g++`/`clang++`/`cl` 全部缺失，无 MinGW/MSVC），Qt 也仍在安装（`<QtRoot>` 为空）。→ `verify.md` 的 C1–C3 与全部 A/B 组项保持"未开始"，**不得视为通过（R2）**。
- **依赖**：单元 0。

## 单元 2：测试素材与跨语言对拍真值（Node 侧完成，**不需要 C++ 编译器**）

- **目标**：在编译环境就绪前，把 B 组（跨语言一致性）验收所需的**输入与真值**先做出来，让移植一旦能编译就能立刻验证。
- **改动**：
  - 新增 `tools/gen-test-fixtures.mjs`：合成 4 个素材 → 写 WAV → **读回自己写的 WAV** → 用上游引擎算真值 → 落盘清单与真值文件。
  - 新增 `tools/cross-check.h/.cpp/-main.cpp`（C++ 对拍工具，含真值读取、逐帧比对与报告）。
  - 新增 `src/io/{wav-reader,realtime-runner}.h/.cpp`（WAV 读取与实时链路逐帧分析）与 `src/io/CMakeLists.txt`、`tools/CMakeLists.txt`；顶层 CMake 接入 `src/io` 与工具目标。
- **事实（本轮实测，全部来自跑通的命令）**：
  1. **上游 88 键钢琴素材已不存在**：`[上游]\resource_audio\` 目录缺失，全盘搜 `870f3-main` 无结果。该素材是上游 84/88 基线的唯一数据来源，故 spec 的 A1/A2 与 verify 的 B2 **暂时无法执行**（详见坑 A9）。
  2. 4 个合成素材全部通过自检（音名正确 **且** 偏差 < 50 音分，双条件）：A4 纯音中位 **440.0175 Hz（+0.07 音分）**、A3 纯音 220.0009 Hz（+0.01）、A3 含 2/3/4 次泛音 220.001 Hz（+0.01）、静音 0 帧。A4 的 +0.07 音分与上游 `AGENTS.md` 记录的 "+0.1 音分" 吻合，说明真值链路正确。
  3. 逐帧真值规模：每个有声音素材 **文件分析 263 帧**（帧进 441）、**实时 251 帧**（窗 4096、帧进 512）。
  4. **真值不能存 JSON**：`JSON.stringify` 会丢浮点末位，而 B1 要求相对差 ≤1e-6，用 JSON 当基准会出现"永远对不齐"的假失败。故逐帧数值改为 **float64 原始字节**落盘（64 B/帧 / 48 B/帧），元数据用纯文本 `reference.txt`，C++ 侧无需引任何 JSON 库。
  5. **真值必须补音名列**：数值真值里没有音名，而本项目的核心风险正是八度误判（上游 pitfalls #26：B0 曾报 +1195 音分却因标签正确被计为命中）。故每个素材额外落一份 `*.note`（每行一帧的 ASCII 音名），对拍时逐帧比对音名。
  6. **编码陷阱（又一次）**：清单里静音素材的音名最初写成 `—`（U+2014），C++ 侧按字节读取时看到乱码。已改为 ASCII 安全的 `none`——与上游 pitfalls #20 同类问题，规避方式是"机器读取的清单只放 ASCII"。
  7. **实时链路存在一个已知口径差异**：我们在**上传的 `pitch.html` 源码里读到**的实时循环是"对固定 4096 样点窗直接单帧检测、不走级联"，而文件分析走级联窗长（`frameladder`）。本项目首版沿用该策略（ADR-0004）。因此实时对拍把"C++ 复刻固定窗"与真值比对（必须完全一致），另单独报告"C++ 级联 vs 上游固定"的音名差异率——后者是口径差异，不得计为移植错误。
  8. `readWavMono`（上游 `tools/wav-read.mjs`）的返回字段名是 **`rate`** 而非 `sampleRate`，且 16 bit 归一化用 **`/32768`**（不是 32767）。C++ 侧 `src/io/wav-reader.cpp` 已按同一口径实现——两端输入幅度必须一致，否则对拍会出现无法归因的差异。
- **验证结果**：
  - `node tools/gen-test-fixtures.mjs` → **自检失败项 0**，产出 4 个 WAV + 8 个真值文件 + 清单（可重复执行，结果确定）。
  - `powershell -ExecutionPolicy Bypass -File tools/check-layering.ps1` → **三项全 PASS**（core 零 Qt、qml 零算法、算法定义唯一），并做过负向验证（构造 `fMax: 4300` 的 qml 探针 → 脚本 FAIL 并报出 `_gate_probe.qml:2`，探针已删除）。
  - C++ 侧 `cross-check` 工具**尚未编译**（本机仍无任何 C++ 编译器，Qt 安装在 `<QtRoot>` 仍为空）→ B1/B2/B3 状态保持"未开始"，**不得视为通过（R2）**。
- **依赖**：单元 1。

## 单元 3：分层门禁脚本（工具，已验证）

- **目标**：把"算法不得进界面层"从口头约定变成可机械执行的门槛（上游 pitfalls #22 曾因算法三份副本导致结论分叉）。
- **改动**：新增 `tools/check-layering.ps1`（三项检查：core 零 Qt 头 / qml 零算法符号 / 算法函数无跨文件重复定义）。
- **事实（踩坑与修正，均已记入 pitfalls）**：
  1. **又一次撞上上游 pitfalls #11**：脚本含中文且存为无 BOM UTF-8 → Windows PowerShell 5.1 按 ANSI 解码 → 字符串终止符被吃掉、解析失败。修法：**脚本只写 ASCII**，中文用 `[char]0xXXXX` 拼接（见坑 A12）。
  2. **本机没有 `pwsh`**（只有 Windows PowerShell 5.1），而 `Select-String -Recurse` 是 PS7 专有参数 → 脚本直接抛错。修法：改用 `Get-ChildItem -Recurse -File | Select-String`（见坑 A12）。
  3. **门禁第一版误报**：把同一函数的**合法重载**（`detect` 带/不带调试曲线出参）判成"重复实现"。修法：把参数列表纳入比对键，且只有**同一签名出现在不同文件**才算违规。
  4. 新增坑 A13：门禁脚本必须做**负向验证**（构造真实违规确认能 FAIL），否则可能永远 PASS 而给出虚假安全感。
- **验证结果**：`powershell -ExecutionPolicy Bypass -File tools/check-layering.ps1` → 三项全 PASS（exit 0）；负向验证 → 报 `[FAIL] qml has no algorithm trace` 并给出 `_gate_probe.qml:2 readonly property real fMax: 4300`（exit 1），探针随后删除（`Test-Path` 返回 False）。
- **依赖**：单元 1。

## 单元 4：首次构建 + 跨语言对拍全绿（**算法层已实测**）

- **目标**：把已写好的算法层真正编译起来，并证明它与上游 JS 引擎给出同一结果（B 组）。
- **改动**：
  - 装独立工具链：`winget install BrechtSanders.WinLibs.POSIX.UCRT`（用户级、免提权；**自带 cmake 4.4.2 与 ninja 1.13.2**，见 `AGENTS.md`），路径 `[PC] %LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_...\mingw64\bin`
  - **C++ 标准由 17 改为 20**（ADR-0008）：算法层用 `std::span` 表达样点视图，退回 17 需自造裸指针+长度替代品
  - CMake 接入 `src/io`、`tools`、`tests` 三处子目录；`tests/CMakeLists.txt`（CTest）、`tools/CMakeLists.txt`
  - 新增 `tests/support/json-reader.{h,cpp}`（手写极简 JSON，供单测读真值，不引第三方库）
  - 新增 `tests/core/core-tests.cpp`（纯标准库单测，**不依赖 Qt**，理由见该文件头）
  - 新增 `tools/gen-88key-matrix.mjs`（88 键合成矩阵真值）、`tools/compare-params.mjs`（B3 参数同源检查）
  - 新增 `tools/check-layering.ps1`（分层门禁）
- **事实（全部实测，命令与结果）**：
  1. **构建**：`cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=<...>/g++.exe` + `cmake --build build` → **14/14 目标成功，0 error、0 warning**（门槛 `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`）。
  2. **第一次编译就暴露一个真实缺陷**：源码用了 `std::span`（C++20）而项目声明 C++17 → 编译失败。处置见 ADR-0008（升 C++20，而非自造替代类型）。
  3. **B3 参数同源**：`node tools/compare-params.mjs` → **16 项全部一致**（14 个上游模块常量 + 2 个内联字面量：谐波改判倍数 1.15、八度折回容差 120 音分）。
     - 首跑误报 2 项（`MAX_FRAME`、`kOctaveToleranceCents`），复查为**解析器缺陷**（前者上游是计算值 `FRAME_LADDER[len-1]`，后者是类内 `static constexpr`），已修解析器后全绿。
  4. **B1 逐帧对拍（文件分析）**：`cross-check --data tests/data --mode=analysis` → **4/4 通过**，且**完全逐位一致**：
     - 频率相对差最大 **0.000e+00**、音分绝对差 0.000e+00、置信度 0.000e+00、RMS 0.000e+00
     - 音名不一致 **0 帧**（263 帧 × 3 个有声音素材）
  5. **B1 逐帧对拍（实时链路）**：`--mode=realtime` → **4/4 通过**，同样逐位一致（251 帧）。
     - 另报的"级联窗长 vs 上游固定 4096"音名差异：**0 / 251 帧**——即在合成信号上两种窗长策略结果相同，B4 的口径差异已被量化
  6. **88 键合成矩阵**：`node tools/gen-88key-matrix.mjs` → 176 个用例（88 键 × 纯音/含泛音），**上游引擎命中 174/176（98.9%）**；产物仅 3.0 MB（354 个文件，只存真值不存 WAV）
     - 未命中 2 个：**MIDI 36 C2 与 MIDI 37 C#2 的纯音**被上游引擎判成 C1/C#1（低一个八度，中位偏差 0.0 音分）。含泛音的同一音名则全部判对（harm 88/88）
     - 这是**上游引擎的边缘行为**（纯正弦在低音区被浅谷复核折低八度），不是本项目缺陷；已记入坑 A14，并写入矩阵报告 `tests/data/matrix88/report.md`
- **验证结果**：
  - 构建 0 warning ✅、参数同源 16/16 ✅、分层门禁三项 PASS ✅、文件分析对拍 4/4 ✅、实时链路对拍 4/4 ✅、88 键矩阵真值已产出 ✅
  - `core-tests`（单测）已启动但**尚未取回结果**：它内含 30 次白噪声分析，耗时较长；日志在 `build/logs/core-tests.log`
- **依赖**：单元 1、2、3。

## 单元 5：Qt 层与手机形态界面（**桌面已可运行**）

- **目标**：把算法接到真实界面上，交付一个能在电脑上运行、按手机版布局的桌面程序（用户 2026-09-28 要求）。
- **改动**（新增 20 个文件）：
  - `src/audio/`：`i-audio-source.h`（采集抽象 + 错误分类）、`file-audio-source.{h,cpp}`（WAV 按真实速率回放）、`qt-audio-source.{h,cpp}`（Qt Multimedia 麦克风，**条件编译**）、`CMakeLists.txt`
  - `src/app/`：`pitch-session-controller.{h,cpp}`（实时会话：环缓冲、实时检测、去抖保持、音域统计、曲线）、`file-analysis-controller.{h,cpp}`（文件分析 + `FrameTableModel` + CSV + 曲线抽样）、`main.cpp`（入口 + `--selftest` + `--qmlcheck`）、`CMakeLists.txt`
  - `qml/`：`Main.qml`（单页 + 底部导航 + 隐藏调试入口）、`Theme.qml`（单例配色）、`components/`（NoteDisplay / CentsBar / PitchCurve / LevelMeter / StatCard / PageHeader / BottomNavBar）、`pages/`（LivePage / FilePage / RangePage / MorePage / DebugPage）、`CMakeLists.txt`
  - `run-app.bat`（一键运行启动器）
- **事实（全部实测，含踩坑）**：
  1. **Qt Multimedia 未安装**（`<QtRoot>\6.8.3\mingw_64\lib\cmake` 下无该模块，只有它的翻译文件）→ `QAudioSource` 不可用。
     处置：采集层条件编译该实现；实时页显式提示"未安装 Multimedia，请用 MaintenanceTool 勾选"，并提供**文件回放**作为替代验证手段（`FileAudioSource`）。补装后无需改结构。
  2. **编译器必须用 Qt 自带的 MinGW 13.1.0**：先用 WinLibs MinGW 16.2（UCRT）编译，运行期崩在 `QString::toStdString()` 的 `RtlFreeHeap`（gdb 给出 backtrace），属跨运行时堆不一致。换 Qt 的 MinGW 后消失。
  3. **C++ 标准 C++20**（ADR-0008）：首次编译报 `std::span is only available from C++20 onwards`。
  4. **Qt 官方在线安装器不装 debug 库**（`Qt6Cored.dll` 不存在）→ 必须用 **Release** 构建做试运行/部署（坑 A16）。
  5. **QML 单例的 CMake 声明位置敏感**：`set_source_files_properties(Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)`
     **必须写在 `qt_add_qml_module` 之前**，否则 qmldir 里没有 `singleton` 标记，表现为满屏 `Unable to assign [undefined] to QColor`（坑 A15）。
  6. **QML 模块的输出目录必须与模块名同名**：`OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml/PitchDetector"`，
     否则引擎按 `<导入路径>/PitchDetector/` 找不到模块，报 `module "PitchDetector" is not installed`。
  7. **页面里的 import 用相对路径**（`import "../components"`），不用 `qrc:/...` 绝对路径——后者在本项目布局下不存在。
  8. **自定义 QML 模块的部署尚未解决**（坑 A17）：`windeployqt` 不管自定义模块；且 qmldir 的 `linktarget` 指向静态库、
     插件 DLL 却按共享库解析它，部署目录下模块加载失败。故提供 `run-app.bat`（设 PATH 后从构建目录运行）作为当前可用路径。
  9. 分层门禁在本轮**抓到 3 处真实违规**（QML 里硬编码阈值 0.3、窗长 "4096/16384"、音域 "27–4300 Hz"）——
     已改为由 C++ 暴露只读属性（`Session.yinThreshold` / `frameDescription` / `rangeDescription`），门禁转 PASS。
  10. **实时链路改用两级窗长 4096 + 16384**（ADR-0009）：上游固定 4096 只能覆盖 82–1050 Hz，
     与"手机 APP 要能测钢琴/吉他"直接冲突（A0 周期 1604 样点装不进 4096 窗）。
- **验证结果**（命令与结果）：
  - 构建：`cmake --build build-rel` → **0 error / 0 warning**
  - 界面加载：`pitch-detector-APP.exe --qmlcheck` → **`[PASS] QML 根对象创建成功：1 个`，零 QML 警告**
  - 文件分析链路：`--selftest tests/data/expect-A4-440Hz-pure.wav --expect A4` →
    **A4 / 440.02 Hz / +0.1 音分**；`--selftest tests/data/expect-A3-220Hz-harm.wav --expect A3` → **A3 / 220.00 Hz / +0.0 音分**
  - 无回归：跨语言对拍 analysis **4/4**、realtime **4/4**（仍逐位一致）；参数同源 **16/16**；分层门禁**三项全 PASS**
  - **尚未做**：麦克风实时链路（缺 Multimedia）、手机端、端到端延迟实测、界面人工走查（D1–D10）
- **依赖**：单元 4。

## 单元 7：构建系统由 CMake 改为 qmake（用户指定）

- **目标**：用户明确要求"不用 CMake，改用 qmake"。全部构建配置换成 `.pro`，功能与验收项不得退化。
- **改动**：
  - 删除 8 个 `CMakeLists.txt` 与两个 CMake 构建目录
  - 新增 qmake 工程：`pitch-detector-APP.pro`（subdirs + ordered + 依赖关系）、`src/core/core.pro`、`src/io/io.pro`、`src/audio/audio.pro`、`src/app/app.pro`、`tools/cross-check.pro`、`tests/core-tests.pro`
  - 新增 QML 模块三件套：`qml/qmldir`（**手写**，qmake 不会生成）、`qml/qml.qrc`（把 `/PitchDetector` 前缀的资源编进 exe）
  - 新增 `src/app/theme.h`（**Theme 由 QML 单例改为 C++ 单例**，理由见事实 4）
  - 删除 `qml/Theme.qml`；为 7 个组件 QML 补 `import PitchDetector.App 1.0`
  - 新增 `build-and-run.bat`（一键 qmake + jom + windeployqt + 启动）；`run-app.bat` 改为运行自包含部署版
  - `main.cpp`：QML 加载改为 `addImportPath(":/")` + `loadFromModule`（qrc 布局），并保留磁盘兜底（可用 `PITCH_NO_DISK_QML_FALLBACK` 关闭，用于诊断）
- **事实（全部实测，含大量踩坑）**：
  1. **`-std=c++20` 必须写进 `QMAKE_CXXFLAGS` 且逐个子工程写**：subdirs 顶层的 `QMAKE_*FLAGS` 不传子工程；
     `QMAKE_CXXFLAGS_RELEASE` / `_DEBUG` 会被 mkspec 覆盖，不进最终 `CXXFLAGS`。
  2. **BOM 会让 qmake 静默忽略文件开头的赋值**（坑 A20）。排查过程中"标志写了不生效"的真正原因就是这个：
     我先前用 `Set-Content -Encoding UTF8`（PS 5.1 下写 BOM）批量改过 `.pro`。现象是报 `std::span is not a member of std`，
     与 `.pro` 编码看似毫无关系。
  3. **qmake 用 `-incremental` 不重读 `.pro`**（坑 A22）：只改 `.pro` 内容不增删文件时，Makefile 不重生成，改动静默不生效。
  4. **qmake 无法解析 qrc 里 QML 模块的命名类型**（坑 A21）：界面能创建，但 `Theme` 全部 `ReferenceError`（230 行）。
     同一 qmldir 写法在 CMake 的 `qt_add_qml_module` 下正常（后者生成插件并注册类型表）。
     **处置：Theme 改为 C++ 单例**（`src/app/theme.h` + `qmlRegisterSingletonInstance`），此后零 QML 报错。
  5. **`LIBS` 的相对路径按 `.pro` 所在目录解析**：`tools/cross-check.pro` 里写 `$$PWD/../../lib`（多一级）会解析到
     `<父目录>\lib`，链接时报 `cannot find -lpitch-io`。改为 `$$PWD/../lib`。
  6. **`-Wconversion` 在 Qt 头文件里是噪声**（`qpoint.h`、`qtyperevision.h` 等大量 int→float/quint8 收窄告警）：
     app / audio 两个工程去掉该开关，core / io（不含 Qt 头）保留严格口径。
  7. **qmake 构型带来一个额外收益**：QML 与手写 qmldir 一起编进 exe 资源，**windeployqt 部署后孤岛运行成功**
     （清空 PATH 后 `--qmlcheck` 仍 PASS，63.6 MB 自包含）——上一轮 CMake 版卡住的"自定义 QML 模块部署"问题（坑 A17）就此解决。
- **验证结果**（qmake 版全量复跑）：
  - 构建：`jom` → **0 error / 0 warning**
  - 界面：`--qmlcheck` → **`[PASS] QML 根对象创建成功：1 个`，零 QML 报错**（ReferenceError 从 230 降到 **0**）
  - 文件分析：`--selftest tests/data/expect-A4-440Hz-pure.wav --expect A4` → **A4 命中**
  - 对拍：analysis **4/4**、realtime **4/4**（仍逐位一致）
  - 分层门禁：**三项全 PASS**
  - 部署：`windeployqt --qmldir qml` → **孤岛运行 PASS**
- **依赖**：单元 5、6。

## 单元 8：安装 Qt Multimedia，打通麦克风实时链路

- **目标**：用户要求"Multimedia 没装就装上"。装上并让实时麦克风链路真正可用（不只是编译通过）。
- **改动**：
  - 用 MaintenanceTool **headless 模式**安装组件 `qt.qt6.683.addons.qtmultimedia`
  - `src/audio/qt-audio-source.cpp`：删除 `QAudioFormat::Int8` 分支（Qt 6 已移除该样点格式）
  - `src/app/app.pro`：增加与 `src/audio` 相同的 `qtHaveModule(multimedia)` 判定（subdirs 不传 DEFINES）
  - `src/app/main.cpp`：新增 `--devices` 自检（罗列输入设备与首选格式，验证 Multimedia 运行期可用）；
    设备格式探测改用 setter 构造 `QAudioFormat`（三参构造不存在）
  - `build-and-run.bat`：部署步骤补上 windeployqt 不会自动处理的 Multimedia 运行库与插件
- **事实（全部实测）**：
  1. **装法（可复现）**：
     `<QtRoot>\MaintenanceTool.exe install qt.qt6.683.addons.qtmultimedia --root <QtRoot> --accept-licenses --accept-obligations --accept-messages --confirm-command`
     —— 注意 `--accept-messages` 与 `--default-answer` **互斥**（同时给会直接报错退出）。
     本次装入了 mingw_64 / android_arm64_v8a / wasm_multithread 三条链 + 文档 + 示例。
  2. **装完必须重新 qmake**：`qtHaveModule(multimedia)` 在 qmake 阶段求值（坑 A26）。
  3. **subdirs 的 DEFINES 不联动**（坑 A25）：`src/audio` 判定为 1，但 `src/app` 编译时仍是 0，
     导致 `--devices` 报"构建时未包含 Qt Multimedia"。处置：`app.pro` 里也写一遍判定。
  4. **Qt 6 的 `QAudioFormat` 没有 `Int8`**（坑 A23）：只有 `UInt8/Int16/Int32/Float`，照 Qt 5 记忆写会编译失败。
  5. **`QAudioFormat` 无便捷三参构造**：改用 `setSampleRate/setChannelCount/setSampleFormat`。
  6. **`windeployqt` 不会自动部署 Multimedia**（坑 A24）：它是运行期按需加载的插件式后端，
     不在可执行文件的直接依赖里；必须手工拷贝 `Qt6Multimedia.dll`、`Qt6Network.dll`、`Qt6MultimediaQuick.dll`
     与 `plugins\multimedia\*.dll`。不拷时表现为"源码目录能跑、部署后麦克风不见了"。
     —— 另一条容易踩的坑是**Makefile 陈旧**：删掉 `qml/Theme.qml` 后未强制重生成 Makefile，
     构建报 `dependent '..\..\qml\Theme.qml' does not exist`（坑 A22 同源）。
  7. **本机设备实测**（`--devices`）：
     | 设备 | 首选格式 | 支持 44.1k/单声道/Float |
     |---|---|---|
     | 麦克风 (Redmi 电脑音箱) **[默认]** | 48000 Hz / 2 声道 / Float | 否 |
     | 麦克风 (Steam Streaming Microphone) | 44100 Hz / 1 声道 / Float | 是 |
     默认设备不支持我们的期望格式 → 采集实现会退到设备首选格式并发 `formatMismatch` 提示（设计如此，坑 A3）。
     这条实测正好验证了"格式协商 + 显式提示"这段代码不是空转。
- **验证结果**：
  - `--devices` → **`[PASS] Qt Multimedia 可用且有输入设备`**（2 个设备，FFmpeg 7.1 后端）
  - **部署目录孤岛运行**下 `--devices` 与 `--qmlcheck` **均 PASS**（清空 PATH 后仍可枚举设备）→ 麦克风链路在交付形态下可用
  - 全量回归无退化：界面自检 PASS、文件分析自检 A4 命中、跨语言对拍 analysis/realtime 均 **4/4**、参数同源 **16/16**、分层门禁**三项 PASS**
  - **尚未验证**：真实麦克风收音下的读数（需用户在界面上点"开始监听"——我无法产生声音，也无法代替人看读数）
- **依赖**：单元 7。

## 单元 9：真实钢琴 88 键素材回归（**A1/A2 达成，与上游基线一致**）

- **目标**：用户提供钢琴 88 键素材，要求①补音名后缀②验证 Qt 程序能否正确识别全部音高（上游 web 版仅 4 个音易错）。
- **改动**：
  - 新增 `tools/rename-piano.mjs`（默认预演、`--apply` 执行、冲突即拒绝）
  - 新增 `tools/piano-batch.{h,cpp}` + `piano-batch-main.cpp` + `piano-batch.pro`（88 键批量分析，A1/A2 的执行体）
  - `tools/tools.pro`（subdirs：cross-check + piano-batch）；根 `.pro` 改为引用它
  - `src/io/wav-reader.{h,cpp}`：新增 `readWavMonoW(const std::wstring&)`（宽字符路径入口）
  - `src/app/file-analysis-controller.cpp`、`src/audio/file-audio-source.cpp`、`main.cpp`：改走宽字符入口
  - `main.cpp`：`--selftest` 增加"读取预检"（把"读文件失败"与"分析无有效音高"分开报）
- **事实（全部实测）**：
  1. **素材就地改名完成**：88 个 `tone (N).wav` → `tone (N) - <音名>.wav`（编号 N → MIDI 20+N：1=A0、40=C4、49=A4、88=C8）。
  2. **中文路径是本轮的真正障碍**（坑 A29）：素材目录名 `钢琴88键独立音频文件` 触发三连坑——
     ① `QString::toStdString()` 按本地代码页转 8 位窄字符 → 窄字符 `ifstream` 打不开；
     ② `std::filesystem` 的窄字符接口在中文路径上**直接抛异常**（`Cannot convert character sequence`）；
     ③ Windows 传给 `main` 的 `argv` 是**本地 ANSI 编码**（GBK），当 UTF-8 解析得到乱码 → 报"目录不存在"。
     **最终处置（用户建议，正确且彻底）**：把目录改名为英文短名（`<素材目录>`，纯 ASCII），
     同时代码侧保留宽字符能力（`readWavMonoW` 用 Windows 原生 `CreateFileW`/`ReadFile` 整读入内存后交给同一解析器；
     `piano-batch` 用 `GetCommandLineW` + `CommandLineToArgvW` 取宽字符参数）。
  3. **自定义 streambuf 是个坑**（中途走过弯路）：曾把 `FILE*` 包成 `streambuf` 供 `istream` 用，
     但 WAV 解析依赖 `tellg/seekg` 与 `istream::read()` 的 `gcount` 语义，自定义实现极易出偏差，
     现象是"缺少 fmt 块"（与真正的格式错误无法区分）。最终改为**整读入内存 + 同一个 `parseWav`**，
     两条路径共用同一解析器，不再有流语义风险（单文件约 1.5 MB，代价可接受）。
  4. **`piano-batch` 的进度必须打 stderr**：stdout 重定向到文件时是全缓冲的，后台跑数分钟日志仍 0 字节，
     误以为卡死（实测踩过）。
- **验证结果（A1/A2 达成）**：
  - `bin\piano-batch.exe --dir <素材目录> --csv ... --md ...` → **命中 84 / 88（95.5%）**、
    **命中键偏差中位 6.3 音分**、**含多个音名的文件 36 个**、耗时 444.5 s
  - **与上游 JS 基线逐项吻合**（上游：84/88、偏差中位 6.1 音分、混合音名 36 个）
  - **未命中的正是上游记录的那 4 个键**：`#2 A#0`（众数 F2）、`#3 B0`（众数 B1）、`#36 G#3`（众数 G#4）、
    `#87 B7`（众数 C8，偏差 −47.5 音分）→ 与"web 版只有 4 个音容易出错"的用户印象**完全一致**
  - 逐文件结果：`reports/piano-88.md`（报告）与 `reports/piano-88.csv`（数据）
  - **同源交叉验证**：A0 与 C8 两个样本用**上游 JS 引擎**独立复算，结果与 C++ **逐字段相同**
    （A0：帧 754 / 众数 A0(282) / 中位 156.84 Hz / 14.1 音分 / 音名数 10 / 八度折回 6；
    C8：帧 11 / 众数 C8(10) / 中位 4295.29 Hz / 44.6 音分）→ 证明 A0 的"中位偏高"是**上游行为**而非移植缺陷
  - 应用自身链路（界面"分析"走的同一路径）在 A0 / A4 / C8 三个代表音上均 **PASS**
- **依赖**：单元 8。

## 单元 10：修掉两个用户实测缺陷（麦克风无数据、文件导入失败）

- **目标**：用户实测报两问题——① 点"开始监听"拿不到麦克风数据（只打出 FFmpeg 版本行）② 选音频导入提示"无法载入音频文件"（但文件确实存在）。
- **改动**：
  - `src/audio/qt-audio-source.{h,cpp}`：采集方式由 **`readyRead` 信号驱动改为定时轮询拉取**；`connect` 移到 `start()` 之前；接上 `QAudioSource::stateChanged` 与错误码；新增采集统计（轮询次数/字节/样点/非零比例/块 RMS 峰值/幅度峰值）
  - `src/app/pitch-session-controller.{h,cpp}`：新增 **`adoptSource()`**——所有采集源统一在这里连接信号（原来两处各自 new 源却**漏了 connect**）；新增 `injectSamples()`（测试用）；新增 `chooseAudioFile()` / `chooseAudioFileAndPlay()`（用 C++ 原生 QFileDialog）；新增 `peakRms` / `callbackCount` / `captureStats` 诊断属性；RMS 用 double 累加
  - `src/app/file-analysis-controller.{h,cpp}`：新增 `chooseAndAnalyze()`（同样改用原生 QFileDialog）
  - `src/app/main.cpp`：新增 `--mictest[=秒]`（麦克风采集自检）与 `--looptest`（实时链路自检：注入合成信号走真实链路）
  - `qml/pages/LivePage.qml`、`FilePage.qml`：文件选择改调 C++ 方法；LivePage 增加"采集统计"与"原始信号峰值"显示
  - `src/app/app.pro`：`QT += widgets`（只用 `QFileDialog`）
- **事实（两个根因，都不是"环境问题"）**：
  1. **麦克风没数据的根因是漏了 `connect`**（坑 A30）：`PitchSessionController` 里两处 `make_unique<...AudioSource>()` 之后只调用了 `start()`，没有连接 `samplesReady` 等信号。
     现象极具误导性：状态显示"正在监听"、采集层计数正常（轮询 363 次 / 152 万字节 / 非零样点 76%），
     但**会话层回调次数恒为 0**、读数永远为空——极像"麦克风没声音"。
     定位靠的是新加的"回调次数"计数器：采集层有数据、会话层回调为 0 ⟹ 信号没连上。
  2. **`QAudioSource` 是拉取模型**：其 `QIODevice` 需要主动 `read()`；原实现只连 `readyRead` 且 `connect` 写在 `start()` 之后，
     即便信号连上了也会丢启动瞬间的数据。已改为 `QTimer` 按约 512 样点的间隔轮询 `readAll()`。
  3. **文件导入失败的根因是 QML `FileDialog` 不可靠**（坑 A31）：`selectedFile` 传给 C++ 后打不开文件。
     改用 C++ 侧 `QFileDialog::getOpenFileName`（原生对话框、返回本地路径），并在错误信息里带上**实际路径**便于核对。
  4. **本机麦克风电平极低**：默认设备「麦克风 (Redmi 电脑音箱)」实测块 RMS 峰值 3e-4 ~ 8e-3（正常说话约 1e-2 ~ 1e-1），
     即低约 10~100 倍；幅度峰值 0.02。判定"通道是否可用"要区分两种情形：
     全 0 = 通道没通；非零但极小 = 通道通了但电平太低（麦克风增益/距离/系统静音问题）。
     已把这两类做成诊断输出。
- **验证结果**：
  - `--mictest=5` → **回调 50 次、原始 RMS 峰值 0.0022、`[PASS] 采集到有效信号，通道正常`**
  - `--looptest` → **5 / 5 通过**：440 Hz→A4、82.41 Hz→E2、220 Hz→A3、1046.5 Hz→C6、**27.5 Hz→A0**
    （覆盖音域两端；A0 能测出说明 ADR-0009 的两级窗长在实时链路上确实生效），置信度均 0.999+
  - `--qmlcheck` → PASS，零 QML 报错
  - **部署目录孤岛运行**下三项自检均 PASS（93.2 MB 自包含）
- **依赖**：单元 9。

## 单元 12：Android 构建链打通（含 3 处构建缺陷修复）

- **目标**：同一份 qmake 工程在 Android（arm64-v8a）上编译、链接、出 APK 并在真机运行；桌面行为不得退化。
- **改动**：
  - `src/io/wav-reader.{h,cpp}`：**`readWavMono` 的路径契约定为 UTF-8**（[仓库] `adr/0001-架构与选型.md` ADR-0011）——Windows 分支内部先转 UTF-16 再走既有 `CreateFileW` 路径，非 Windows 直接用窄字符 `ifstream`；取代原来的"宽/窄双入口"。
  - `src/audio/file-audio-source.cpp`、`src/app/file-analysis-controller.cpp`、`src/app/main.cpp`：调用点统一改为 `readWavMono(path.toUtf8().toStdString())`。
  - `src/app/app.pro`：Android 链接静态库时带 ABI 后缀（`android: ANDROID_LIB_SUFFIX = _$${QT_ARCH}`）。
  - `pitch-detector-APP.pro`：只在 `!android` 作用域里加入 `tools`、`tests` 两个子工程。
- **事实（全部实测，含三条"看起来像玄学"的根因）**：
  1. **守卫不对称**：`readWavMonoW` 的声明与实现都在 `#if defined(_WIN32)` 内，而 3 个调用点在守卫之外 → Windows 编得过，Android 报 `use of undeclared identifier 'readWavMonoW'`（坑 A38）。
  2. **静态库 ABI 后缀**：qmake 的 `mkspecs/features/android/android.prf:45-47` 给静态库 TARGET 追加 `_$$QT_ARCH`（产物 `libpitch-core_arm64-v8a.a`）；而链接器的 `-l` 只按 `lib<name>.a` 查找，于是命中了**桌面那份** `libpitch-core.a`（MinGW 符号是 `St4span`，Android 是 libc++ `__ndk1::span`）→ 表现为"符号明明在库里却报一堆 undefined symbol"（坑 A37）。
  3. **子工程连带编译**：顶层 SUBDIRS 含 `tools/tools.pro` 与 `tests/core-tests.pro`，两者在非 Windows 上编不过（`readWavMonoW`/`toWidePath` 只在 `_WIN32` 里定义），且在手机上没有任何意义 → Android 作用域内排除。
  4. **改 `.pro` 后必须显式重跑 qmake**（与坑 A22 同源）：本次实测 jom 因 `if not exist Makefile` 复用了旧 Makefile，链接阶段仍在用不带后缀的旧 `LIBS`，删掉 `Makefile*` 重新 qmake 后才通过。
  5. **环境版本必须成对**：Qt Creator 用 **20.0.0**（9.0.0 自带的 `sdk_definitions.json` 版本表只到 Qt 6.4，对 Qt 6.8 会兜底要求 2021 年的 `ndk;25.1.8937393`）；Android 侧 `cmdline-tools` 用 **12.0**（23.0.0 已改名 "Android CLI"，`sdkmanager --list` 输出斜杠包名，Qt Creator 解析不到，坑 A39）；NDK 用 **26.1.10909125**（`libQt6Core_arm64-v8a.so` 内 clang 指纹 `r487747d/10552028` 与本机 NDK 一致）。
- **验证结果**：
  - **Android 交叉编译**：`qmake <repo>/pitch-detector-APP.pro -spec android-clang "ANDROID_ABIS=arm64-v8a" …` + `jom` → **0 error**；产出 [仓库] `bin/libpitch-detector-APP_arm64-v8a.so`，`llvm-readelf -h` 实测 `ELF64 / DYN / AArch64`（417 KB）。
  - **桌面回归**：`jom` **0 error**；用**中文路径**素材（[素材目录] `tone (49) - A4.wav`）跑 `bin\pitch-detector-APP.exe --selftest … --expect A4` → 预检读取成功（396900 样点 / 44100 Hz）、众数音名 **A4**（442.11 Hz，+8.3 音分）、退出码 0 → 证明新的 UTF-8→UTF-16 路径没有丢坑 A29 的能力。
  - **真机**：APK 在 Qt Creator 20 侧构建并在手机 `3XQ0225B04012528` 上成功运行（用户手动验证，2026-09-30）。
- **依赖**：单元 8、9。

## 单元 13：真机麦克风打不开 + "格式不一致"提示的修复

- **目标**：手机端真正采到麦克风；并消除"设备实际格式与请求不同"这类**本可避免**的提示。
- **现象（用户真机实测）**：① 点"开始监听"报"无法打开音频输入：QAudioSource::start() 返回空，错误码=0" ② 界面长期挂着"注意：设备实际格式与请求不同（请求 44100 Hz/1 声道，实际 48000 Hz/2 声道）"。
- **改动**：
  - 新增 [仓库] `android/AndroidManifest.xml`（从 `<QtRoot>/6.8.3/android_arm64_v8a/src/android/templates/AndroidManifest.xml` 原样复制后再改），显式声明 `RECORD_AUDIO` 与 `uses-feature android.hardware.microphone required="false"`，包名定为 `org.pitchdetector.app`；`src/app/app.pro` 加 `ANDROID_PACKAGE_SOURCE_DIR = $$PWD/../../android`（qmake **没有**权限变量，权限只能进自定义清单）。
  - `src/audio/qt-audio-source.cpp`：`start()` 开头按平台申请**运行期权限**（`QMicrophonePermission` + `QCoreApplication::checkPermission/requestPermission`，授权后自动按原参数重试）；采样率**在请求阶段就与设备对齐**（设备不支持请求值则直接用设备首选值）；实际格式一律取 `QAudioSource::format()` 真值；"打开设备"抽成 `openDevice()`，错误码为 0 时附带平台相关的排查提示。
  - `src/audio/file-audio-source.cpp`：删除文件回放时的 `formatMismatch`（采样率由文件决定，谈不上"协商"）。
- **事实（全部实测）**：
  1. **清单权限本来就有**：`RECORD_AUDIO` 会被 androiddeployqt 按 `lib/Qt6Multimedia_arm64-v8a-android-dependencies.xml` **自动注入**（改动前生成的旧 APK 清单里实测已存在）。真因不是"没声明"，而是 Android 6+ 的**运行期权限没申请**——Qt 侧表现就是 `start()` 返回空 + 错误码 0，毫无线索（坑 A41）。
  2. **`isFormatSupported()` 偏保守**：旧实现用它预判，判否即退让并发提示；而该提示本可避免——算法与采样率无关（τ 换算按实际采样率），采样率完全可以先对齐再请求（坑 A42）。
  3. **自定义清单确实生效**：用 `--no-build` 检查会看到**上一轮遗留的旧清单**而误判"没生效"；`androiddeployqt --gradle` 重建后，`<build>/AndroidManifest.xml` 实测为 `package="org.pitchdetector.app"` + `RECORD_AUDIO`。**核验这类产物必须先删旧目录再重建**。
- **验证结果**：
  - Android 交叉编译 **0 error**；`jom install` + `androiddeployqt --gradle` → `BUILD SUCCESSFUL in 3s`，产出 `android-build-debug.apk`（35.96 MB，[PC] 临时构建目录）。
  - 桌面 **0 error**；中文路径 `--selftest` 仍命中 A4 → 采集层改动未破坏既有能力。
  - **真机麦克风待用户复验**（`verify.md` C14/C15）：首次点"开始监听"应弹出系统录音权限框。
- **依赖**：单元 12。

## 单元 14：存储访问诊断（回答"为什么手机上选不到某些目录的音频"）

- **目标**：用户实测"选择文件时内部存储的 `Android` 目录里只有 media 与 obj"→ 把"目录被系统封锁"与"应用没权限"这两个成因**分别测出来**，并据此判断能否做到"看到手机所有文件"。
- **改动**：
  - 新增 `src/app/storage-access.{h,cpp}`（注册为 QML 单例 `Storage`）：① 读"所有文件访问权限"状态（`Environment.isExternalStorageManager()`，先按系统版本判定、仅 API 30+ 才调，避免低版本 `NoSuchMethodError`）② 逐个探测目录（存在性 / 目录标志 / **可读标志** / **条目数** / 头 8 项）③ 在 `/sdcard/Android/data` 下找第一个 `.wav` 并用真实读取器 `readWavMono()` 读一遍 ④ 报告写入 `files/storageprobe.txt`（手机端唯一可读通道，同 `--mictest` 的理由）⑤ `requestAllFilesAccess()` 跳系统设置页（`MANAGE_APP_ALL_FILES_ACCESS_PERMISSION`，失败回退全局页）；JNI 异常用 `QJniEnvironment::checkAndClearExceptions()` 清掉，不让它把进程打崩。
  - `android/AndroidManifest.xml`：为本次验证加 `MANAGE_EXTERNAL_STORAGE`（`tools:ignore="ScopedStorage"`）。
  - `qml/pages/DebugPage.qml`：新增「存储访问」卡片（当前权限状态 / 去系统授权 / 重新探测 / 报告原文），全部走 D+E 令牌与既有卡片写法。
  - `src/app/app.pro` + `src/app/main.cpp`：登记新源文件与单例。
- **实测结论（同机 A/B，详见 ADR-0015 与坑 A45）**：**`MANAGE_EXTERNAL_STORAGE` 打不开 `Android/data` 与 `Android/obb`**（授权前后都是"可读=否 0 项"，而 `isExternalStorageManager()` 已为 true）；它只放宽了公共目录里**非媒体文件**的可见性（`/sdcard` 15→18 项、`Download` 3→5 项）。用户 QQ 收到的 2 个 `.wav` 恰在 `Android/data` 里 → 任何权限都读不到，"看到所有文件"在无 root 时不可实现。
- **验证结果**：桌面 `jom` 0 error、`--qmlcheck` 0 错误、`tools/check-theme.ps1` PASS、`tools/check-layering.ps1` PASS；Android 交叉编译 0 error；APK 安装成功并完成授权前/后两次真机探测，另用最新一版 APK（16:51:58）复测：唤醒屏幕后 1 秒内写出报告、内容与当前状态一致；`aapt2 dump permissions` 确认清单里已**无** `MANAGE_EXTERNAL_STORAGE`。
- **用户决定（2026-09-30）**：验证完毕后**删除 `MANAGE_EXTERNAL_STORAGE` 与「去系统授权」按钮**（实测换不到那两个目录，属高风险权限），`StorageAccess` 只保留"探测 + 报告"能力；B 档（应用内自建浏览器）暂不做。
- **踩坑**：A46（桌面构建验证不到 `#ifdef Q_OS_ANDROID` 分支；构建脚本未"失败即停"导致 `adb install` 装的是旧包）、A47（屏幕熄灭时 adb 启动应用，进程在但 `main()` 不推进，看起来像写文件失败；撤销 appops 必须带 `--uid`）。
- **依赖**：单元 12 / 13。

## 单元 15：节拍器（新页面 F6）

- **目标**：独立页面做节拍器；内置节拍音 + 用户上传自定义音频；拍号可选；**每拍可单独细分**（ADR-0016）。
- **分层落位（按 ADR-0002，逻辑与 Qt 分离）**：
  - `src/core/metronome-pattern.{h,cpp}`：拍号 + 逐拍细分模型 → 每小节的点击事件表（位置/所属拍/角色/是否强拍）；
    夹紧、配置串 `toString`/`fromString`（QSettings 用）、预置拍号（12 种）、**复合拍号重音分组**（6/8→1,4 等）
  - `src/core/tap-tempo.{h,cpp}`：点击测速（取最近 4 个间隔平均、停顿 >2 秒重新开始、过快/过慢间隔不参与）
  - `src/core/click-voice.{h,cpp}`：点击音合成（基音+第二分音+噪声，**无状态哈希噪声**保证可复现）与自定义样本混音
    （不同采样率时线性重采样）；**按角色目标峰值归一化**（强拍 0.95 / 弱拍 0.80 / 细分 0.45）
  - `src/core/metronome-renderer.{h,cpp}`：时间轴调度（绝对拍位 double、活跃点击固定 8 槽、渲染中不分配内存）
  - `src/core/audio-packer.{h,cpp}`：最终打包（总增益 0.8 + 声道复制 + **硬夹紧**）——下沉到 core 是为了能被自检直接测
  - `src/audio/metronome-engine.{h,cpp}`：`QAudioSink` 拉模式 + 自造 `QIODevice`；设备格式优先"单声道 Float32"，
    不支持则退到设备首选并做转换；拍点通过**原子量**单向发布给界面线程（音频回调里不加锁）
  - `src/app/metronome-controller.{h,cpp}`：QML 单例 `Metronome`（速度/拍号/细分/音色/播放/拍点指示/持久化/自定义音频加载）
  - `qml/pages/MetronomePage.qml` + 新组件 `Chip` / `IconButton` / `ValueSlider`；底栏新增第 2 个导航位（共 5 个）
- **实测（全部为自动核对）**：
  - `--metrocheck` **85/85 通过**：同角色起点偏差极差 0–1 样点（≤21 µs）；512 vs 4096 块长**逐位相同**；
    密集重叠下独立重建与实测最大差 **0**；自定义样本与「素材 × 增益」最大差 **0**；单次点击峰值精确为 0.95/0.80/0.45；
    打包（夹紧/立体声复制/Int16 量化/缓冲不足）逐项正确；配置串往返稳定、越界值被夹紧；
    **重音拍位** 4/4→[1]、6/8→[1,4]、9/8→[1,4,7]、12/8→[1,4,7,10]、7/8→[1]；**点击测速** 8 个用例（等间隔 120、
    不均匀取平均 122、过快间隔忽略、停顿重开、非单调时间戳不给速度）
  - `--metrolive=3`：真实声卡链路可用（`Digital Audio (S/PDIF)`，48000 Hz/2ch/Float32，3 秒送出 151552 帧、37 次回调）
  - 界面：`--qmlcheck` 0 错误/0 警告；`--uishot --page metro` 像素核对深/浅两套令牌齐全，底栏节拍器图标按强调色渲染
  - 门禁：`check-theme` / `check-layering` / `check-paths` / `check-ascii-bat` 全 PASS
- **验证工具本身踩的坑**：A49（起点检测必须先用包络；信号首样点就发声时要补前置静音；期望值按样点数反推）、
  A50（重叠严重时包络无法分辨 → 改用"独立重建逐样点比对"）、A48（单次点击峰值必须归一化）、A51（`QAudioDecoder::error` 重载）、
  **A52（只改静态库后 `jom` 不会重链 app → 验证到的是旧代码，必须核对 exe 比 .a 新）**、A53（分组为 1 时取模恒真 → 每拍都成重音）
- **依赖**：单元 12（Android 链）、单元 13（权限与格式协商）。

## 单元 11 起：待用户验收与后续

| 计划单元 | 内容 | 前置 |
|---|---|---|
| 11 | **实时麦克风监听人工验收**（用户下一步）：对着乐器/人声实测；本机默认麦克风电平偏低，建议换 Steam Streaming Microphone 或靠近麦克风 | 用户运行 `build-and-run.bat` |
| 12 | 端到端延迟实测并记录（spec D3；ADR-0009 的切换条件依赖它） | 单元 11 |
| 13 | Android 套件配置与真机验证（Multimedia 的 Android 链已就位；仍需 NDK / cmdline-tools / JDK） | 用户拍板 |
| **16** | **节拍器后续完善（F6-a/b/c，用户已点名，等他说"完善节拍器功能"再开工）**：逐拍重音开关、逐拍静音、时值取值扩到 1–6（附点/不等分单列第二阶段）；同时按 F6-d 做配置串的版本化演进（旧串必须仍可解析） | 单元 15；**需求与验收见 `spec.md` 的「F6 增补」表** |
| **17** | **录音的实时分析**（ADR-0017 遗留）：录音过程中把采集到的样点喂进分析链路，让卷帘随录制增长。设计要点：`QMediaRecorder` 不暴露样点 → 需并行开 `QtAudioSource`；**必须先确认真机能否同时被两个客户端打开麦克风**，不能则退化为"停止后分析整段" | 单元 15/18；**只能在手机上验证（本机无输入设备）** |

## 单元 18：录音分析页重构（钢琴卷帘 / 长图导出 / 主流格式 / 现场录音）

- **目标**（用户 2026-09-30 要求）：实时页删掉"近 5 秒曲线"与"选择音频文件"；音域页并入实时页；录音分析页按钮统一为实时页风格并加图标；导入支持 mp3/wav 等主流格式；曲线区改为**钢琴卷帘**（左键位图、右音高曲线、横轴时间秒）；新增录音（开始/暂停继续/停止清空/保存 mp3）；导出 CSV 改为**导出长图**。
- **改动**：
  - `src/app/piano-roll-renderer.{h,cpp}`（新增）：几何计算 + 卷帘渲染（左键盘列贴 `resources/piano/*.svg`、右曲线、底部时间轴）+ PNG 保存；配色由 `rollPaletteFromTheme(ThemeProvider&)` 现算（不引入第二处颜色定义）
  - `src/app/piano-roll-image-provider.{h,cpp}`（新增）：`image://pianoroll/<revision>` → QML 的 `Image` 直接显示 C++ 渲染结果；`FileAnalysis.rollRevision` 自增即触发换图
  - `src/audio/audio-file-decoder.{h,cpp}`（新增）：两级解码（PCM WAV 直读 → `QAudioDecoder`），供文件分析与节拍器自定义音色共用（消掉了原来那两份重复解码代码）
  - `src/audio/audio-recorder.{h,cpp}` + `src/app/recorder-controller.{h,cpp}`（新增）：`QMediaRecorder` 录音，容器协商（MP3 → Mpeg4Audio → Wave），录到临时文件；保存=复制；停止=删除临时文件（清空）
  - `src/app/file-analysis-controller.{h,cpp}`：解码移到主线程、分析仍在工作线程；新增卷帘属性与 `exportRollImage()`；**删除 `exportCsv()` 与 `FrameTableModel::toCsv()`**
  - `qml/pages/FilePage.qml`：整体重写（四组 ActionButton 带图标 + 卷帘横向 Flickable + 摘要 + 逐帧表）
  - `qml/pages/LivePage.qml`：删曲线卡与选文件按钮，新增「本次音域」卡（原音域页内容）
  - 删除 `qml/pages/RangePage.qml` 与 `qml/components/PitchCurve.qml`（已无引用），同步清 `qmldir`/`qml.qrc`/`Main.qml`（导航回到 4 项）
  - 新增自检 `--rollcheck`（解码→分析→渲染→像素断言→PNG）与 `--recformats`（后端编解码能力）
- **实测**：`--rollcheck` **13/13**（图 804×82、白键行亮度 244 / 黑键行 35、曲线像素 2987、时间轴文字 230、抽样颜色 162）；`--recformats` 证明**后端支持 MP3 编码**；`--qmlcheck` 0 错误 0 警告；四道门禁全 PASS；桌面 0 error / 0 warning。
- **未完成/未验证**：录音的麦克风路径（本机无输入设备）、压缩格式的端到端解码（本机无编码器造样本）、录音的实时分析（需先确认真机能否双客户端开麦）。
- **踩坑**：A54（SVG 注释 `*/` 结尾导致素材失效）、A55（`computeRollGeometry` 参数顺序传反、被"独立反推期望值"的断言抓出）、A56（`Set-Content -Encoding UTF8` 写 BOM 破坏 qmldir/qrc，坑 A20 复发）。

## 遗留与未完成

| 项 | 说明 |
|---|---|
| 待用户拍板 6 项 | 见 `spec.md` 第四节与 `design/architecture.md` 第六节 |
| 环境未就绪 | Qt 安装中；构建/测试命令未实测，`AGENTS.md` 相关表格待回填 |
| 上游实时链路从未验证 | 上游 `verify.md` 的 C1–C7、V1–V8、M3–M7 全部为"待验"；本项目计划用 `FileAudioSource` 把实时逻辑做成可自动回归 |
| 会话开始时的空目录 | 会话开始时的工作目录是一个空目录，用户已确认真实项目在别处；该空目录**未做任何操作**，由用户决定是否删除（具体路径不写进仓库，见坑 A32） |

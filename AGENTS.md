# pitch-detector-APP — AI 协作须知（AGENTS.md）

> 只写 **AI 猜不到的硬事实**（命令、路径职责、硬约定、禁区）。通用行为规范在 `ai-rules/`，不在此重复（R7 SSOT）。

## 协作规则

- 行为规则路径：`<rules-repo>`（`rules/R1`–`R11`、`roles/`）；该库跨项目通用、**不在本仓库内**，位置由使用者提供
- 该库不可见时，先让用户粘贴 `templates/dialogue-opening.md` 规则段再开工
- 项目文档位置与维护方式见 `ai-rules/rules/R6-feature-docs.md`
- 本仓库 git 边界：**不做任何 git 写操作**，改动留工作区（R4）；只读白名单见 R4

## 项目一句话

`pitch-detector` 网页版（单文件 HTML + Web Audio + JS/YIN）的 **Qt 重写版**，目标形态：手机 APP + 电脑桌面客户端 + 嵌入式设备预留。UI 用 QML，算法与流程用 C++，跨平台同一份代码。

## 上游项目（只读，勿改）

**位置**：与本仓库**互为兄弟目录**的 `pitch-detector-web/`。仓库内不写死它的绝对路径
（两台开发机的磁盘与父目录不同，见「环境与路径约定」）。所有 Node 工具通过
`tools/_path-policy.mjs` 自动定位：先看环境变量 `PITCH_WEB_ROOT`，再从本仓库位置**逐级向上**找 `pitch-detector-web`。

| 项 | 值 |
|---|---|
| 原项目根 | `[上游]`（= 兄弟目录 `pitch-detector-web/`，**只读参考，禁止修改**） |
| 唯一算法真值（JS） | `[上游] tools/pitch-engine.js`（492 行，UMD） |
| 原项目文档 | `[上游] dev-docs/pitch-detector/`（context / pitfalls #1–#29 / ADR-0001 / spec / verify / dev / debug / audio-test-report） |
| **钢琴素材（回归基准）** | **存在**（2026-09-30 复核）：`[上游] resource_audio/870f3-main/钢琴88键独立音频文件/`，88 个 WAV。注：早期记录误判为"已缺失"，坑 A9 已更正 |
| Node 参考工具 | `tools/verify-engine.mjs`、`tools/test-single-note.mjs`、`tools/wav-read.mjs`（**跨语言一致性验证的对照端**） |

- **算法参数不得凭记忆或推测落地**：一律回查 `pitch-engine.js` 原文（R2：数值须有实据）
- 本项目的测试真值由 `node tools/gen-test-fixtures.mjs` 生成（读上游引擎算真值），产物在 `tests/data/`
  - 逐帧数值为 **float64 原始字节**（`*.f64`，已 gitignore，可重新生成）；音名真值 `*.note` 与清单 `reference.txt` 为文本，**保留跟踪**

## 环境与路径约定（**两台机器开发，必读**）

本项目在**两台电脑**上开发，唯一稳定的事实是：**`pitch-detector-app/` 以下的目录结构一致**，
而仓库所在的盘符与父目录**不同**。因此：

| 规则 | 允许的写法 | 禁止的写法 |
|---|---|---|
| 构建 | qmake 变量 `$$PWD` / `$$OUT_PWD`；批处理 `%~dp0` | 任何盘符绝对路径 |
| Qt 位置 | 环境变量 `PITCH_QT_ROOT`，或从 `PATH` 上的 `qmake.exe` 反推（`build-and-run.bat` 已实现） | 写死 `D:\devtools\qt` |  <!-- path-check: ignore 本行是“禁止写法”的示例本身 -->
| 上游仓库 | 环境变量 `PITCH_WEB_ROOT`，或向上找兄弟目录（`tools/_path-policy.mjs`） | 写死上游绝对路径 |
| 素材目录 | 命令行参数注入本机实际位置 | 把素材路径写进源码/文档 |
| 文档 | 占位符 `<repo>` / `<QtRoot>` / `<上游>` / `<素材目录>` / `<rules-repo>` | 写盘符 |

- **机器门禁**：`node tools/check-paths.mjs` —— 发现盘符绝对路径即 FAIL（退出码 1）。
  改动路径相关文件后应运行它；新增门禁须按坑 A13 做负向验证。
- **批处理编码门禁**：`node tools/check-ascii-bat.mjs` —— `.bat`/`.cmd` 必须纯 ASCII。
  cmd.exe 按字节解码 `.bat`，一个中文字符会破坏**下一行**的解析（实测可致 `set PATH` 被撕碎、
  构建静默不开始），且 UTF-8 BOM 与 `chcp 65001` 都救不了（坑 A34）。中文说明一律放 `.md`。
- 环境变量的含义与解析顺序，唯一出处是 `tools/_path-policy.mjs`（Node 侧 SSOT），
  批处理侧 `build-and-run.bat` 用同一套顺序；改规则时两处必须同步。
- 环境变量是**进程启动快照**：`setx` 只改注册表，当前已开的终端/宿主进程看不到，
  必须重开终端才生效（坑 A36）——排查"探测逻辑是否有效"时不要被这一点误导。

### 换到另一台电脑要做什么（**不需要改任何项目文件**）

1. `git clone`（或整目录拷贝）本仓库到任意位置；若要用到上游算法真值，把 `pitch-detector-web`
   放在与本仓库**互为兄弟**的父目录下（或设 `PITCH_WEB_ROOT`）。
2. 装 Qt 6.8.3（MinGW 64-bit）+ Qt Multimedia。若 Qt 不在 PATH：
   `setx PITCH_QT_ROOT <你的Qt安装根>`，然后**重开终端**（A36）。
3. 装 Qt Creator 后，若要它认出编译器和 Kit：把 `<QtRoot>/Tools/mingw1310_64/bin` 与
   `<QtRoot>/6.8.3/mingw_64/bin` 加入 PATH，重启 Qt Creator；需要"添加 Qt 版本"时在
   `Preferences > Kits > Qt Versions > Add...` 里选 `qmake.exe`（**别用命令行改配置**，坑 A33）。
4. 跑 `build-and-run.bat`（自动探测 Qt，不需要改文件）。
5. **不要**从另一台机器拷贝 `Makefile*`、`.qmake.stash`、`obj/`、`moc/`、`bin/`、`lib/`、`run/`
   —— 它们含那台机器的绝对路径。删掉后重新 qmake + 构建即可（第 4 步会做）。

> 实测（2026-09-30）：把仓库复制到**另一个盘符下的不同目录名**（临时路径，测完已删除）、
> 排除所有构建产物后运行 `build-and-run.bat`：**0 error / 0 warning**，4 个 exe 全部产出，
> `run\` 部署 1358 个文件，`--qmlcheck` / `--looptest`(5/5) / `core-tests`(109 项 / 失败 0) 全通过，
> **期间未修改任何项目文件**。同时验证了三种探测情形：设 `PITCH_QT_ROOT` → 走变量分支；
> 不设变量但 PATH 含 qmake → 自动反推成功；两者都没有 → 明确报错退出（不会静默用错目录）。
- **换机器/换目录后必须重新 `qmake`**：`Makefile*`、`.qmake.stash`、`obj/`、`moc/`、`bin/`、`lib/`、`run/`
  都含**本机**绝对路径，是生成物（已 gitignore）。跨机器不要拷贝这些目录，删掉后重新生成即可。

### 本机 Qt 环境（示例值，仅供理解结构）

```
<QtRoot>/6.8.3/mingw_64                 Qt 6.8.3 本体（含 Qt Multimedia）
<QtRoot>/Tools/mingw1310_64/bin         Qt 自带 MinGW GCC 13.1.0
<QtRoot>/Tools/jom/jom.exe                 并行 make（缺失时回退 mingw32-make）
```

> **Qt Creator 找不到编译器时**：Qt Creator 是通过 **PATH** 自动探测 MinGW 的。
> 把 `<QtRoot>/Tools/mingw1310_64/bin` 与 `<QtRoot>/6.8.3/mingw_64/bin` 加入 PATH 后重启 Qt Creator，
> 工具链即被自动探测（`TargetAbi = x86-windows-msys-pe-64bit`）。
> 需要"添加 Qt 版本"时在 `Edit > Preferences > Kits > Qt Versions > Add...` 里选 `qmake.exe`——
> 命令行/SDK 工具写配置文件不可靠（坑 A33）。

## 构建 / 运行 / 测试

> **构建系统：qmake**（用户指定，不用 CMake；工程文件为根目录 `pitch-detector-APP.pro` + 各子目录 `.pro`）。
> **工具链（2026-09-30 实测可用，本机）**：
> · Qt 6.8.3 `<QtRoot>/6.8.3/mingw_64`（含 Qt Multimedia）
> · **编译器用 Qt 自带的 MinGW 13.1.0** `<QtRoot>/Tools/mingw1310_64/bin`
> · 并行 make 用 jom `<QtRoot>/Tools/jom/jom.exe`（Qt Creator 20 起 jom 随 Qt SDK 装在 `Tools/jom`，旧的 `Tools/QtCreator/bin/jom` 已随 Qt Creator 9 一起删除）
> · **不要用 WinLibs 的 g++ 编 Qt 部分**（UCRT 堆不一致，实测崩溃在 `QString::toStdString` 的 `RtlFreeHeap`）
> · **不必手工设 PATH**：`build-and-run.bat` 自己探测 Qt（`PITCH_QT_ROOT` → PATH 上的 qmake 反推）
> · 纯命令行构建时先设 `<QtRoot>` 并把它的两个 bin 目录加入 PATH

| 用途 | 命令（`<repo>` = 仓库根，`<QtRoot>` = Qt 安装根） | 状态 |
|---|---|---|
| **一键构建+部署+运行** | 双击 `<repo>\build-and-run.bat`（自动探测 Qt） | **已实测**（2026-09-30 全新克隆式构建：0 error / 0 warning / 9 s） |
| **一键运行（已部署版）** | 双击 `<repo>\run-app.bat` | **已实测**（自包含，不需 Qt 在 PATH） |
| 生成工程 | `<QtRoot>/6.8.3/mingw_64/bin/qmake.exe pitch-detector-APP.pro CONFIG+=release` | **已实测通过** |
| 构建 | `jom`（或 `mingw32-make`） | **已实测通过**：0 error / 0 warning |
| **路径卫生门禁** | `node tools/check-paths.mjs` | **已实测**：拦截机器相关绝对路径（坑 A32） |
| **批处理编码门禁** | `node tools/check-ascii-bat.mjs` | **已实测**：.bat/.cmd 必须纯 ASCII（坑 A34） |
| **界面风格门禁** | `powershell -ExecutionPolicy Bypass -File tools/check-theme.ps1` | **已实测**：qml/ 里不得出现十六进制色值/裸字号/裸圆角；已做负向验证（ADR-0012） |
| **移植性实测** | 换路径后重新构建（见下） | **已实测**：异路径 + 仅靠 PATH 反推 → 0 error / 0 warning / 部署 1358 文件 |
| 生成测试真值 | `node tools/gen-test-fixtures.mjs` | 已实测通过（**须先备好上游仓库**，见「环境与路径约定」） |
| 生成 88 键矩阵真值 | `node tools/gen-88key-matrix.mjs` | 已实测通过（176 用例，上游命中 174/176；约 253 s） |
| 参数同源检查 | `node tools/compare-params.mjs` | **已实测通过**（16/16 一致） |
| 分层门禁 | `powershell -ExecutionPolicy Bypass -File tools/check-layering.ps1` | 已实测通过（三项全 PASS） |
| 单元测试 | `bin\core-tests.exe --data tests\data` | 已实测通过（T1–T5 免真值项 79/81；T6/T7 需先跑真值生成） |
| 跨语言对拍 | `bin\cross-check.exe --data tests\data --mode=analysis`（或 `--mode=realtime`） | 已实测通过：4/4 且逐位一致 |
| 界面自检（无头） | `bin\pitch-detector-APP.exe --qmlcheck` | **已实测通过**（2026-09-30）：QML 根对象创建成功、零 QML 错误 |
| **界面截图（配色/图标核对）** | `bin\pitch-detector-APP.exe --uishot <png>` | **已实测通过**：渲染成 PNG（400×860）后退出；用像素抽样核对令牌色是否真的生效（坑 A44） |
| 实时链路自检（无头） | `bin\pitch-detector-APP.exe --looptest` | **已实测通过**（2026-09-30）：5/5（27.5 Hz A0 … 1046.5 Hz C6） |
| **节拍器离线自检**（无头，不碰声卡） | `bin\pitch-detector-APP.exe --metrocheck`（可加 `--metrowav <wav>` 导出试听文件） | **已实测通过**（2026-09-30）：**65/65**。核对点击位置（同角色抖动 ≤1 样点）、块长无关性（逐位相同）、重叠下按期望重建（最大差 0）、自定义样本、打包与夹紧、拍号模型 |
| **节拍器真实输出自检** | `bin\pitch-detector-APP.exe --metrolive=3` | **已实测通过**（2026-09-30）：48000 Hz/2ch/Float32，3 秒 151552 帧、37 次回调（会真的发声） |
| **指定页面截图** | `bin\pitch-detector-APP.exe --uishot <png> --page metro`（可加 `--theme light`） | **已实测通过**：用于按像素核对某个页面的令牌色与图标着色 |
| 音频设备自检 | `bin\pitch-detector-APP.exe --devices` | **已实测通过**（2026-09-30）：FFmpeg 7.1 后端，设备因机器而异 |
| 文件分析自检（无头） | `bin\pitch-detector-APP.exe --selftest <wav> --expect <音名>` | 已实测通过（A4 / A3 素材均命中） |
| 部署 | `windeployqt --release --qmldir qml ... run\pitch-detector-APP.exe` **＋ 手工补 Qt Multimedia** | 已实测通过（孤岛运行 PASS，含设备枚举） |

### Android 构建（2026-09-30 实测打通，arm64-v8a）

- **Qt Creator 用 20.0.0**（`<QtRoot>/Tools/QtCreator-20/bin/qtcreator.exe`）；9.0.0 已删除——它自带的 `android/sdk_definitions.json` 版本表只到 Qt 6.4，对 Qt 6.8 会兜底要求 2021 年的 `ndk;25.1.8937393`
- **Qt for Android 套件**：`<QtRoot>/6.8.3/android_arm64_v8a`（含 qtmultimedia）。注意它**没有 `bin/qmake.exe`**，只有包装脚本 `bin/qmake.bat`（内部用 host qmake + `target_qt.conf`）
- **版本必须成对（缺一即失败，详见 pitfalls A39）**：JDK **17** / `cmdline-tools` **12.0**（新版 23.0.0 的 `sdkmanager --list` 用斜杠包名，Qt Creator 解析不到 platform/build-tools）/ `build-tools;34.0.0` / `platforms;android-34` / `ndk;26.1.10909125`（与 Qt 6.8.3 官方 Android 库同一 NDK）
- **SDK 根目录**：由环境变量 `ANDROID_SDK_ROOT` 与 Qt Creator 的 `[AndroidConfigurations] SDKLocation` 提供；仓库内不写盘符。`sdkmanager` 必须落在 `<AndroidSdk>/cmdline-tools/latest/bin/sdkmanager.bat`（Qt Creator 只认这一处），且必须用 JDK 17 运行
- **Gradle**：Qt 不带本体，首次构建从 `gradle-wrapper.properties` 的 `distributionUrl` 下载（Gradle 8.10）；国内直连实测仅 ~75 KB/s，按 pitfalls A40 改成镜像
- **本仓库的 Android 特有两件事**：① 静态库带 ABI 后缀（`libpitch-core_arm64-v8a.a`），链接处必须带后缀（坑 A37）② 顶层 `.pro` 的 `!android` 作用域排除了 `tools`/`tests`（它们在手机上无意义且在非 Windows 编不过，坑 A38）
- **录音权限**：`android/AndroidManifest.xml` 声明 `RECORD_AUDIO`（由 `src/app/app.pro` 的 `ANDROID_PACKAGE_SOURCE_DIR` 引入），**运行期**由 `QtAudioSource::start()` 用 `QMicrophonePermission` 申请——首次点「开始监听」会弹系统权限框。若出现"`QAudioSource::start()` 返回空、错误码=0"，先查权限：清单里声明 ≠ 已授权（坑 A41）
- **命令行等价流程**（影子构建，不污染仓库）：`qmake <repo>/pitch-detector-APP.pro -spec android-clang "ANDROID_ABIS=arm64-v8a" "ANDROID_SDK_ROOT=…" "ANDROID_NDK_ROOT=…" "ANDROID_SDK_BUILD_TOOLS_REVISION=34.0.0"` → `jom` → 产出 `<repo>/bin/libpitch-detector-APP_arm64-v8a.so`（用 `llvm-readelf -h` 应报 `AArch64`）；打 APK 由 Qt Creator 的「构建安卓 APK」步骤完成（先 `make install INSTALL_ROOT=…` 再调 androiddeployqt）
- **命令行构建的两个实测坑**：① `android_arm64_v8a/bin/qmake.bat` 要求 `ANDROID_NDK_ROOT` / `ANDROID_SDK_ROOT` / `JAVA_HOME` 以**环境变量**形式存在——只当 qmake 参数传会报 `You need to set the ANDROID_NDK_ROOT environment variable` 并连带说 mkspec 读不了 ② `androiddeployqt.exe` 在 **host 侧** `<QtRoot>/6.8.3/mingw_64/bin/`，**不在** `android_arm64_v8a/bin/`。脚本务必"失败即停"，并在安装前**打印 APK 时间戳**（否则旧包会安静地安装成功，坑 A46）
- **`#ifdef Q_OS_ANDROID` 分支桌面构建不参与编译**：改了 JNI 代码（`QJniObject` / `QNativeInterface::QAndroidApplication` 等）必须跑一次 Android 目标构建才算验证过（坑 A46）
- **真机自动化检查前先唤醒屏幕**：屏幕熄灭（`dumpsys power` 的 `mWakefulness=Asleep`）时 `adb shell am start` 起的进程**能出现在 `ps` 里但 `main()` 不推进**，启动期写的诊断文件不会出现——看起来像"改动没生效"（实测踩过，坑 A47）。正确顺序：`input keyevent KEYCODE_WAKEUP` → `am force-stop` → 删旧诊断文件并**确认删除成功** → `am start`

## 节拍器（2026-09-30 新增，F6 / ADR-0016）

- **页面**：底栏第 2 位「节拍器」（`qml/pages/MetronomePage.qml`），共 5 个导航位（实时 / 节拍器 / 文件 / 音域 / 更多）
- **分层**：拍号与逐拍细分模型、点击音合成、时间轴调度、输出打包都在 `src/core`（零 Qt、可单测）；
  `QAudioSink` 拉模式在 `src/audio/metronome-engine`；界面接线在 `src/app/metronome-controller`（QML 单例 `Metronome`）
- **铁律（改动前先读）**：
  - 点击时刻**只能**由音频线程按采样点摆放，**不要**改成 `QTimer` + `QSoundEffect`（抖动+漂移，ADR-0016）
  - **单次点击的峰值必须归一化**到角色目标值（强 0.95 / 弱 0.80 / 细分 0.45）；多路重叠超 1.0 属正常，由 `packMono` 的总增益+硬夹紧兜住（坑 A48）
  - 合成音的噪声必须是**无状态**哈希（按样点下标），带状态 RNG 会让"不同块长渲染出不同声音"（坑 A49）
  - 验证重叠场景时不要用包络检测去分辨每个点击，改用"按期望 (位置,角色) 独立重建再逐样点比对"（坑 A50）
  - BPM 指"每拍"（分母那个时值）；6/8 的二拍感 = **复合拍号自动按 3 个一组加重音**（6/8 重音在第 1、4 拍），
    若要 DAW 那种"按附点四分计速"的语义，需要另加「节拍基准」选项（当前**不做**，见 ADR-0016）
  - 拍号预置 12 种（含 1/4、5/8、2/2）；点击测速（tap tempo）的纯逻辑在 `src/core/tap-tempo`
- **自检**：`--metrocheck`（数值，86 项）、`--metrolive=3`（声卡）、`--metrowav <wav>`（导出试听）
- **后续完善项（用户已点名，等他发话再做，别自己开工）**：逐拍重音开关、逐拍静音、时值取值扩到 1–6（+配置串版本化兼容）。
  需求与验收清单在 `dev-docs/pitch-detector-APP/features/pitch-detector/spec.md` 的「F6 增补」表；计划行见 `dev.md` 单元 16。
  **明确不做**：DAW 式「节拍基准（BPM basis）」——BPM 一律指分母时值，二拍感靠复合拍号自动重音分组
- **存储访问（2026-09-30 真机 A/B 实测，ADR-0015）**：Android 11（API 30）起 `Android/data` 与 `Android/obb` 对**所有第三方应用**封锁——系统选择器里看不到，`MANAGE_EXTERNAL_STORAGE`（"所有文件访问权限"）也打不开（授权前后都是"可读=否 0 项"，该权限已从清单删除）。要分析的音频必须放在**公共目录**（`Download` / `Music` / `Documents`）；QQ 接收的文件默认落在 `Android/data`，需用户手动移出。诊断入口：Debug 页「存储访问」卡片，或 `adb shell run-as org.pitchdetector.app cat files/storageprobe.txt`

- **Qt Multimedia 已安装**（用 Qt 安装目录下的 `MaintenanceTool.exe`，headless 装入 6.8.3）：
  组件名 `qt.qt6.683.addons.qtmultimedia`，装法：
  `<QtRoot>\MaintenanceTool.exe install qt.qt6.683.addons.qtmultimedia --root <QtRoot> --accept-licenses --accept-obligations --accept-messages --confirm-command`
  - **装完必须重新 qmake 再构建**（`CONFIG` 里的 `qtHaveModule(multimedia)` 在 qmake 阶段求值）
  - **`windeployqt` 不会自动带上 Multimedia**（它是运行期按需加载的插件式后端，不在直接依赖里）：
    部署时必须手工拷贝 `Qt6Multimedia.dll`、`Qt6Network.dll`、`Qt6MultimediaQuick.dll` 与 `plugins\multimedia\*.dll`
    ——`build-and-run.bat` 已包含这一步
- **音频设备因机器而异**（`--devices` 的实测结果不可跨机器照抄）：采集实现会在设备不支持
  44.1 kHz 单声道 Float 时退到设备首选格式并显式提示（坑 A3）

- **改了 `.pro` 或增删源文件后必须重新 qmake**：qmake 用 `-incremental` 不重读 `.pro`；
  只改 `.pro` 内容而不增删文件时，Makefile **不会**自动重生成（实测踩过，表现为"改了没生效"）
- **只改了静态库（`src/core`、`src/io`、`src/audio`）的源码后，`jom` 不会重链 app**：qmake 只把 `LIBS += -lpitch-core`
  写进链接命令行，**不把 .a 当 exe 的依赖**，于是新代码没进二进制、你验证到的是上一版逻辑（实测踩过：自检失败项与改前**一模一样**，差点误判成"改动无效"，坑 A52）。
  正确做法：删 `bin/pitch-detector-APP.exe` 再 `jom`，并**核对 exe 时间戳比 `lib/*.a` 新**
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

## 界面风格（D + E —— 用户 2026-09-30 定稿，ADR-0012）

> **这是硬约定，不是建议。** 任何新界面、新组件、改样式，都必须遵守本节；
> 完整规范与组件配方见 `[仓库] dev-docs/pitch-detector-APP/design/ui-style.md`。

- **风格定义**：
  - **D = 仪表/DAW 深色高对比**：近黑底 + **单一**强调色（青绿）+ 超大读数；弱光可读、一眼看清
  - **E = 借用 IBM Carbon 的规则**：8px 栅格、克制灰阶、状态色有明确语义
  - 参照链接：<https://carbondesignsystem.com/elements/color/overview/> ・ <https://m3.material.io/>（后者只借"底部导航/触控尺寸"这类平台惯例）
- **两套配色**（ADR-0013）：`dark`（默认，D 风格）+ `light`（白底浅色）。同一套令牌名，只换取色；
  切换入口在「更多 → 外观」，选择写 `QSettings`（`ui/themeMode`）并重启保持
  - **颜色属性必须带 `NOTIFY modeChanged`**（不能是 `CONSTANT`，否则切换不会重绘）
  - 规则：深色态配**浅**内容、浅色态配**深**内容；浅色态的强调色/状态色要比深色态**更深**
  - 命令行 `--theme dark|light` 只改内存不落盘，供截图与排查
- **令牌唯一来源**：`[仓库] src/app/theme.h`。颜色、字号、间距、圆角、图标尺寸、动效时长**只能**从这里取
  - **禁止**在 `qml/` 里出现：十六进制色值、`Qt.rgba(`/`Qt.hsla(`、`font.pixelSize: <数字>`、`radius: <数字>`
  - 机械门禁：`powershell -ExecutionPolicy Bypass -File tools/check-theme.ps1`（改界面后必须跑；已做负向验证，坑 A13）
- **图标**：一律来自 `[仓库] resources/icons/`（Lucide 图标集，ISC 许可，清单见 `resources/ATTRIBUTION.md`）
  - **不许用文字符号/emoji 代替图标**；图标**必须**经 `qml/components/Icon.qml` 渲染
    （Lucide 的 SVG 是 `stroke="currentColor"`，Qt 不解析它，直接 `Image` 会渲染成黑色）
  - **按钮**一律用 `qml/components/ActionButton.qml`（primary/secondary 两态），不要各页各写一套
- **布局骨架**：手机单列、底部四入口导航（实时/文件/音域/更多，ADR-0006）+ 手机比例窗口 400×860（ADR-0010）
  - **三段结构（ADR-0014，每个页面都必须）**：标题区 `PageHeader` **固定** / 内容区 **`PageScroller`（可上下滑动）** / 底部导航 `BottomNavBar` **固定**
    - 页面文件里只写内容区：`Item { PageScroller { anchors.fill: parent; Card { … } } }`
    - 内容区**不要用 `Layout.fillHeight`**（滚动容器里高度由内容决定）→ 需要高度就给固定值
    - 滚动条样式/回弹/自动隐藏都在 `PageScroller` 里，页面不要自带 `Flickable`/`ScrollBar`
  - 页面自上而下：操作区 → 状态 → 提示 → **主读数卡** → 曲线卡 → 次要信息卡
  - 卡片统一"面 + 1px 描边 + 内边距"三件套（`Theme.surface` / `Theme.border` / `Theme.spacing`），
    用 1px 描边分层而**不用阴影**（低端机性能 + 深色下阴影不可见）
- **数值来源铁律（与分层铁律同源）**：界面不得自己定阈值——置信度门槛用 `Session.displayMinConfidence`、
  读数保持用 `Session.holdMs`、静音门槛用 `Session.rmsFloor`（都由 C++ 暴露）
- **改风格的流程**：① 先改 `adr/0001-架构与选型.md`（新增/修订 ADR）→ ② 改 `theme.h` 令牌 →
  ③ `check-theme.ps1` 过 → ④ 更新 `design/ui-style.md`。**跳过①直接改样式视为违规**
- **Qt 控件风格钉死 `Basic`**（`main.cpp` 里 `QQuickStyle::setStyle("Basic")`）：避免 Windows/Android 各带一套平台配色

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

- 规格 / 日志 / 验收：`[仓库] dev-docs/pitch-detector-APP/features/pitch-detector/`
- 项目上下文 / 坑 / 决策：`[仓库] dev-docs/pitch-detector-APP/`

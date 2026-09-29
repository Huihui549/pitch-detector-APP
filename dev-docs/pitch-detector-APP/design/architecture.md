# 架构设计（design/architecture.md）

> 状态：**已冻结**（2026-09-28 用户拍板：手机优先 / 单页 + 底部导航 / 调试页隐藏入口 / 实时固定窗 4096；见 ADR-0004、ADR-0006）。
> 本文件写"为什么这么分层、模块间怎么交互"；字段级实现细节写在代码与 `features/pitch-detector/*.md`。

## 一、总览

```
┌──────────────────────────────────────────────────────────┐
│ qml/            纯界面（无算法、无阈值、无换算）            │
│   Main.qml（单页 + 底部导航栏：实时/文件/音域/更多）        │
│   pages/{LivePage,FilePage,RangePage,MorePage,DebugPage}   │
│   components/{NoteDisplay,CentsBar,PitchCurve,LevelMeter,  │
│               BottomNavBar,NoteSegmentTable,StatCard}      │
└───────────────────────┬──────────────────────────────────┘
                        │ 属性 / 信号 / 槽（QML ↔ C++）
┌───────────────────────▼──────────────────────────────────┐
│ src/app/        控制器与 QML 注册                          │
│   PitchSessionController（实时会话、状态机）                │
│   FileAnalysisController（文件分析、进度、结果模型）         │
│   RangeTracker（音域统计）· ReadingSmoother（保持/去抖）     │
│   NoteModel / FrameTableModel（表格数据）                   │
└───────┬───────────────────────────────┬──────────────────┘
        │                               │
┌───────▼─────────────┐       ┌─────────▼──────────────────┐
│ src/io/             │       │ src/audio/                 │
│   WavReader         │       │   IAudioSource（接口）      │
│   CsvExporter       │       │   QtAudioSource（Qt实现）    │
│   AnalysisRunner    │       │   （嵌入式实现预留）         │
└───────┬─────────────┘       └─────────┬──────────────────┘
        │                               │
┌───────▼───────────────────────────────▼──────────────────┐
│ src/core/        纯逻辑，零 Qt 依赖，全部可用标准库单测      │
│   PitchEngine（YIN + 级联窗长 + 两项精修 + 谐波复核）        │
│   OctaveUnifier（八度轨迹校正）                             │
│   NoteConverter（频率 ↔ 音名/音分，十二平均律 SPN）          │
│   LadderFrameProvider（按窗长取帧的抽象）                    │
│   PitchTypes（Frame / Result / Config 纯结构体）             │
└──────────────────────────────────────────────────────────┘
```

## 二、分层职责与依赖规则

| 层 | 允许依赖 | 禁止 |
|---|---|---|
| `src/core/` | C++ 标准库 | Qt、UI、文件系统、网络 |
| `src/audio/` | Qt Multimedia、`src/core`（仅类型） | UI、`src/app` |
| `src/io/` | C++ 标准库、Qt Core（`QFile`/`QString` 视需要）、`src/core` | UI、`src/app` |
| `src/app/` | 以上全部、Qt Core/Gui/Qml | 直接实现算法 |
| `qml/` | `src/app` 注册的类型 | 任何算法、阈值、文件 IO |

**判据（可机械检查）**：`qml/` 目录内不得出现 `27`、`4300`、`0.3`、`4096`、`16384`、`log2`、`440`（除显示文案外）；`src/core/` 内不得出现 `#include <Q`。

## 三、核心模块契约（草案）

### 3.1 `src/core/` — 算法内核

- **输入**：`const float* frame`（或 `std::span<const float>`）+ `sampleRate` + `EngineConfig`（fMin/fMax/thresholds）
- **输出**：`std::optional<PitchResult>`，`PitchResult{ double freq; double confidence; int tau; bool octaveFixed; }`
- **无状态**（便于并发与复用）：
  - `PitchEngine` 不持有跨帧状态；工作缓冲按**每次调用传入**的 `EngineBuffers`（由调用方预分配、复用）避免逐帧分配（R8：热路径禁动态分配）
  - 上游引擎用模块级静态缓冲（`yinD`/`cmndBuf`）便于 JS 写法；C++ 侧改为显式传入，**语义等价但可重入**
- **移植对照点**（每一处行为都必须与上游逐行对齐）：

| 上游函数 | C++ 对应 | 关键不变量 |
|---|---|---|
| `detectPitch` | `PitchEngine::detect` | τ 起点 `sr/fMax/2`；首个低于阈值谷 + 谷底下滑；边界 `tauEst<=tauMin` / `>=tauMax-1` 判无效 |
| `refineTau` | `PitchEngine::refineTau` | ±0.5 样点、1/64 步长、线性插值 |
| `refineFreqByCorrelation` | `PitchEngine::refineFreq` | 仅 ≥500 Hz；±3%；240 步 + 抛物线 |
| `harmonicScore` / `preferFundamental` | `PitchEngine::harmonicScore` / `preferFundamental` | 算术加权 `1/k`；分频下限 40 Hz；改判需 > 1.15 倍 |
| `detectWithLadder` | `PitchEngine::detectWithLadder` | 取第一个 `tauMax < n/2` 的有效结果；RMS 不足即停 |
| `unifyOctaves` | `OctaveUnifier::apply` | 置信度加权中位数；±1200/±2400 ± 120 音分容差；**折回后重算音名/音分**（pitfalls #28） |
| `analyzeBuffer` | `AnalysisRunner::run` | 先扫峰值 RMS → 逐帧 → 八度校正 → 重算派生字段 → 统计 |

### 3.2 `src/audio/` — 采集抽象

```
IAudioSource（纯虚）
  ├─ bool start(const AudioFormat& requested)   // 返回协商后的实际格式
  ├─ void stop()
  ├─ AudioFormat actualFormat() const
  ├─ signal samplesReady(const float* data, int count)   // 或回调接口
  └─ signal errorOccurred(AudioError, QString message)
        ├─ QtAudioSource（QAudioSource 实现，桌面 + Android）
        ├─ EmbeddedAudioSource（预留：ALSA / 厂商 SDK）
        └─ FileAudioSource（测试用：把 WAV 当实时流喂入，验证实时链路无需麦克风）
```

- **`FileAudioSource` 是本设计的关键测试手段**：上游无法自动验证实时链路，本项目可以把已知音频按实时速度喂给实时链路，做到**实时逻辑可自动回归**（对应上游 `verify.md` 里大量"待验"的采集项）

### 3.3 `src/app/` — 控制器

| 类 | 职责 | 关键点 |
|---|---|---|
| `PitchSessionController` | 开/停采集、驱动 YIN、维护滚动窗口、对外发 `readingChanged` | 用环形缓冲持帧；读数保持 700 ms；静音/低置信度发「—」而非残留值 |
| `RangeTracker` | 本次会话最高/最低音与累计有效时长 | 置信度门槛 + 连续 3 帧；单帧误判不入极值 |
| `FileAnalysisController` | 导入音频 → 分析 → 结果模型 + CSV | 分析放工作线程（`QThread`），进度回主线程；避免阻塞界面 |
| `ReadingSmoother` | 去抖与保持 | 参数与上游一致；抖动不得被当作算法误差（pitfalls #14） |
| `DebugInfo` | 暴露 τ 曲线、RMS、实际格式、置信度 | 对应上游 `detectWithCurve`；**曲线数据由 core 提供，界面不重算**（pitfalls #22） |

## 四、跨平台差异处理

| 关注点 | Android（**首版优先**） | Windows 桌面（开发机验证 + 客户端） | 嵌入式（预留） |
|---|---|---|---|
| 采集 API | `QAudioSource` + `RECORD_AUDIO` 运行期权限 | `QAudioSource` | `EmbeddedAudioSource`（ALSA/SDK，未落地） |
| 界面适配 | 竖屏单栏、底部导航、大触控区（**基准形态**） | 同 QML 加宽布局，首版只保证可用 | 帧缓冲/触摸，QML 同源 |
| 文件选择 | 平台文件选择器（需存储权限） | `FileDialog` | 视平台 |
| 构建 | CMake + NDK + Gradle（`androiddeployqt`） | CMake + MinGW/MSVC | 交叉工具链 |

## 五、防"算法分叉"的机械保障（对应上游 #22）

1. 算法只存在于 `src/core/`，其它目录**只调用**
2. `qml/` 的禁用符号检查（见第二节判据）纳入 `verify.md` 自动检查
3. C++ 与 JS 逐帧对拍（ADR-0003）作为回归项，任何算法改动必须两端一致

## 六、已冻结决策（2026-09-28）

| # | 事项 | 决定 |
|---|---|---|
| 1 | 首版平台优先级 | 手机 APP（Android ARM64-v8a）优先；桌面同代码库一并构建 |
| 2 | Android 纳入首版 | 纳入（工具链成本前置） |
| 3 | 实时链路窗长策略 | 固定窗 4096（沿用上游），切换条件见 ADR-0004 |
| 4 | 界面形态 | 单页 + 底部导航栏（实时/文件/音域/更多） |
| 5 | 调试页 | 进首版，隐藏入口（连点标题区 7 次） |

> 唯一剩余待定项：Android `minSdkVersion`/`targetSdkVersion`（见 `context.md` 待定项）。

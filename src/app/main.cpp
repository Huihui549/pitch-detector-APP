// 应用入口
//
// 界面形态（用户 2026-09-28 拍板）：单页 + 底部导航，**按手机版布局**；
// 桌面运行时就开一个手机比例的小窗（9:19.5），这样桌面上看到的效果与手机一致，
// 不需要为"桌面宽屏"另做一套布局（宽屏适配属第二优先，见 ADR-0006）。
//
// 同时提供 `--selftest`：无界面跑一遍文件分析链路并打印结果。
// 理由：无头环境下我无法点击界面，但必须能**自动验证**"WAV → 算法 → 对外数据"这段是否真的通。

#include "file-analysis-controller.h"
#include "pitch-session-controller.h"
#include "storage-access.h"
#include "theme.h"
#include "wav-reader.h"

#include <QApplication>          // 需要它才能用 QFileDialog（QWidget 类必须有 QApplication）
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextStream>
#include <QTimer>

#if PITCH_HAVE_QT_MULTIMEDIA
#include <QAudioDevice>
#include <QAudioFormat>
#include <QMediaDevices>
#endif

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

namespace {

/// 无界面自检：跑文件分析并打印关键结果，退出码 0 = 通过。
int runSelfTest(const QString& wavPath, const QString& expectNote) {
    QTextStream out(stdout);
    QFileInfo info(wavPath);
    if (!info.exists()) {
        out << "[FAIL] 文件不存在：" << wavPath << "\n";
        return 2;
    }

    // 先直接用同一个读取器读一次：把"读文件失败"与"分析无有效音高"分开报，
    // 否则两者都表现为"分析未产生有效音高"，无法定位（曾经因此白查一轮）。
    {
        // 路径统一按 UTF-8 传（Windows 上由 readWavMono 内部转 UTF-16）。
        // 原来的"宽字符 vs 窄字符"两条路对比已取消：窄字符按本地代码页解释那条路已不存在（坑 A29）。
        const pitch::WavData probe = pitch::readWavMono(wavPath.toUtf8().toStdString());
        out << "预检读取：" << (probe.ok ? "成功" : "失败")
            << "（样点 " << static_cast<double>(probe.samples.size()) << "，采样率 " << probe.sampleRate;
        if (!probe.ok) {
            out << "，原因：" << QString::fromStdString(probe.error);
        }
        out << "）\n";
        if (!probe.ok) {
            return 2;
        }
    }

    pitch::FileAnalysisController controller;
    bool done = false;
    bool ok = false;
    QObject::connect(&controller, &pitch::FileAnalysisController::finished,
                     [&done, &ok](bool success) {
                         done = true;
                         ok = success;
                     });

    controller.analyze(wavPath);

    // 等待完成：分析在独立线程跑，这里用事件循环等（与真实使用路径一致）
    QElapsedTimer timer;
    timer.start();
    while (!done && timer.elapsed() < 300000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }

    const QString summary = controller.summary();
    out << "===== 自检：文件分析链路 =====\n";
    out << "文件： " << info.fileName() << "\n";
    out << summary << "\n";

    if (!done) {
        out << "[FAIL] 分析超时（>300 s）\n";
        return 1;
    }
    if (!ok) {
        out << "[FAIL] 分析未产生有效音高\n";
        return 1;
    }

    // 从摘要里取出"众数音名"做断言：这是与上游同口径的判定（音名 + 偏差双条件已在算法内）
    const QString marker = QStringLiteral("众数音名：");
    // indexOf 返回 qsizetype（64 位），显式收窄：此处字符串只有几十字符，越界不可能
    const int pos = static_cast<int>(summary.indexOf(marker));
    if (pos < 0) {
        out << "[FAIL] 摘要缺少众数音名\n";
        return 1;
    }
    const QString rest = summary.mid(pos + static_cast<int>(marker.size()));
    // 用 QStringLiteral 的 \u 转义而不是宽字符字面量：'（' 这类全角字符在 -Wpedantic 下
    // 会报 "character not encodable in a single execution character code unit"
    const int end = static_cast<int>(rest.indexOf(QStringLiteral("\uFF08")));   // 全角左括号
    const QString detected = (end >= 0 ? rest.left(end) : rest).trimmed();

    out << "检出音名：" << detected << "  期望音名：" << expectNote << "\n";
    if (!expectNote.isEmpty() && detected != expectNote) {
        out << "[FAIL] 音名不匹配\n";
        return 1;
    }
    out << "[PASS] 文件分析链路通过\n";
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    // 先扫一遍原始参数判断运行模式，再决定创建哪种 Application。
    // 理由：--selftest 是纯计算自检，**不需要窗口平台**；若仍用 QGuiApplication，
    // 在没有可用显示平台的环境下会在启动阶段就崩（实测退出码 0xC0000409），
    // 而那与代码逻辑无关，会把"无头自检"这件事变得不可用。
    bool wantsSelfTest = false;
    bool wantsQmlCheck = false;
    bool wantsDevices = false;
    bool wantsUiShot = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--selftest" || arg.rfind("--selftest=", 0) == 0) {
            wantsSelfTest = true;
        } else if (arg == "--qmlcheck") {
            wantsQmlCheck = true;
        } else if (arg == "--devices") {
            wantsDevices = true;
        } else if (arg == "--uishot" || arg.rfind("--uishot=", 0) == 0) {
            wantsUiShot = true;
        }
    }
    // 设备枚举需要 Qt Multimedia（属 GUI 侧运行时），故与 qmlcheck 一同走 QGuiApplication
    const bool needGui = !wantsSelfTest || wantsQmlCheck || wantsDevices || wantsUiShot;

    std::unique_ptr<QCoreApplication> appHolder;
    if (needGui) {
        // 用 QApplication 而不是 QGuiApplication：文件选择走 QFileDialog（QWidget 类），
        // 而 QWidget 必须有 QApplication；用 QGuiApplication 时点"选择音频文件"会直接崩溃
        // （实测报 `QWidget: Cannot create a QWidget without QApplication`，见坑 A32）。
        // QApplication 是 QGuiApplication 的子类，QML/Quick 照常工作。
        appHolder = std::make_unique<QApplication>(argc, argv);
        // 控件风格显式钉成 Basic：本项目外观全部由 Theme 令牌 + 自绘组件决定。
        // 跟随平台默认风格会让 Windows / Android 各带一套配色与圆角，与"风格统一"
        // 的硬约定冲突（ADR-0012、AGENTS.md 硬约定）。
        QQuickStyle::setStyle(QStringLiteral("Basic"));
    } else {
        appHolder = std::make_unique<QCoreApplication>(argc, argv);
    }
    QCoreApplication& app = *appHolder;
    app.setApplicationName(QStringLiteral("pitch-detector-APP"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setOrganizationName(QStringLiteral("pitch-detector"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("音高检测工具（手机界面形态的 Qt 实现）"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption selfTestOption(
        QStringLiteral("selftest"),
        QStringLiteral("无界面自检：分析指定 WAV 并打印结果"),
        QStringLiteral("wav"));
    QCommandLineOption expectOption(
        QStringLiteral("expect"),
        QStringLiteral("自检期望音名（如 A4）；不匹配则退出码非 0"),
        QStringLiteral("note"));
    QCommandLineOption qmlCheckOption(
        QStringLiteral("qmlcheck"),
        QStringLiteral("加载 QML 后立即退出：用于在无头环境下验证界面能否被创建（有 QML 错误则退出码非 0）"));
    QCommandLineOption uiShotOption(
        QStringLiteral("uishot"),
        QStringLiteral("把界面渲染成 PNG 后退出：用像素核对配色与图标可见性（参数为输出路径）"),
        QStringLiteral("png"));
    QCommandLineOption themeOption(
        QStringLiteral("theme"),
        QStringLiteral("临时指定主题（dark / light）：只影响本次运行、不写入配置，用于截图与排查"),
        QStringLiteral("mode"));
    QCommandLineOption devicesOption(
        QStringLiteral("devices"),
        QStringLiteral("罗列音频输入设备与首选格式：用于验证 Qt Multimedia 是否在运行期可用"));
    QCommandLineOption micTestOption(
        QStringLiteral("mictest"),
        QStringLiteral("麦克风采集自检：启动采集并打印采集统计与读数。用 --mictest=3 指定秒数（默认 5）"),
        QStringLiteral("seconds"));
    QCommandLineOption loopTestOption(
        QStringLiteral("looptest"),
        QStringLiteral("实时链路自检：注入合成信号（A4=440Hz 等）走与麦克风相同的处理链路，验证实时检测是否可用"));
    parser.addOption(selfTestOption);
    parser.addOption(expectOption);
    parser.addOption(qmlCheckOption);
    parser.addOption(uiShotOption);
    parser.addOption(themeOption);
    parser.addOption(devicesOption);
    parser.addOption(micTestOption);
    parser.addOption(loopTestOption);
    parser.process(app);

    if (parser.isSet(loopTestOption)) {
        // 实时链路自检：把已知频率的合成信号按 512 样点逐批"喂"进控制器，
        // 走与麦克风完全相同的路径。这样"没出数"时能立刻分清是采集问题还是算法问题。
        QTextStream out(stdout);
        auto* session = new pitch::PitchSessionController(&app);

        struct Case {
            double freq;
            const char* note;
        };
        const Case cases[] = {
            {440.00, "A4"}, {82.41, "E2"}, {220.00, "A3"}, {1046.50, "C6"}, {27.50, "A0"},
        };
        constexpr double kSampleRate = 44100.0;
        constexpr int kChunk = 512;
        int passed = 0;
        int total = 0;

        for (const Case& c : cases) {
            session->resetStatistics();
            // 生成 1.2 秒正弦（够攒满最大窗 16384 样点，覆盖低音）
            const int totalSamples = static_cast<int>(kSampleRate * 1.2);
            for (int offset = 0; offset + kChunk <= totalSamples; offset += kChunk) {
                QVector<float> chunk(kChunk);
                for (int i = 0; i < kChunk; ++i) {
                    const double t = static_cast<double>(offset + i) / kSampleRate;
                    chunk[i] = static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979323846 * c.freq * t));
                }
                session->injectSamples(chunk, static_cast<int>(kSampleRate));
            }
            const QString detected = session->noteName();
            const bool ok = detected == QString::fromLatin1(c.note);
            ++total;
            if (ok) {
                ++passed;
            }
            out << (ok ? "[PASS] " : "[FAIL] ") << c.freq << " Hz 期望 " << c.note
                << "，实时链路检出 " << detected << "（" << session->frequency() << " Hz，置信度 "
                << session->confidence() << "，回调 " << session->callbackCount() << "）\n";
        }
        out << "\n实时链路自检：" << passed << " / " << total << " 通过\n";
        return passed == total ? 0 : 1;
    }

    if (parser.isSet(micTestOption)) {
        // 采集自检：不需要人说话也能判断"数据通道是否通"——
        // 有数据则说明采集链路正常（读数是否为对应音高另说）；零数据则说明采集没起来。
        //
        // 秒数从**原始 argv** 里自己解析（`--mictest=3` 或独立开关注入）：
        // Qt 6.8 的 QCommandLineOption 没有 ValueIsOptional，用带值的选项会让
        // `--mictest` 后面跟的下一个开关被当成它的值，启动方式反而不稳。
        double duration = 5.0;
        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            const std::string key = "--mictest=";
            if (a.rfind(key, 0) == 0) {
                duration = std::atof(a.substr(key.size()).c_str());
            }
        }
        if (!(duration > 0.0) || duration > 600.0) {
            duration = 5.0;
        }

        QTextStream out(stdout);
        auto* session = new pitch::PitchSessionController(&app);
        out << "麦克风采集自检：启动 " << duration << " 秒…\n";
        // 手机端既没有终端、也没有 logcat 通道（**实测**：stdout 与 qInfo 都不进 logcat），
        // 故自检把每秒读数**写文件**——debug 包可用
        //   adb shell run-as <包名> cat files/mictest.txt
        // 读取，这是手机上唯一能把"现象"变成"数据"的通道。
        const QString tracePath =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
            QStringLiteral("/mictest.txt");
        QDir().mkpath(QFileInfo(tracePath).absolutePath());
        {
            QFile f(tracePath);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                QTextStream ts(&f);
                ts << "mictest 开始，时长 " << duration << " 秒\n";
            }
        }
        out << "读数字的方式（手机）：adb shell run-as org.pitchdetector.app cat files/mictest.txt\n";
        session->startMicrophone();

        // 每秒一行。这一行同时给出"有没有信号（RMS/回调）"与"识别成什么（音名/频率/置信度）"，
        // 用来区分三种完全不同的故障：采集没通 / 采集通了但识别不出 / 识别成了别的音。
        // 没有它，手机上只能靠肉眼看界面，无法把现象变成可比对的数据。
        auto* tick = new QTimer(&app);
        tick->setInterval(1000);
        QObject::connect(tick, &QTimer::timeout, &app, [session, tracePath]() {
            const QString line =
                QStringLiteral("RMS峰值=%1 回调=%2 音名=%3 频率=%4 置信度=%5")
                    .arg(session->peakRms())
                    .arg(session->callbackCount())
                    .arg(session->noteName())
                    .arg(session->frequency())
                    .arg(session->confidence());
            qInfo().noquote() << QStringLiteral("[mictest] ") + line;
            QFile f(tracePath);
            if (f.open(QIODevice::Append | QIODevice::Text)) {
                QTextStream ts(&f);
                ts << line << "\n";
            }
        });
        tick->start();

        QTimer::singleShot(static_cast<int>(duration * 1000.0), &app, [session, &out, tracePath]() {
            out << "状态：" << session->stateText() << "\n";
            out << "采集实现：" << session->sourceDescription() << "\n";
            out << "采集统计：" << session->captureStats() << "\n";
            out << "原始 RMS 峰值：" << session->peakRms() << "（判断麦克风是否真的收到声音）\n";
            out << "音频回调次数：" << session->callbackCount() << "\n";
            out << "当前读数：音名 " << session->noteName() << "，频率 " << session->frequency()
                << " Hz，置信度 " << session->confidence() << "\n";
            QString verdict;
            if (session->peakRms() <= 0.0) {
                verdict = QStringLiteral("[FAIL] 采集到 0 信号：麦克风通道没通（或被系统静音/禁用）");
            } else if (session->peakRms() < 0.002) {
                verdict = QStringLiteral("[WARN] 采集到极弱信号（%1）：环境安静或麦克风被静音，请对着麦克风出声再测")
                              .arg(session->peakRms());
            } else {
                verdict = QStringLiteral("[PASS] 采集到有效信号，通道正常");
            }
            out << verdict << "\n";
            out.flush();
            qInfo().noquote() << QStringLiteral("[mictest] 结束：%1 | 实现=%2 | 统计=%3 | 音名=%4 频率=%5 置信度=%6")
                                     .arg(verdict)
                                     .arg(session->sourceDescription())
                                     .arg(session->captureStats())
                                     .arg(session->noteName())
                                     .arg(session->frequency())
                                     .arg(session->confidence());
            // 结论也写进同一份文件：手机端只能这样读（见上面的说明）
            QFile f(tracePath);
            if (f.open(QIODevice::Append | QIODevice::Text)) {
                QTextStream ts(&f);
                ts << "结论：" << verdict << "\n"
                   << "采集实现：" << session->sourceDescription() << "\n"
                   << "采集统计：" << session->captureStats() << "\n";
            }
            QCoreApplication::quit();
        });
        return app.exec();
    }

    if (parser.isSet(devicesOption)) {
        QTextStream out(stdout);
        // 同样写一份文件：手机端读不到 stdout（见 --mictest 的说明）
        const QString devTrace = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                                 QStringLiteral("/devices.txt");
        QDir().mkpath(QFileInfo(devTrace).absolutePath());
        QFile devFile(devTrace);
        devFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
        QTextStream devOut(&devFile);
        devOut << "运行平台：" << QSysInfo::productType() << " / " << QSysInfo::prettyProductName() << "\n";
#if PITCH_HAVE_QT_MULTIMEDIA
        devOut << "默认输入设备：" << QMediaDevices::defaultAudioInput().description() << "\n";
        const QList<QAudioDevice> inputs = QMediaDevices::audioInputs();
        out << "===== 音频输入设备（Qt Multimedia）=====\n";
        out << "设备数：" << inputs.size() << "\n";
        devOut << "设备数：" << inputs.size() << "\n";
        for (const QAudioDevice& d : inputs) {
            const QAudioFormat f = d.preferredFormat();
            out << "  · " << d.description() << (d.isDefault() ? "  [默认]" : "") << "\n";
            out << "      首选格式：采样率 " << f.sampleRate() << " Hz，声道 " << f.channelCount()
                << "，样点格式 " << static_cast<int>(f.sampleFormat()) << "\n";
            devOut << "  · " << d.description() << (d.isDefault() ? "  [默认于列表]" : "") << "\n";
            devOut << "      首选格式：采样率 " << f.sampleRate() << " Hz，声道 " << f.channelCount()
                   << "，样点格式 " << static_cast<int>(f.sampleFormat()) << "\n";
            // 用 setter 构造期望格式：QAudioFormat 没有便捷的三参构造（实测花括号初始化会编译失败）
            QAudioFormat wanted;
            wanted.setSampleRate(44100);
            wanted.setChannelCount(1);
            wanted.setSampleFormat(QAudioFormat::Float);
            out << "      支持 44.1 kHz 单声道 Float："
                << (d.isFormatSupported(wanted) ? "是" : "否（代码会自动退到设备首选格式并提示）")
                << "\n";
        }
        out << (inputs.isEmpty() ? "[WARN] 未找到任何输入设备\n" : "[PASS] Qt Multimedia 可用且有输入设备\n");
#else
        out << "[FAIL] 本二进制在构建时未包含 Qt Multimedia（PITCH_HAVE_QT_MULTIMEDIA=0）\n";
        out << "       请确认已安装 Qt Multimedia，并在重新构建前重新运行 qmake。\n";
#endif
        return 0;
    }

    if (parser.isSet(selfTestOption)) {
        return runSelfTest(parser.value(selfTestOption), parser.value(expectOption));
    }

    // 注册控制器为 QML 单例：整个应用只有一份会话状态，界面各页共享
    auto* session = new pitch::PitchSessionController(&app);
    auto* fileAnalysis = new pitch::FileAnalysisController(&app);
    // 存储访问诊断：回答"为什么在手机上选不到/读不到某些目录里的音频"（Android 存储策略 vs 权限）
    auto* storage = new pitch::StorageAccess(&app);
    // 主题也走同一注册路径（qmake 构型下 QML 模块的单例声明不可用，见 theme.h 的说明）
    auto* theme = new pitch::ThemeProvider(&app);
    if (parser.isSet(themeOption)) {
        // 临时指定主题（不落盘）：给截图与排查用；界面里的切换会正常持久化
        theme->applyMode(parser.value(themeOption));
    }

    qmlRegisterSingletonInstance("PitchDetector.App", 1, 0, "Session", session);
    qmlRegisterSingletonInstance("PitchDetector.App", 1, 0, "FileAnalysis", fileAnalysis);
    qmlRegisterSingletonInstance("PitchDetector.App", 1, 0, "Storage", storage);
    qmlRegisterSingletonInstance("PitchDetector.App", 1, 0, "Theme", theme);

    const bool qmlCheck = parser.isSet(qmlCheckOption);
    // 界面截图（可选）：把窗口渲染成 PNG，用于**用像素核对界面**（配色/图标是否可见）
    const bool uiShot = parser.isSet(uiShotOption);
    const QString uiShotPath = parser.value(uiShotOption);
    QQmlApplicationEngine engine;

    // QML 入口加载（qmake 构型）
    //
    // qmake 没有 qt_add_qml_module，QML 不生成插件；本项目改为把 qml/ 整个目录
    // ——含**手写**的 qmldir——通过 qml/qml.qrc 编进可执行文件，资源内布局为：
    //     :/PitchDetector/{qmldir, Main.qml, components/…, pages/…}
    // 于是把资源根加入导入路径后，按模块名加载即可命中；qmldir 里的
    // `prefer :/PitchDetector/` 让模块内各文件的相对导入同样走资源。
    //
    // 这条路的好处：QML 直接进可执行文件，运行期**不依赖任何磁盘目录布局**，
    // 从而避开了前一轮 CMake 方案下 windeployqt 部署目录里"模块找不到"的问题（坑 A17）。
    engine.addImportPath(QStringLiteral(":/"));

    if (qmlCheck) {
        // 诊断：确认 QML 资源与手写 qmldir 真的在资源里。
        // 这几条是"界面加载不起来"时最需要先看的信息——上一轮曾靠它定位到
        // "QMLEngine 找不到模块"其实是资源/目录布局问题（坑 A15/A17）。
        const QStringList toCheck{
            QStringLiteral(":/PitchDetector/qmldir"),
            QStringLiteral(":/PitchDetector/Main.qml"),
            QStringLiteral(":/PitchDetector/pages/LivePage.qml"),
        };
        for (const QString& p : toCheck) {
            QTextStream(stdout) << "  resource " << p << " -> "
                                << (QFile::exists(p) ? "OK" : "MISSING") << "\n";
        }
    }

    // 兜底：若将来改为不把 QML 编入资源，只要 <exe 目录>/qml 存在就仍能加载。
    // 诊断开关：定义 PITCH_NO_DISK_QML_FALLBACK 可禁用该兜底，用于判定
    // "QML 到底是从资源里的模块加载的，还是从磁盘加载的"——两者的类型作用域不同：
    // 磁盘加载的同目录文件是普通组件，不享受 qmldir 里的单例声明（会报 Theme is not defined）。
#ifdef PITCH_NO_DISK_QML_FALLBACK
    if (qmlCheck) {
        QTextStream(stdout) << "  （诊断：已禁用磁盘 QML 兜底）\n";
    }
#else
    {
        const QString appDirQml = QCoreApplication::applicationDirPath() + QStringLiteral("/qml");
        if (QFileInfo(appDirQml).isDir()) {
            engine.addImportPath(appDirQml);
        }
    }
#endif

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.loadFromModule("PitchDetector", "Main");
    if (engine.rootObjects().isEmpty()) {
        QTextStream(stderr) << "[FAIL] QML 根对象创建失败（检查 qml/ 与 QML 模块导入路径）\n";
        return -1;
    }

    if (qmlCheck) {
        // 无头自检：让事件循环跑一小会儿（足以暴露绑定求值期的错误），然后正常退出。
        // 退出码 0 = 界面能被创建；QML 里的错误会以警告形式出现在 stderr，需人工核对。
        QTextStream(stdout) << "[PASS] QML 根对象创建成功：" << engine.rootObjects().size() << " 个\n";
        QTimer::singleShot(1200, &app, &QCoreApplication::quit);
    }

    if (uiShot) {
        // 界面截图：把窗口渲染成 PNG 后退出。用途是**用像素核对配色/图标可见性**——
        // 例如"图标是否被染成令牌色、而不是渲染成黑色"（见坑 A44），无需人眼看图。
        // 等 1.5 s 再抓：QML 首帧布局与 MultiEffect 着色都是异步完成的。
        QTimer::singleShot(1500, &app, [&engine, uiShotPath]() {
            if (engine.rootObjects().isEmpty()) {
                QTextStream(stderr) << "[FAIL] 没有根对象，无法截图\n";
                QCoreApplication::exit(3);
                return;
            }
            auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
            if (window == nullptr) {
                QTextStream(stderr) << "[FAIL] 根对象不是 QQuickWindow，无法截图\n";
                QCoreApplication::exit(4);
                return;
            }
            const QImage shot = window->grabWindow();
            const bool saved = shot.save(uiShotPath);
            QTextStream(stdout) << (saved ? "[PASS] 界面已截图：" : "[FAIL] 截图保存失败：")
                                << uiShotPath << "（" << shot.width() << "x" << shot.height() << "）\n";
            QCoreApplication::exit(saved ? 0 : 5);
        });
    }
    return app.exec();
}

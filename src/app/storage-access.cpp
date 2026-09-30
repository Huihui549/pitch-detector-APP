#include "storage-access.h"

#include "wav-reader.h"      // 探测的最后一步：真的把这个 WAV 读进来（能读通才算"可访问"）

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>
#include <QSysInfo>
#include <QTextStream>

#ifdef Q_OS_ANDROID
// 只为 systemDescription() 用到的 QNativeInterface::QAndroidApplication（声明在 qcoreapplication_platform.h）
#include <QCoreApplication>
#endif

namespace pitch {
namespace {

/// 逐个列出的目录：覆盖"看得见/看不见"的分界线
/// （Android/data、Android/obb 是被系统封锁的一侧；Download 是公共目录一侧的对照）
const QStringList& probePaths() {
    static const QStringList paths{
        QStringLiteral("/sdcard"),
        QStringLiteral("/sdcard/Android"),
        QStringLiteral("/sdcard/Android/data"),
        QStringLiteral("/sdcard/Android/media"),
        QStringLiteral("/sdcard/Android/obb"),
        QStringLiteral("/sdcard/Download"),
    };
    return paths;
}

/// 单目录探测：存在性、目录/可读标志、条目数与头几项。
/// 为什么要连"条目数"一起报：FUSE 拒读时 entryList() 返回空列表而不是报错，
/// 只看"有没有报错"区分不出"空目录"和"被拒绝"（坑 A45）。
QString describeDir(const QString& path) {
    const QFileInfo info(path);
    if (!info.exists()) {
        return QStringLiteral("%1 → 不存在").arg(path);
    }
    const QStringList entries = QDir(path).entryList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System, QDir::Name);

    QString line = QStringLiteral("%1 → 目录=%2 可读=%3 条目=%4")
                       .arg(path)
                       .arg(info.isDir() ? QStringLiteral("是") : QStringLiteral("否"))
                       .arg(info.isReadable() ? QStringLiteral("是") : QStringLiteral("否"))
                       .arg(entries.size());
    if (!entries.isEmpty()) {
        const QStringList head = entries.mid(0, 8);
        line += QStringLiteral("，前 %1 项：%2")
                    .arg(head.size())
                    .arg(head.join(QStringLiteral(", ")));
    }
    return line;
}

/// 在 root 下找第一个 .wav，一并回报扫过多少个候选。
/// 规模说明：Android/data 下通常几百个子目录，一次全扫在百毫秒级，故不做预算/中断（保持简单）。
QString findFirstWav(const QString& root, int* candidates) {
    QDirIterator it(root, QStringList{QStringLiteral("*.wav")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        ++(*candidates);
        if (!path.isEmpty()) {
            return path;
        }
    }
    return {};
}

/// 用**真实的读取器**读一遍：只有它能读通，才说明"这个文件真的可用"，
/// 而不只是"列目录时看见了名字"（两者在 Android 上可能不一致）。
QString readWavProbe(const QString& wavPath) {
    const WavData data = readWavMono(wavPath.toUtf8().toStdString());
    if (!data.ok) {
        return QStringLiteral("读取失败：%1").arg(QString::fromStdString(data.error));
    }
    return QStringLiteral("读取成功：样点 %1，采样率 %2 Hz")
        .arg(static_cast<qlonglong>(data.samples.size()))
        .arg(data.sampleRate);
}

/// 报告抬头用的系统标识。
/// 为什么不用 QOperatingSystemVersion::current().name()：实测在 Android 12 上它只返回 "Android"，
/// 不带版本号，无法据以判断"系统存储策略是哪一代"（Android 10 与 11 的规则并不相同）。
QString systemDescription() {
#ifdef Q_OS_ANDROID
    return QStringLiteral("Android API %1")
        .arg(QNativeInterface::QAndroidApplication::sdkVersion());
#else
    return QSysInfo::productType() + QLatin1Char(' ') + QSysInfo::productVersion();
#endif
}

} // namespace

StorageAccess::StorageAccess(QObject* parent) : QObject(parent) {
    refresh();
}

void StorageAccess::refresh() {
    m_report = buildReport();
    writeReportFile(m_report);
    emit changed();
}

QString StorageAccess::buildReport() const {
    QString text;
    QTextStream out(&text);
    out << "存储访问探测（系统 " << systemDescription() << "）\n";

    out << "\n[目录可见性]\n";
    for (const QString& path : probePaths()) {
        out << "  " << describeDir(path) << "\n";
    }

    // 关键一项：Android/data 里到底有没有音频、能不能读通。
    // 因为"相机/QQ/微信"收进来的音频多数落在 Android/data 里（本机实测就是这样）。
    const QString root = QStringLiteral("/sdcard/Android/data");
    out << "\n[Android/data 内音频可读性]\n";
    int candidates = 0;
    const QString wav = findFirstWav(root, &candidates);
    if (wav.isEmpty()) {
        out << "  扫描候选 " << candidates << " 个 .wav：未找到"
            << "（该目录被 Android 封锁，第三方应用无 root 读不到——音频请放到 Download/Music 等公共目录）"
            << "\n";
    } else {
        out << "  找到：" << wav << "\n";
        out << "  " << readWavProbe(wav) << "\n";
    }
    return text;
}

void StorageAccess::writeReportFile(const QString& text) {
#ifdef Q_OS_ANDROID
    // 手机端唯一能把现象变成数据的通道就是写文件（见 storage-access.h 顶部说明）
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                         QStringLiteral("/storageprobe.txt");
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QTextStream out(&file);
        out << text;
    }
#else
    Q_UNUSED(text)
#endif
}

} // namespace pitch

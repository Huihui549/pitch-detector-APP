// 存储访问诊断（Android 存储可见性）
//
// 为什么要有这个类：手机端"选不到 / 读不到文件"至少有两种互不相同的成因——
//   ① 系统文件选择器（SAF）按 Android 策略隐藏了目录（Android 11 起固定隐藏 Android/data 与 Android/obb）；
//   ② 目录存在且看得见，但内容读不出来。
// 两者在界面上的表现完全一样（"这里没有文件"），只能分别测出来，否则排查只能靠猜。
// 本类只做**测**：列目录（连可读标志与条目数一起报）、并尝试真的打开一个音频；不做界面、不改系统状态。
//
// **故意不申请** MANAGE_EXTERNAL_STORAGE（"所有文件访问权限"）：2026-09-30 真机 A/B 实测证明它
// 打不开 Android/data 与 Android/obb（授权前后都是"可读=否 0 项"，见 ADR-0015、坑 A45），
// 对本程序（只读 WAV）没有收益，且属 Google Play 高风险权限。音频要能被选到，只能放公共目录。
//
// 手机端怎么读结果：报告会写入 files/storageprobe.txt，用
//   adb shell run-as org.pitchdetector.app cat files/storageprobe.txt
// 读取（**实测**：stdout 与 qInfo 都不进 logcat，这是手机上唯一的读取通道）。

#pragma once

#include <QObject>
#include <QString>

namespace pitch {

class StorageAccess : public QObject {
    Q_OBJECT
    /// 探测报告（多行文本）。构造时执行一次，refresh() 重新执行。
    Q_PROPERTY(QString report READ report NOTIFY changed)

public:
    explicit StorageAccess(QObject* parent = nullptr);

    QString report() const { return m_report; }

    /// 重跑探测；报告同时写入 files/storageprobe.txt（仅 Android）
    Q_INVOKABLE void refresh();

signals:
    void changed();

private:
    /// 组装报告正文
    QString buildReport() const;
    static void writeReportFile(const QString& text);

    QString m_report;
};

} // namespace pitch

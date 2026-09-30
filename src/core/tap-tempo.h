// 点击测速（tap tempo）—— **纯逻辑，零 Qt 依赖**
//
// 为什么单独成类：这是"用户跟着感觉点几下，程序把速度算出来"的算法，属于可测的纯数据变换
// （喂时间戳 → 出 BPM）。若写在界面控制器里，就只能靠真手点几下才能验证。
//
// 口径与市面做法一致（Dollar Web 公开的算法思路）：
//   · 取**最近若干次**点击间隔的平均，而不是所有历史（否则改了速度后要等很久才跟得上）
//   · 间隔过大（用户停下来想了想）视为**重新开始**，不算进平均
//   · 样本不足时返回 0：界面据此提示"再多点几下"，而不是拿一次间隔硬报一个速度
//
// 编码要求（坑 A20）：本文件必须存为 UTF-8 **无 BOM**。

#pragma once

#include <cstddef>
#include <deque>

namespace pitch {

class TapTempo {
public:
    /// 参与平均的**间隔**个数上限（= 需要的时间戳个数 - 1）
    static constexpr int kMaxIntervals = 4;
    /// 超过这个间隔（毫秒）视为"重新开始"，清空历史
    static constexpr long long kResetMs = 2000;
    /// 合法间隔范围：对应 BPM 上限/下限（200 ms = 300 BPM，2000 ms = 30 BPM）
    static constexpr long long kMinIntervalMs = 200;
    static constexpr long long kMaxIntervalMs = 2000;

    /// 记录一次点击。
    /// @param nowMs 单调递增的毫秒时间戳（由调用方提供，便于测试注入）
    /// @return 当前估计的 BPM；样本不足（还不到 2 次有效点击）时返回 0
    int tap(long long nowMs);

    /// 清空历史（例如用户停止播放或手动改了速度）
    void reset();

    /// 当前估计的 BPM（0 = 样本不足）
    int bpm() const { return m_bpm; }

    /// 已记录的点击次数（用于界面提示"已点 N 下"）
    int tapCount() const { return static_cast<int>(m_stamps.size()); }

private:
    void recompute();

    std::deque<long long> m_stamps{};
    int m_bpm = 0;
};

} // namespace pitch

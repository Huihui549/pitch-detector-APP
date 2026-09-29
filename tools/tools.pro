# 开发工具（不进入产品产物）
#
# 两个验证程序，都**不属于任何产品分层**：链接 core 与 io，不依赖 Qt。
# 因此这里不写 QT += core。
#
# 1) cross-check ：跨语言一致性对拍（B 组验收的执行体）。
#    用 tools/gen-test-fixtures.mjs 生成的"真值"（由上游 JS 引擎算出）逐帧比对 C++ 移植结果。
# 2) piano-batch ：真实钢琴 88 键素材批量分析（A1/A2 验收的执行体）。
#    上游的 84/88 基线就是用等价工具量出来的；本项目要复现同一口径。

TEMPLATE = subdirs
CONFIG += ordered

SUBDIRS = \
    cross-check.pro \
    piano-batch.pro

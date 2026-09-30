/**
 * 环境解析（本仓库唯一出处，SSOT）—— 供所有 Node 工具脚本引用。
 *
 * ============================ 为什么必须是"推导"而不是"写死" ============================
 *
 * 本项目在两台电脑上开发（公司 / 家里），仓库在磁盘上的位置不同：
 *     [公司]  ???/pitch-detector-app      与上游 pitch-detector-web 互为兄弟目录
 *     [家里]  ???/pitch-detector-app
 * 唯一稳定的事实是：**pitch-detector-app 以下的目录结构在两台机器上完全一致**。
 * 因此本文件里的每一个路径都从"本文件自身位置"出发推导，绝不出现任何机器绝对路径。
 *
 * 一旦在这里写死绝对路径，另一台机器上就会静默取到不存在的目录——而且
 * 报错往往只是"文件不存在"，看不出真正原因。故配套 tools/check-paths.mjs 做静态拦截。
 *
 * ============================ 解析顺序 ============================
 *
 *   上游 Web 项目（算法真值的唯一来源）：
 *     1) 环境变量 PITCH_WEB_ROOT
 *     2) 从本文件所在目录逐级向上，找 <某级>/pitch-detector-web
 *   任一步失败即抛错，不静默降级（见 dev-docs pitfalls A7）。
 *
 *     Qt 开发环境：[Qt 安装根]/Tools/... 与 [Qt 安装根]/6.8.3/mingw_64。
 *     Qt 根本身也无法从仓库推导（它不在仓库内），故按下面顺序找：
 *     1) 环境变量 PITCH_QT_ROOT（最明确）
 *     2) 从 qmake.exe 在 PATH 中的位置反推（Qt Creator 环境天然满足）
 *     3) 与仓库同级的已知常见位置 —— 仅作为便利回退，不作为契约
 *     找不到就抛错，并明确告诉使用者该设哪个变量。
 */

import { existsSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

/** 本文件所在目录（= 仓库的 tools/）。 */
const HERE = dirname(fileURLToPath(import.meta.url));
/** 仓库根（= pitch-detector-app/）。所有相对推导的基准。 */
export const REPO_ROOT = resolve(HERE, "..");

/* ============================ 上游 Web 项目 ============================ */

/** 上游 Web 项目的目录名（与本项目互为兄弟目录）。 */
const WEB_DIR_NAME = "pitch-detector-web";

/**
 * 从 startDir 逐级向上，寻找名为 name 的兄弟目录。
 * 为什么向上找而不是"../name"：仓库可以被放在任意深度的目录里，
 * 写死 ../ 只对"正好同级"成立；向上逐级找才与"仓库以下结构一致"这个前提吻合。
 */
function findAncestorSibling(startDir, name) {
  let dir = resolve(startDir);
  for (;;) {
    const candidate = join(dir, name);
    if (existsSync(candidate)) return candidate;
    const parent = dirname(dir);
    if (parent === dir) return null; // 已到盘根
    dir = parent;
  }
}

function resolveWebRoot() {
  const override = process.env.PITCH_WEB_ROOT;
  if (override) {
    if (existsSync(join(override, "tools", "pitch-engine.js"))) return override;
    throw new Error(
      `[FAIL] PITCH_WEB_ROOT 指向的目录里没有 tools/pitch-engine.js：${override}`,
    );
  }
  const found = findAncestorSibling(REPO_ROOT, WEB_DIR_NAME);
  if (found && existsSync(join(found, "tools", "pitch-engine.js"))) return found;

  throw new Error(
    [
      "[FAIL] 找不到上游 Web 项目（pitch-detector-web）。",
      "       上游是本项目算法真值的唯一来源，缺失时不得静默跳过（pitfalls A7）。",
      `       已从以下位置逐级向上查找「${WEB_DIR_NAME}」：`,
      `         ${REPO_ROOT}`,
      "       处理：把上游仓库放到与本仓库共同的某个父目录下，",
      "             或设置环境变量 PITCH_WEB_ROOT 指向上游根目录。",
    ].join("\n"),
  );
}

/**
 * 上游 Web 项目根目录。
 *
 * **惰性解析**：只在真正调用时才抛错。
 * 若在模块加载期就解析，则上游仓库缺席时**任何** import 本模块的脚本都会立刻崩溃——
 * 包括 check-paths.mjs 这类根本不需要上游的工具。故这里只导出函数，不导出常量值。
 */
export function getUpstreamRoot() {
  return resolveWebRoot();
}
/** 上游算法引擎（UMD，浏览器 + Node 双环境）。 */
export function getUpstreamEngine() {
  return join(resolveWebRoot(), "tools", "pitch-engine.js").replace(/\\/g, "/");
}
/** 上游 WAV 读取实现（与本项目 C++ 侧 wav-reader 同源）。 */
export function getUpstreamWavread() {
  return join(resolveWebRoot(), "tools", "wav-read.mjs").replace(/\\/g, "/");
}

/* ============================ Qt 开发环境 ============================ */

/** Qt 版本目录名（Qt 安装根下的这一级）。 */
const QT_VERSION_DIR = "6.8.3";
/** Qt 套件目录名。 */
const QT_KIT_DIR = "mingw_64";

function findQtRootFromPath() {
  // Qt Creator 启动的终端里，qmake 通常已在 PATH 上：
  //   <QtRoot>/<version>/<kit>/bin/qmake.exe
  // 反过来推 4 级就得到 <QtRoot>。
  const pathEntries = (process.env.PATH ?? "").split(process.platform === "win32" ? ";" : ":");
  for (const entry of pathEntries) {
    if (!entry) continue;
    const norm = entry.replace(/[\\/]+$/, "");
    if (!/mingw_64[\\/]bin$/i.test(norm) && !/mingw_64[\\/]bin[\\/]?$/i.test(norm)) continue;
    const kitDir = dirname(norm);          // .../6.8.3/mingw_64
    const verDir = dirname(kitDir);        // .../6.8.3
    const root = dirname(verDir);          // <QtRoot>
    if (existsSync(join(root, "Tools"))) return root.replace(/\\/g, "/");
  }
  return null;
}

function resolveQtRoot() {
  const override = process.env.PITCH_QT_ROOT;
  if (override) {
    if (existsSync(join(override, QT_VERSION_DIR, QT_KIT_DIR, "bin", "qmake.exe"))) {
      return override.replace(/\\/g, "/");
    }
    throw new Error(
      `[FAIL] PITCH_QT_ROOT 下没有 ${QT_VERSION_DIR}/${QT_KIT_DIR}/bin/qmake.exe：${override}`,
    );
  }
  const fromPath = findQtRootFromPath();
  if (fromPath) return fromPath;

  throw new Error(
    [
      "[FAIL] 找不到 Qt 安装根目录。",
      `       期望结构：<QtRoot>/${QT_VERSION_DIR}/${QT_KIT_DIR}/bin/qmake.exe`,
      "       处理：设置环境变量 PITCH_QT_ROOT 指向 Qt 安装根（例如 <盘符>:/devtools/qt），",
      "             或把 Qt 的 bin 目录加入 PATH 后重试。",
    ].join("\n"),
  );
}

/**
 * Qt 安装根目录。同样**惰性解析**（理由见 getUpstreamRoot）：
 * 不需要 Qt 的脚本（如 check-paths.mjs）不该因为 Qt 探测失败而无法运行。
 */
export function getQtRoot() {
  return resolveQtRoot();
}
/** Qt 6.8.3 MinGW 64-bit 套件根。 */
export function getQtKit() {
  return `${resolveQtRoot()}/${QT_VERSION_DIR}/${QT_KIT_DIR}`;
}
/** Qt 自带 MinGW 工具链根。 */
export function getMingwRoot() {
  return `${resolveQtRoot()}/Tools/mingw1310_64`;
}

/**
 * 供排查用的一行摘要（各字段独立尝试，某一个失败不影响其它字段的显示）。
 */
export function describeEnv() {
  const safe = (fn) => {
    try {
      return fn();
    } catch (e) {
      return `(解析失败：${String(e.message).split("\n")[0]})`;
    }
  };
  return [
    `REPO_ROOT      = ${REPO_ROOT}`,
    `UPSTREAM_ROOT  = ${safe(getUpstreamRoot)}`,
    `QT_ROOT        = ${safe(getQtRoot)}`,
  ].join("\n");
}

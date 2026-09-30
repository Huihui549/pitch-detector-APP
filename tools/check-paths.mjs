/**
 * 路径卫生检查（防回归）—— 阻止"机器相关绝对路径"再次进入仓库。
 *
 * ============================ 为什么需要它 ============================
 *
 * 本项目在两台电脑上开发（公司 / 家里），仓库所在的磁盘与父目录**不同**。
 * 唯一稳定的事实是：pitch-detector-app 以下的目录结构一致。
 * 因此仓库里**不允许出现任何指向仓库之外的绝对路径**——一旦出现，
 * 另一台机器上必然失效，而且症状通常只是"文件找不到"，极难归因；
 * 更糟的是会形成"在这台机器改成 A、到那台机器改回 B"的来回修改死循环。
 *
 * 本脚本把这条规则变成可执行的检查：**发现即失败（退出码 1）**，
 * 因此可以挂进提交前检查或验收流程，而不是靠人记住。
 *
 * 用法（仓库根目录）：
 *   node tools/check-paths.mjs            # 检查，发现违规即退出码 1
 *   node tools/check-paths.mjs --list     # 额外列出扫描了哪些文件
 *
 * 允许的写法（不会报错）：
 *   · 相对路径：../pitch-detector-web、tools/xxx.mjs
 *   · qmake 变量：$$PWD、$$OUT_PWD、$$shadowed(...)
 *   · 批处理变量：%~dp0、%PITCH_QT_ROOT%
 *   · 环境变量覆盖：PITCH_WEB_ROOT、PITCH_QT_ROOT
 *   · 文档里的占位符：<repo>、<QtRoot>、<上游仓库>
 */

import { readFileSync, readdirSync } from "node:fs";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, "..");
const SELF = fileURLToPath(import.meta.url);
const LIST = process.argv.includes("--list");

/* ============================ 违规模式 ============================ */

/**
 * 每一条都对应一类"上过当"的写法。
 * 注意盘符规则的前置断言 (?<![A-Za-z0-9_:/])：少了它会把 URL 里的
 * "https://..." 误报成盘符 "s:/"（实测踩过，会淹没真正的违规）。
 */
const PATTERNS = [
  { name: "Windows 盘符绝对路径", re: /(?<![A-Za-z0-9_:/])[A-Za-z]:[\\/][^\s`"')]*/g },
  { name: "UNC 路径", re: /\\\\[A-Za-z0-9_.-]+\\/g },
  { name: "历史遗留目录名", re: /\bdev_project\b|\bdev_doc\b/g },
];

/**
 * 显式抑制：在行尾或上一行写 `path-check: ignore` 即可放行该行。
 *
 * 为什么需要它：文档里必须能**举例说明被禁止的写法**（否则规则难以理解），
 * 而那些例子本身长得就像违规。与其为了过门禁去篡改文档，不如让作者显式声明
 * "这行是故意举例"——这样例外是**可见的、逐行的、需要理由的**，
 * 而不是靠把整份文件加进白名单（那样会连带放过真正的违规）。
 */
const SUPPRESS = /path-check:\s*ignore/;

/**
 * 允许出现盘符的例外（必须逐条写明理由，且尽量少）：
 *   · 本文件自身：模式定义里必须写出这些形状才能检测它们
 */
function isAllowed(file) {
  const rel = relative(REPO, file).replace(/\\/g, "/");
  return rel === "tools/check-paths.mjs";
}

/* ============================ 扫描范围 ============================ */

/**
 * 不扫描：版本库元数据、构建产物（可重建，且天然含绝对路径——
 * 它们由本机 qmake 生成，本来就不该跨机器搬运）。
 */
const SKIP_DIRS = new Set([
  ".git", "obj", "moc", "rcc", "ui", "bin", "lib",
  "run", "reports", "build", "node_modules", ".qtcreator", ".qtc_clangd",
]);

/** 不扫描的单个生成物文件。 */
const SKIP_FILES = new Set([".qmake.stash"]);

/** 只扫文本类文件。 */
const TEXT_EXT = new Set([
  ".md", ".pro", ".pri", ".txt", ".mjs", ".js", ".json", ".ps1", ".bat",
  ".h", ".hpp", ".cpp", ".qml", ".qrc", ".yml", ".yaml",
]);

/** 这些目录下的内容是"生成的测试数据/报告"，不参与检查。 */
const SKIP_PATH_FRAGMENTS = ["/tests/data", "/tests/data-reports"];

function listFiles(root) {
  const out = [];
  const walk = (dir) => {
    for (const e of readdirSync(dir, { withFileTypes: true })) {
      if (SKIP_DIRS.has(e.name)) continue;
      const full = join(dir, e.name);
      if (e.isDirectory()) walk(full);
      else if (e.isFile()) out.push(full);
    }
  };
  walk(root);
  return out;
}

/* ============================ 执行检查 ============================ */

const files = listFiles(REPO);
const violations = [];
let scanned = 0;

for (const file of files) {
  if (resolve(file) === resolve(SELF) || isAllowed(file)) continue;

  const rel = relative(REPO, file).replace(/\\/g, "/");
  if (SKIP_PATH_FRAGMENTS.some((f) => `/${rel}`.includes(f))) continue;

  const base = file.slice(file.lastIndexOf("\\") + 1);
  const dot = file.lastIndexOf(".");
  const ext = dot >= 0 ? file.slice(dot).toLowerCase() : "";
  if (SKIP_FILES.has(base)) continue;
  if (!TEXT_EXT.has(ext) && !base.startsWith(".")) continue;

  const raw = readFileSync(file);
  if (raw.includes(0)) continue; // 二进制
  const text = raw.toString("utf8");
  if (text.includes("\uFFFD")) continue; // 非干净 UTF-8，跳过（另有工具处理）

  scanned++;
  const lines = text.split(/\r?\n/);
  for (let i = 0; i < lines.length; i++) {
    // 抑制：本行或上一行声明 `path-check: ignore`
    if (SUPPRESS.test(lines[i]) || (i > 0 && SUPPRESS.test(lines[i - 1]))) continue;
    for (const { name, re } of PATTERNS) {
      re.lastIndex = 0;
      const m = re.exec(lines[i]);
      if (!m) continue;
      violations.push({
        file: rel,
        line: i + 1,
        kind: name,
        hit: m[0],
        text: lines[i].trim().slice(0, 100),
      });
    }
  }
}

/* ============================ 报告 ============================ */

console.log(`仓库根：${REPO}`);
console.log(`扫描文件：${scanned} 个`);
if (LIST) {
  for (const f of files) console.log(`  · ${relative(REPO, f).replace(/\\/g, "/")}`);
  console.log("");
}

if (violations.length === 0) {
  console.log("[PASS] 未发现机器相关绝对路径。");
  process.exit(0);
}

console.log(`\n[FAIL] 发现 ${violations.length} 处机器相关路径：\n`);
for (const v of violations) {
  console.log(`  ${v.file}:${v.line}  [${v.kind}]  命中「${v.hit}」`);
  console.log(`      ${v.text}`);
}
console.log(
  [
    "",
    "为什么必须改掉：本项目在两台电脑上开发，仓库位置不同；",
    "写死绝对路径会让另一台机器失效，并造成来回修改的死循环。",
    "",
    "正确写法：",
    "  · 构建脚本：qmake 用 $$PWD / $$OUT_PWD；批处理用 %~dp0",
    "  · Qt 位置：环境变量 PITCH_QT_ROOT，或从 PATH 上的 qmake 反推",
    "  · 上游仓库：环境变量 PITCH_WEB_ROOT，或从自身位置向上找兄弟目录",
    "  · 文档：用 <repo> / <QtRoot> 这类占位符",
    "",
    "统一入口：tools/_path-policy.mjs（Node 侧路径解析 SSOT）",
  ].join("\n"),
);
process.exit(1);

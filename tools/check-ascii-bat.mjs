/**
 * 批处理编码门禁 —— `.bat` / `.cmd` 必须**纯 ASCII**。
 *
 * ============================ 为什么（坑 A12 的 cmd 版本）============================
 *
 * cmd.exe 解析 `.bat` 时是**按字节**解码的，用的是本地代码页（中文 Windows 上是 GBK）。
 * GBK 是双字节编码：一个非 ASCII 字节会"吞掉"紧随其后的那个字节。后果不只是那一个字变乱码，
 * 而是**紧随其后的内容一起被吃掉**——可能把注释残片当命令执行，也可能把换行和下一个命令吞掉，
 * 使后面整段解析错位（实测：`set "PATH=..."` 被撕碎 → 构建静默不开始）。
 *
 * 试过的三种"补救"全部无效（都实测过）：
 *   ① 存成 UTF-8 无 BOM    → 字节被按 GBK 误解码，坏
 *   ② 存成 UTF-8 with BOM  → cmd 在 .bat 上不认 BOM，仍然坏
 *   ③ 首行 `chcp 65001`    → cmd 边读边执行，切代码页发生在后续行被解析之后，仍然坏
 *
 * 故规则是硬性的：**`.bat`/`.cmd` 只写 ASCII**，中文说明放到 `.md` 文档里。
 * 这条规则以前只写在 pitfalls 里靠人记住，现在由本脚本机械拦截。
 *
 * 用法（仓库根目录）：
 *   node tools/check-ascii-bat.mjs
 *
 * 退出码：0 = 全部纯 ASCII；1 = 有违规。
 */

import { readFileSync, readdirSync } from "node:fs";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, "..");

/** 不扫描的目录（构建产物、版本库）。 */
const SKIP_DIRS = new Set([
  ".git", "obj", "moc", "rcc", "ui", "bin", "lib",
  "run", "reports", "build", "node_modules", ".qtcreator", ".qtc_clangd",
]);

function listBatFiles(root) {
  const out = [];
  const walk = (dir) => {
    for (const e of readdirSync(dir, { withFileTypes: true })) {
      if (SKIP_DIRS.has(e.name)) continue;
      const full = join(dir, e.name);
      if (e.isDirectory()) walk(full);
      else if (e.isFile() && /\.(bat|cmd)$/i.test(e.name)) out.push(full);
    }
  };
  walk(root);
  return out;
}

const files = listBatFiles(REPO);
const problems = [];

for (const file of files) {
  const rel = relative(REPO, file).replace(/\\/g, "/");
  const bytes = readFileSync(file);

  // BOM 本身在 .bat 里不被 cmd 认可，也算问题（说明有人按 UTF-8 存过）
  const hasBom = bytes.length >= 3 && bytes[0] === 0xef && bytes[1] === 0xbb && bytes[2] === 0xbf;

  /** 非 ASCII 字节的分布（按行号定位，便于直接修）。 */
  const lines = bytes.toString("latin1").split(/\r?\n/);
  const badLines = [];
  for (let i = 0; i < lines.length; i++) {
    // latin1 解码下，任何 >0x7F 的字节都会映射到 U+0080..U+00FF
    if (/[\u0080-\u00ff]/.test(lines[i])) {
      badLines.push({ line: i + 1, bytes: bytes.length });
    }
  }

  if (hasBom || badLines.length > 0) {
    problems.push({ rel, hasBom, badLines });
  }
}

console.log(`仓库根：${REPO}`);
console.log(`扫描 .bat/.cmd：${files.length} 个\n`);

if (problems.length === 0) {
  console.log("[PASS] 所有批处理文件均为纯 ASCII。");
  process.exit(0);
}

console.log(`[FAIL] ${problems.length} 个批处理文件含非 ASCII 内容：\n`);
for (const p of problems) {
  console.log(`  ${p.rel}`);
  if (p.hasBom) console.log("      · 文件带 UTF-8 BOM（cmd 在 .bat 上不认 BOM）");
  for (const b of p.badLines.slice(0, 8)) console.log(`      · L${b.line} 含非 ASCII 字节`);
  if (p.badLines.length > 8) console.log(`      · …另有 ${p.badLines.length - 8} 行`);
}

console.log(
  [
    "",
    "为什么必须纯 ASCII：cmd.exe 按本地代码页(GBK)逐字节解码 .bat，",
    "一个非 ASCII 字符会吞掉紧随其后的字节，导致**下一行**解析错位，",
    "可能出现 `set \"PATH=...\"` 被撕碎、构建静默不开始这类难查的故障。",
    "UTF-8(BOM/无 BOM) 与 `chcp 65001` 三种补救均实测无效。",
    "",
    "处理：把中文说明移到 .md 文档（AGENTS.md / dev-docs 的 pitfalls A34），",
    "      批处理里只留 ASCII 注释与提示。",
  ].join("\n"),
);
process.exit(1);

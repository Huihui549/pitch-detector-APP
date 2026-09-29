/**
 * 给钢琴 88 键素材补音名后缀（`tone (N).wav` → `tone (N) - <音名>.wav`）
 *
 * 命名依据（上游已用实测反证）：88 键编号连续为半音，编号 N → MIDI 20+N，
 *   1 = A0、26 = A#2、40 = C4、63 = B5、88 = C8。
 * 上游曾用"编号 26–63 逐个频率命中、相邻频率比中位 1.0591 vs 理论 1.0595"证实过该对应关系。
 *
 * 用法（[PC]）：
 *   node tools/rename-piano.mjs --dir "D:\钢琴88键独立音频文件"            # 预演（默认，不改任何文件）
 *   node tools/rename-piano.mjs --dir "D:\钢琴88键独立音频文件" --apply    # 真正改名
 *
 * 安全约束（用户素材，按 R9 精神处理）：
 *   1. **默认预演**，必须显式 --apply 才动文件
 *   2. 目标名已存在时**拒绝执行**（不覆盖、不跳过）
 *   3. 命名与目录无关，避免自我递归/误伤（坑 A19）
 *   4. 只处理 `tone (N).wav` 形式的文件；已带后缀的跳过并报告
 */

import { readdirSync, renameSync, existsSync } from "node:fs";
import { join, dirname, basename } from "node:path";

/* ============================ 参数 ============================ */

const argv = process.argv.slice(2);
let dir = null;
let apply = false;
for (let i = 0; i < argv.length; ++i) {
  if (argv[i] === "--dir" && i + 1 < argv.length) {
    dir = argv[++i];
  } else if (argv[i].startsWith("--dir=")) {
    dir = argv[i].slice(6);
  } else if (argv[i] === "--apply") {
    apply = true;
  } else if (argv[i] === "--help" || argv[i] === "-h") {
    console.log('用法: node tools/rename-piano.mjs --dir "<素材目录>" [--apply]');
    process.exit(0);
  }
}

if (!dir) {
  console.error('用法: node tools/rename-piano.mjs --dir "<素材目录>" [--apply]');
  console.error("（不加 --apply 时只预演，不改任何文件）");
  process.exit(2);
}
if (!existsSync(dir)) {
  console.error(`[FAIL] 目录不存在：${dir}`);
  process.exit(1);
}

/* ============================ 命名规则 ============================ */

const NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

/** 编号（1..88）→ 音名（SPN，中央 C = C4）。编号 N ↔ MIDI 20+N（1 = A0，88 = C8）。 */
function noteNameOfIndex(n) {
  const midi = 20 + n;
  const name = NOTE_NAMES[((midi % 12) + 12) % 12];
  const octave = Math.floor(midi / 12) - 1;
  return `${name}${octave}`;
}

/** 从 `tone (N).wav` 提取编号；不匹配则返回 null。 */
function indexOfFile(name) {
  const m = /^tone \((\d+)\)\.wav$/i.exec(name);
  if (!m) {
    return null;
  }
  const n = Number(m[1]);
  return Number.isInteger(n) && n >= 1 && n <= 88 ? n : null;
}

/* ============================ 主流程 ============================ */

const entries = readdirSync(dir).filter((f) => f.toLowerCase().endsWith(".wav")).sort();
const plan = [];
const skipped = [];

for (const name of entries) {
  const n = indexOfFile(name);
  if (n === null) {
    // 已带后缀或命名不符：不动，只在报告里列出
    skipped.push(name);
    continue;
  }
  const target = `tone (${n}) - ${noteNameOfIndex(n)}.wav`;
  plan.push({ from: name, to: target, index: n, note: noteNameOfIndex(n) });
}

console.log(`目录：${dir}`);
console.log(`WAV 文件：${entries.length} 个`);
console.log(`待改名：${plan.length} 个；跳过（命名不符或已改名）：${skipped.length} 个`);
console.log("");

if (plan.length === 0) {
  console.log("没有需要改名的文件。");
  if (skipped.length > 0) {
    console.log("跳过的文件：");
    for (const s of skipped) {
      console.log(`  · ${s}`);
    }
  }
  process.exit(0);
}

// 冲突检查：目标名已存在，或计划内目标名重复 → 拒绝执行（不覆盖、不跳过）
const existing = new Set(entries);
const targets = new Set();
const conflicts = [];
for (const p of plan) {
  if (existing.has(p.to) && !existing.has(p.from)) {
    conflicts.push(`目标已存在：${p.to}`);
  }
  if (targets.has(p.to)) {
    conflicts.push(`计划内目标重复：${p.to}`);
  }
  targets.add(p.to);
}
if (conflicts.length > 0) {
  console.error("[FAIL] 存在命名冲突，已中止（不覆盖任何文件）：");
  for (const c of conflicts) {
    console.error(`  · ${c}`);
  }
  process.exit(1);
}

// 抽样展示前 8 条与后 3 条，便于人工核对对应关系
console.log("前 8 条：");
for (const p of plan.slice(0, 8)) {
  console.log(`  编号 ${String(p.index).padStart(2)}  ${p.from.padEnd(20)} → ${p.to}`);
}
console.log("后 3 条：");
for (const p of plan.slice(-3)) {
  console.log(`  编号 ${String(p.index).padStart(2)}  ${p.from.padEnd(20)} → ${p.to}`);
}
console.log("");
console.log("关键校准点（照理论值核对，便于发现编号错位）：");
for (const idx of [1, 40, 49, 88]) {
  const hit = plan.find((p) => p.index === idx);
  if (hit) {
    console.log(`  编号 ${String(idx).padStart(2)} → ${hit.note}`);
  }
}
console.log("");

if (!apply) {
  console.log("【预演模式】未改动任何文件。确认无误后加 --apply 执行。");
  process.exit(0);
}

let done = 0;
for (const p of plan) {
  renameSync(join(dir, p.from), join(dir, p.to));
  ++done;
}
console.log(`[完成] 已重命名 ${done} 个文件。`);
if (skipped.length > 0) {
  console.log(`跳过的 ${skipped.length} 个文件未改动：`);
  for (const s of skipped) {
    console.log(`  · ${s}`);
  }
}

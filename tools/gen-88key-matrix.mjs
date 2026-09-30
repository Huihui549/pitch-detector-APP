/**
 * 88 键合成音阶矩阵生成器（替代口径 A1'/A2'）
 *
 * 为什么需要它：
 *   上游 84/88 的基线素材（88 个钢琴 WAV）已从磁盘消失（坑 A9），A1/A2 暂时无法执行。
 *   本脚本用**等程律合成的 88 键**（A0=27.50 Hz … C8=4186.01 Hz）建立替代基准：
 *   ① 它能验证"全键盘覆盖、级联窗长、τ 区间收口"这些结构性能力
 *   ② 它能给出 C++ 与 JS 的逐帧一致性基准（与真实素材无关，纯算法等价性）
 *
 * 与真实素材的差距（必须说清，不得当成等价替代）：
 *   合成信号没有起音瞬态、衰减、拍频与真实调律偏差——上游 pitfalls #21/#25 那类
 *   "素材本身导致偏差"的问题，本矩阵**测不出来**。故 A1/A2（真实素材）仍需素材回归。
 *
 * 为什么落盘的是"真值"而不是 176 个 WAV：
 *   每个 WAV 9 s @44.1 kHz 约 794 KB，88 键 × 2 类 ≈ 140 MB。而本矩阵的用途是**跨语言对拍**，
 *   对拍只需要"输入信号的确定性描述 + 上游引擎给出的逐帧真值"。
 *   信号由 C++ 侧用同一公式重算（等程律频率是精确值），因此无需传输样点。
 *   注意：C++ 侧的合成公式必须与本文件严格一致（tests/core/core-tests.cpp 的 synthTone）。
 *
 * 用法（[PC] 仓库根目录）：
 *   node tools/gen-88key-matrix.mjs
 *
 * 产物：
 *   tests/data/matrix88/index.txt        清单（每键一行：midi 音名 频率 真值文件 期望结果）
 *   tests/data/matrix88/<音名>.f64       逐帧真值（8 字段 × 8 字节）
 *   tests/data/matrix88/<音名>.note      逐帧音名
 *   tests/data/matrix88/report.md        JS 引擎在本矩阵上的自检报告
 */

import { createRequire } from "node:module";
import { mkdirSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

import { getUpstreamEngine } from "./_path-policy.mjs";

const require = createRequire(import.meta.url);
const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, "..");
const OUT_DIR = join(REPO, "tests", "data", "matrix88");

// 上游引擎路径由 tools/_path-policy.mjs 解析（PITCH_WEB_ROOT 优先，其次向上找兄弟目录），
// 缺失时它已直接抛错，故此处不再重复判空。
const Engine = require(getUpstreamEngine());

/* ============================ 信号与真值 ============================ */

const SAMPLE_RATE = 44100;
const DURATION_SEC = 3.0;
const AMPLITUDE = 0.5;
const FADE_SEC = 0.02;
const HOP = 441;

/// 分析用窗长阶梯：与上游 engine 的 FRAME_LADDER 一致
const LADDER = Engine.FRAME_LADDER;

/** MIDI 音号 → 频率（等程律，A4 = 440 Hz）。 */
function midiToFreq(midi, refA4 = 440) {
  return refA4 * Math.pow(2, (midi - 69) / 12);
}

/** MIDI 音号 → 音名（SPN，中央 C = C4）。 */
function midiToName(midi) {
  const names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
  return names[((midi % 12) + 12) % 12] + (Math.floor(midi / 12) - 1);
}

/**
 * 合成信号：与 C++ 侧测试使用同一公式。
 * 必须与 tests/core/core-tests.cpp 的 synthTone 逐行等价，否则对拍结论无效。
 */
function synthesize(freq, partials, sampleRate, durationSec) {
  const n = Math.round(durationSec * sampleRate);
  const out = new Float32Array(n);
  if (freq === null || partials.length === 0) return out;
  for (let i = 0; i < n; i++) {
    const t = i / sampleRate;
    let v = 0;
    for (let k = 0; k < partials.length; k++) {
      v += partials[k] * Math.sin(2 * Math.PI * freq * (k + 1) * t);
    }
    out[i] = v;
  }
  let peak = 0;
  for (let i = 0; i < n; i++) peak = Math.max(peak, Math.abs(out[i]));
  if (peak > 0) {
    const gain = AMPLITUDE / peak;
    for (let i = 0; i < n; i++) out[i] *= gain;
  }
  const fade = Math.round(FADE_SEC * sampleRate);
  for (let i = 0; i < fade && i < n; i++) {
    const w = i / fade;
    out[i] *= w;
    out[n - 1 - i] *= w;
  }
  return out;
}

const FRAME_FIELDS = [
  { key: "timeSec", kind: "f64" },
  { key: "freqRaw", kind: "f64" },
  { key: "freq", kind: "f64" },
  { key: "cents", kind: "f64" },
  { key: "confidence", kind: "f64" },
  { key: "rms", kind: "f64" },
  { key: "frameSize", kind: "u64" },
  { key: "octaveFixed", kind: "u64" },
];

function writeFloat64Records(path, records) {
  const buf = Buffer.alloc(records.length * FRAME_FIELDS.length * 8);
  let off = 0;
  for (const rec of records) {
    for (const f of FRAME_FIELDS) {
      const v = f.kind === "u64" ? BigInt(Math.round(rec[f.key] ?? 0)) : Number(rec[f.key] ?? 0);
      if (f.kind === "u64") buf.writeBigUInt64LE(v, off);
      else buf.writeDoubleLE(v, off);
      off += 8;
    }
  }
  writeFileSync(path, buf);
  return buf.length;
}

/* ============================ 主流程 ============================ */

mkdirSync(OUT_DIR, { recursive: true });

/// 钢琴 88 键：MIDI 21（A0）… 108（C8）
const MIDI_LOW = 21;
const MIDI_HIGH = 108;

/// 与 gen-test-fixtures 一致的两种信号
const SIGNALS = [
  { tag: "pure", partials: [1.0] },
  { tag: "harm", partials: [1.0, 0.5, 0.33, 0.25] },
];

const indexLines = [
  `sampleRate=${SAMPLE_RATE}`,
  `hop=${HOP}`,
  `durationSec=${DURATION_SEC}`,
  `amplitude=${AMPLITUDE}`,
  `frameLadder=${LADDER.join(",")}`,
  `midiLow=${MIDI_LOW}`,
  `midiHigh=${MIDI_HIGH}`,
  `keyCount=${MIDI_HIGH - MIDI_LOW + 1}`,
  `signalCount=${SIGNALS.length}`,
];

const reportRows = [];
let totalKeys = 0;
let hitKeys = 0;

const t0 = Date.now();

for (let midi = MIDI_LOW; midi <= MIDI_HIGH; midi++) {
  const note = midiToName(midi);
  const freq = midiToFreq(midi);

  for (const sig of SIGNALS) {
    const stem = `${note}-${sig.tag}`;
    const samples = synthesize(freq, sig.partials, SAMPLE_RATE, DURATION_SEC);
    const r = Engine.analyzeBuffer(samples, SAMPLE_RATE, HOP);

    writeFloat64Records(join(OUT_DIR, `${stem}.f64`), r.frames);
    writeFileSync(join(OUT_DIR, `${stem}.note`),
      r.frames.map((f) => f.note).join("\n") + (r.frames.length ? "\n" : ""), "ascii");

    // 判定口径与上游一致：众数音名正确 **且** 偏差 < 50 音分（pitfalls #26）
    const counts = new Map();
    for (const f of r.frames) counts.set(f.note, (counts.get(f.note) || 0) + 1);
    let modeNote = "—";
    let modeCount = 0;
    for (const [k, v] of counts) if (v > modeCount) { modeCount = v; modeNote = k; }

    const cents = r.frames.length ? r.medianCents : NaN;
    const hit = r.frames.length > 0 && modeNote === note && Math.abs(cents) < 50;

    totalKeys++;
    if (hit) hitKeys++;

    indexLines.push(
      `key.${stem}.midi=${midi}`,
      `key.${stem}.note=${note}`,
      `key.${stem}.freq=${freq.toFixed(6)}`,
      `key.${stem}.signal=${sig.tag}`,
      `key.${stem}.partials=${sig.partials.join(",")}`,
      `key.${stem}.bin=${stem}.f64`,
      `key.${stem}.noteFile=${stem}.note`,
      `key.${stem}.frames=${r.frames.length}`,
      `key.${stem}.modeNote=${modeNote}`,
      `key.${stem}.medianFreq=${r.medianFreq.toFixed(6)}`,
      `key.${stem}.medianCents=${Number.isFinite(cents) ? cents.toFixed(3) : "nan"}`,
      `key.${stem}.hit=${hit ? 1 : 0}`,
    );

    reportRows.push({
      midi, note, tag: sig.tag, freq, frames: r.frames.length, modeNote,
      medianFreq: r.medianFreq, medianCents: cents, hit,
      octaveFixed: r.octaveFixed,
    });
  }

  if ((midi - MIDI_LOW + 1) % 11 === 0) {
    console.log(`  已处理 ${midi - MIDI_LOW + 1} / ${MIDI_HIGH - MIDI_LOW + 1} 键`);
  }
}

const elapsed = ((Date.now() - t0) / 1000).toFixed(1);

writeFileSync(join(OUT_DIR, "index.txt"), indexLines.join("\n") + "\n", "utf8");

/* ============================ 自检报告 ============================ */

const missRows = reportRows.filter((r) => !r.hit);
const bySignal = SIGNALS.map((s) => {
  const rows = reportRows.filter((r) => r.tag === s.tag);
  const hits = rows.filter((r) => r.hit).length;
  return { tag: s.tag, hits, total: rows.length };
});

const reportLines = [
  "# 88 键合成矩阵 —— JS 上游引擎自检报告",
  "",
  `> 由 \`node tools/gen-88key-matrix.mjs\` 生成（${new Date().toISOString()}）。`,
  "> 用途：A1'/A2' 替代口径的基准真值 + 跨语言对拍输入。",
  "> **不是**真实素材的等价替代：合成信号没有起音瞬态、衰减与真实调律偏差，",
  "> 故上游 pitfalls #21/#25 那类问题在本矩阵测不出来（见坑 A9）。",
  "",
  "## 总览",
  "",
  "| 指标 | 值 |",
  "|---|---|",
  `| 音键数 | ${MIDI_HIGH - MIDI_LOW + 1}（MIDI ${MIDI_LOW}–${MIDI_HIGH}，A0…C8） |`,
  `| 信号类型 | ${SIGNALS.length}（pure 纯音 / harm 含 2·3·4 次泛音） |`,
  `| 用例总数 | ${totalKeys} |`,
  `| 命中（众数音名正确 且 偏差 < 50 音分） | **${hitKeys} / ${totalKeys}（${((hitKeys / totalKeys) * 100).toFixed(1)}%）** |`,
  `| 耗时 | ${elapsed} s |`,
  "",
  "## 分类命中",
  "",
  "| 信号 | 命中 | 占比 |",
  "|---|---|---|",
  ...bySignal.map((s) => `| ${s.tag} | ${s.hits} / ${s.total} | ${((s.hits / s.total) * 100).toFixed(1)}% |`),
  "",
  "## 未命中明细",
  "",
  missRows.length
    ? "| MIDI | 音名 | 信号 | 期望频率 | 众数音名 | 中位频率 | 中位偏差(音分) | 帧数 |"
    : "（无未命中项）",
  ...(missRows.length ? ["|---|---|---|---|---|---|---|---|"] : []),
  ...missRows.map((r) =>
    `| ${r.midi} | ${r.note} | ${r.tag} | ${r.freq.toFixed(2)} | ${r.modeNote} | ${r.medianFreq.toFixed(2)} | ${r.medianCents.toFixed(1)} | ${r.frames} |`),
  "",
  "## 说明",
  "",
  "- 判定为**双条件**（音名正确 且 偏差 < 50 音分）：只查音名会被\"标签碰巧对、频率差一个八度\"蒙混（上游 pitfalls #26）。",
  "- 本矩阵的命中率**不是**上游的 84/88 基线，两者不可直接比较：84/88 是真实钢琴素材的成绩。",
  "- `medianCents` 为整段中位频率相对等程律的偏差；合成信号理论上应接近 0。",
  "",
].join("\n");

writeFileSync(join(OUT_DIR, "report.md"), reportLines, "utf8");

console.log("");
console.log(`矩阵产物: ${OUT_DIR}`);
console.log(`用例总数: ${totalKeys}，上游引擎命中: ${hitKeys}（${((hitKeys / totalKeys) * 100).toFixed(1)}%）`);
console.log(`耗时    : ${elapsed} s`);
console.log("");
for (const s of bySignal) {
  console.log(`  ${s.tag}: ${s.hits} / ${s.total}`);
}
if (missRows.length) {
  console.log("");
  console.log("未命中明细（上游引擎在这些用例上也没判对，故不能当作 C++ 的失败）：");
  for (const r of missRows) {
    console.log(`  MIDI ${r.midi} ${r.note} ${r.tag}: 众数 ${r.modeNote}，中位 ${r.medianFreq.toFixed(2)} Hz，偏差 ${r.medianCents.toFixed(1)} 音分，帧数 ${r.frames}`);
  }
}

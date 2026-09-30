/**
 * 测试素材生成器 + 跨语言对拍的"真值"导出（Node 侧，不依赖 C++ 编译器）
 *
 * 为什么要它：
 *   本项目要求 C++ 移植与上游 JS 引擎**逐帧一致**（ADR-0003 / verify.md B 组）。
 *   对拍需要两样东西：① 一段确定的音频 ② 上游引擎在**同一段音频**上的逐帧结果。
 *   生成一边、算另一边，就得到一份可提交进仓库的"真值"（tests/data/reference.json）。
 *
 * 为什么真值必须"读回自己写的 WAV"再算：
 *   若用内存里的浮点数组当输入，而 C++ 侧读的是 WAV 里的 16 bit 量化结果，两边的输入就不同，
 *   对拍会在起音/低电平处出现无法归因的差异。故本脚本先写 WAV，再**从文件读回**算真值。
 *
 * 用法（[PC]，本仓库根目录）：
 *   node tools/gen-test-fixtures.mjs
 *
 * 上游依赖（只读）：
 *   [上游] ../pitch-detector-web/tools/pitch-engine.js   —— 算法真值
 *   [上游] ../pitch-detector-web/tools/wav-read.mjs       —— WAV 读取（与上游分析器同源）
 *   路径不在此内联：由 tools/_path-policy.mjs 从本仓库位置向上查找兄弟目录
 *   （可用环境变量 PITCH_WEB_ROOT 覆盖），缺失时直接抛错，不静默跳过（pitfalls A7）。
 */

import { createRequire } from "node:module";
import { mkdirSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

import { getUpstreamEngine, getUpstreamWavread } from "./_path-policy.mjs";

const require = createRequire(import.meta.url);
const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, "..");
const OUT_DIR = join(REPO, "tests", "data");

/* ============================ 上游依赖 ============================ */

const UPSTREAM_ENGINE = getUpstreamEngine();
const UPSTREAM_WAVREAD = getUpstreamWavread();

const Engine = require(UPSTREAM_ENGINE);
const { readWavMono } = await import(`file:///${UPSTREAM_WAVREAD}`);

/* ============================ 素材参数 ============================ */

const SAMPLE_RATE = 44100;
const DURATION_SEC = 3.0;
/// 签名振幅：留出余量避免削顶（削顶会产生虚假泛音，把对拍结论带偏）
const AMPLITUDE = 0.5;
/// 淡入淡出：避免起止不连续产生宽带瞬态（上游也踩过交界瞬态的坑）
const FADE_SEC = 0.02;

/**
 * 素材清单。
 * 只挑上游引擎实测能判对的音，避免把"上游也判错"的音写进对拍基准。
 * 440 Hz 是 A4 标准音（上游实测 +0.1 音分）；220 Hz 是 A3；含泛音版用于验证谐波复核路径。
 */
const FIXTURES = [
  {
    file: "expect-A4-440Hz-pure.wav",
    note: "A4",
    freq: 440.0,
    partials: [1.0],
    desc: "A4 标准音，纯正弦（对拍主用例）",
  },
  {
    file: "expect-A3-220Hz-pure.wav",
    note: "A3",
    freq: 220.0,
    partials: [1.0],
    desc: "A3，纯正弦（覆盖中低音区）",
  },
  {
    file: "expect-A3-220Hz-harm.wav",
    note: "A3",
    freq: 220.0,
    partials: [1.0, 0.5, 0.33, 0.25],
    desc: "A3，含 2/3/4 次泛音（覆盖频域精修与谐波一致性复核）",
  },
  {
    file: "expect-silence.wav",
    note: "—",
    freq: null,
    partials: [],
    desc: "纯静音：必须返回无有效音高（回归项 E3 的一半）",
  },
];

/* ============================ WAV 写出 ============================ */

/**
 * 生成单声道 16 bit PCM WAV。
 * 不引入任何库：44 字节固定头 + 交织为 1 声道的样点。
 */
function encodeWavMono16(samples, sampleRate) {
  const dataBytes = samples.length * 2;
  const buf = Buffer.alloc(44 + dataBytes);

  buf.write("RIFF", 0, "ascii");
  buf.writeUInt32LE(36 + dataBytes, 4);
  buf.write("WAVE", 8, "ascii");
  buf.write("fmt ", 12, "ascii");
  buf.writeUInt32LE(16, 16);        // fmt 块长度
  buf.writeUInt16LE(1, 20);         // PCM
  buf.writeUInt16LE(1, 22);         // 单声道
  buf.writeUInt32LE(sampleRate, 24);
  buf.writeUInt32LE(sampleRate * 2, 28); // 字节率 = 采样率 × 声道 × 位深/8
  buf.writeUInt16LE(2, 32);         // 块对齐
  buf.writeUInt16LE(16, 34);        // 位深
  buf.write("data", 36, "ascii");
  buf.writeUInt32LE(dataBytes, 40);

  for (let i = 0; i < samples.length; i++) {
    // 夹住范围再量化：越界会回绕成反相信号，是最隐蔽的素材缺陷
    const v = Math.max(-1, Math.min(1, samples[i]));
    buf.writeInt16LE(Math.round(v * 32767), 44 + i * 2);
  }
  return buf;
}

/** 合成一段信号：基频 + 指定泛音（振幅按 partials 给定），两端加淡入淡出。 */
function synthesize({ freq, partials, durationSec, sampleRate }) {
  const n = Math.round(durationSec * sampleRate);
  const out = new Float32Array(n);
  if (freq === null || partials.length === 0) {
    return out; // 静音
  }
  for (let i = 0; i < n; i++) {
    const t = i / sampleRate;
    let v = 0;
    for (let k = 0; k < partials.length; k++) {
      v += partials[k] * Math.sin(2 * Math.PI * freq * (k + 1) * t);
    }
    out[i] = v;
  }
  // 归一化到目标振幅，避免泛音叠加后削顶
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

/* ============================ 真值计算 ============================ */

/** 频率 → 音名（科学音高记号，中央 C = C4）。与引擎的 freqToNote 同源。 */
function noteNameOf(freq, refA4 = Engine.A4) {
  if (!(freq > 0)) return "—";
  const info = Engine.freqToNote(freq, refA4);
  return info.name + info.octave;
}

/**
 * 在整段数据上逐帧跑引擎，返回与 C++ 侧 AnalysisRunner 对应的逐帧结果。
 *
 * 帧进固定为 441 样点（10 ms）：与上游文件分析口径一致，也是 C++ 侧文件分析的默认值。
 */
function referenceFrames(samples, sampleRate, hop) {
  const cfg = {};
  const r = Engine.analyzeBuffer(samples, sampleRate, hop);
  return {
    hop,
    octaveFixed: r.octaveFixed,
    medianFreq: r.medianFreq,
    medianCents: r.medianCents,
    peakRms: r.peakRms,
    rmsFloor: r.rmsFloor,
    frames: r.frames.map((f) => ({
      timeSec: f.t,
      freqRaw: f.freqRaw === undefined ? f.freq : f.freqRaw,
      freq: f.freq,
      note: f.note,
      cents: f.cents,
      confidence: f.conf,
      rms: f.rms,
      frameSize: f.frame,
      octaveFixed: f.octaveFixed === true,
    })),
  };
}

/**
 * 实时链路路径的真值：固定窗 4096、帧进 512。
 * 与文件分析的差异是有意的（上游：实时要低延迟，故不做级联窗长；ADR-0004）。
 * C++ 侧用 detectPitch 单帧检测复现这条路径。
 */
function referenceRealtimeFrames(samples, sampleRate, frame, hop) {
  const out = [];
  for (let pos = 0; pos + frame <= samples.length; pos += hop) {
    const r = Engine.detectPitch(samples.subarray(pos, pos + frame), sampleRate);
    if (!r) continue;
    const info = Engine.freqToNote(r.freq, Engine.A4);
    out.push({
      offsetSamples: pos,
      timeSec: pos / sampleRate,
      freq: r.freq,
      note: info.name + info.octave,
      cents: info.cents,
      confidence: r.confidence,
      tau: r.tau,
    });
  }
  return { frame, hop, frames: out };
}

/* ============================ 二进制真值写出 ============================ */

/**
 * 为什么真值要用二进制而不是 JSON：
 *   JSON.stringify 的双精度序列化会丢末位（17 位有效数字不一定能往返），而 verify.md B1 要求
 *   C++ 与 JS 逐帧频率相对差 ≤ 1e-6——用 JSON 当基准会出现"永远对不齐"的假失败。
 *   故逐帧数值一律以 float64 原始字节落盘；元数据用纯文本，便于 C++ 侧不引 JSON 库即可读取。
 *
 * 记录布局（小端，与 x86/ARM64 一致；C++ 侧读取时按字段逐个读，不做结构体 memcpy，
 * 避免依赖编译器的结构体对齐与填充规则）：
 *   FrameRecord  : 8 字段 × 8 字节 = 64 字节
 *     timeSec(f64) freqRaw(f64) freq(f64) cents(f64) confidence(f64) rms(f64)
 *     frameSize(u64) octaveFixed(u64)
 *   RealtimeRecord: 6 字段 × 8 字节 = 48 字节
 *     offsetSamples(u64) timeSec(f64) freq(f64) cents(f64) confidence(f64) tau(u64)
 */
function writeFloat64Records(path, records, fields) {
  const buf = Buffer.alloc(records.length * fields.length * 8);
  let off = 0;
  for (const rec of records) {
    for (const f of fields) {
      const v = f.kind === "u64" ? BigInt(Math.round(rec[f.key] ?? 0)) : Number(rec[f.key] ?? 0);
      if (f.kind === "u64") {
        buf.writeBigUInt64LE(v, off);
      } else {
        buf.writeDoubleLE(v, off);
      }
      off += 8;
    }
  }
  writeFileSync(path, buf);
  return buf.length;
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

const REALTIME_FIELDS = [
  { key: "offsetSamples", kind: "u64" },
  { key: "timeSec", kind: "f64" },
  { key: "freq", kind: "f64" },
  { key: "cents", kind: "f64" },
  { key: "confidence", kind: "f64" },
  { key: "tau", kind: "u64" },
];

/* ============================ 主流程 ============================ */

mkdirSync(OUT_DIR, { recursive: true });

const reference = {
  meta: {
    generator: "tools/gen-test-fixtures.mjs",
    upstreamEngine: UPSTREAM_ENGINE,
    upstreamEngineFMin: Engine.F_MIN,
    upstreamEngineFMax: Engine.F_MAX,
    upstreamFrameLadder: Engine.FRAME_LADDER,
    upstreamYinThreshold: Engine.YIN_THRESHOLD,
    sampleRate: SAMPLE_RATE,
    durationSec: DURATION_SEC,
    amplitude: AMPLITUDE,
    hopAnalyze: 441,
    realtimeFrame: 4096,
    realtimeHop: 512,
  },
  fixtures: [],
};

/// 供 C++ 侧读取的纯文本清单（每行 key=value，逐字解析，不引 JSON 库）
const metaLines = [
  `sampleRate=${SAMPLE_RATE}`,
  `hopAnalyze=${reference.meta.hopAnalyze}`,
  `realtimeFrame=${reference.meta.realtimeFrame}`,
  `realtimeHop=${reference.meta.realtimeHop}`,
  `fixtureCount=${FIXTURES.length}`,
];

let failures = 0;

for (const fx of FIXTURES) {
  const samples = synthesize({
    freq: fx.freq,
    partials: fx.partials,
    durationSec: DURATION_SEC,
    sampleRate: SAMPLE_RATE,
  });
  const wavPath = join(OUT_DIR, fx.file);
  writeFileSync(wavPath, encodeWavMono16(samples, SAMPLE_RATE));

  // 读回自己写的 WAV：保证 C++ 侧与真值用的是**同一批样点**
  // 注意上游 readWavMono 的返回字段名是 `rate`（不是 sampleRate），且 16 bit 归一化用 /32768。
  // C++ 侧 WavReader 必须采用同一口径，否则两端输入幅度不同，对拍会出现无法归因的差异。
  const { data, rate } = readWavMono(wavPath);
  if (rate !== SAMPLE_RATE) {
    console.error(`[FAIL] ${fx.file} 读回采样率为 ${rate}，期望 ${SAMPLE_RATE}`);
    failures++;
    continue;
  }

  const analysis = referenceFrames(data, rate, reference.meta.hopAnalyze);
  const realtime = referenceRealtimeFrames(data, rate,
    reference.meta.realtimeFrame, reference.meta.realtimeHop);

  // 自检：素材必须是"引擎能判对"的，否则它当不了对拍基准
  let selfCheck;
  if (fx.freq === null) {
    const ok = analysis.frames.length === 0;
    if (!ok) failures++;
    selfCheck = {
      expect: "无有效音高",
      actualFrames: analysis.frames.length,
      pass: ok,
    };
  } else {
    const expectNote = fx.note;
    const okNote = analysis.medianFreq > 0 && noteNameOf(analysis.medianFreq) === expectNote;
    const okCents = Math.abs(analysis.medianCents) < 50;
    const ok = okNote && okCents && analysis.frames.length > 0;
    if (!ok) failures++;
    selfCheck = {
      expectNote,
      actualNote: noteNameOf(analysis.medianFreq),
      medianFreq: Number(analysis.medianFreq.toFixed(4)),
      medianCents: Number(analysis.medianCents.toFixed(2)),
      frames: analysis.frames.length,
      realtimeFrames: realtime.frames.length,
      pass: ok,
      dualCondition: "音名正确 且 偏差 < 50 音分（pitfalls #26）",
    };
  }

  reference.fixtures.push({
    file: fx.file,
    note: fx.note,
    freq: fx.freq,
    partials: fx.partials,
    desc: fx.desc,
    selfCheck,
    analysis,
    realtime,
  });

  // 二进制真值：C++ 侧按 ByteReader 逐字段读取比对
  const stem = fx.file.replace(/\.wav$/, "");
  const analysisBin = `${stem}.analysis.f64`;
  const realtimeBin = `${stem}.realtime.f64`;
  const analysisNote = `${stem}.analysis.note`;
  const realtimeNote = `${stem}.realtime.note`;
  const analysisBytes = writeFloat64Records(join(OUT_DIR, analysisBin), analysis.frames, FRAME_FIELDS);
  const realtimeBytes = writeFloat64Records(join(OUT_DIR, realtimeBin), realtime.frames, REALTIME_FIELDS);

  // 音名单独存文本列：数值真值里没有音名，而"标签碰巧对、频率差一个八度"这类假命中
  // 只能靠逐帧音名比对抓出来（pitfalls #26）——八度误判是本项目的核心问题，不能只比频率。
  writeFileSync(join(OUT_DIR, analysisNote),
    analysis.frames.map((f) => f.note).join("\n") + (analysis.frames.length ? "\n" : ""), "ascii");
  writeFileSync(join(OUT_DIR, realtimeNote),
    realtime.frames.map((f) => f.note).join("\n") + (realtime.frames.length ? "\n" : ""), "ascii");

  metaLines.push(
    `fixture.${stem}.wav=${fx.file}`,
    // 音名用 ASCII 安全值（静音写 none）：清单文件要能被 C++ 侧按字节可靠读取，
    // 不引入非 ASCII 字符可避免"读取方按什么编码解释"的歧义（pitfalls #20 的同类问题）
    `fixture.${stem}.note=${fx.note === "—" ? "none" : fx.note}`,
    `fixture.${stem}.expectFreq=${fx.freq === null ? "none" : fx.freq}`,
    `fixture.${stem}.analysisBin=${analysisBin}`,
    `fixture.${stem}.analysisNote=${analysisNote}`,
    `fixture.${stem}.analysisBytes=${analysisBytes}`,
    `fixture.${stem}.analysisFrames=${analysis.frames.length}`,
    `fixture.${stem}.realtimeBin=${realtimeBin}`,
    `fixture.${stem}.realtimeNote=${realtimeNote}`,
    `fixture.${stem}.realtimeBytes=${realtimeBytes}`,
    `fixture.${stem}.realtimeFrames=${realtime.frames.length}`,
  );

  const flag = selfCheck.pass ? "PASS" : "FAIL";
  console.log(`[${flag}] ${fx.file}  ${fx.desc}`);
  console.log(`        自检: ${JSON.stringify(selfCheck)}`);
  console.log(`        真值: 分析 ${analysis.frames.length} 帧 / 实时 ${realtime.frames.length} 帧`);
}

const refPath = join(OUT_DIR, "reference.json");
writeFileSync(refPath, JSON.stringify(reference, null, 2), "utf8");

const metaPath = join(OUT_DIR, "reference.txt");
writeFileSync(metaPath, metaLines.join("\n") + "\n", "utf8");

console.log("");
console.log(`真值（明细）: ${refPath}`);
console.log(`真值（清单）: ${metaPath}`);
console.log(`素材目录    : ${OUT_DIR}`);
console.log(`自检失败项  : ${failures}`);
if (failures > 0) {
  console.error("[FAIL] 有素材未通过自检——它不能作为对拍基准，请调整素材参数后重跑。");
  process.exit(1);
}

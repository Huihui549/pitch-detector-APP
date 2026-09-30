/**
 * 参数同源检查（verify.md B3）
 *
 * 为什么必须机械化：
 *   上游 pitch-engine.js 的每个常数都是从失败方案里实测筛出来的（阈值 0.3、窗长阶梯、
 *   浅谷阈值 0.15、谐波改判倍数 1.15…）。本项目移植时把它们抄进了 src/core/pitch-types.h，
 *   "抄错一位"不会有任何编译错误，只会静默降低准确度。
 *   靠人眼核对两份文件不可靠，故做成本脚本：**逐项比对，不一致即失败**。
 *
 * 比对口径：只比"数值"（整数、浮点、数组），不比注释与命名。
 *   命名差异（JS 的 F_MIN ↔ C++ 的 kDefaultFMin）在映射表里显式声明，避免误报。
 *
 * 用法（[PC] 仓库根目录）：
 *   node tools/compare-params.mjs
 *
 * 退出码：0 = 全部一致；1 = 有不一致（或上游/本地文件缺失）。
 */

import { readFileSync, existsSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

import { getUpstreamEngine } from "./_path-policy.mjs";

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO = resolve(HERE, "..");

const UPSTREAM = getUpstreamEngine();
const CPP_HEADER = join(REPO, "src", "core", "pitch-types.h");

/**
 * 映射表：上游 JS 常量 ↔ 本项目 C++ 常量。
 * `kind` 决定比对方式：number（数值容差内相等）、intArray（逐元素相等）。
 * `tolerance` 只用于浮点；整数与数组用精确比较。
 */
const MAPPING = [
  { js: "A4", cpp: "kDefaultA4", kind: "number", note: "A4 基准频率" },
  { js: "F_MIN", cpp: "kDefaultFMin", kind: "number", note: "音域下限" },
  { js: "F_MAX", cpp: "kDefaultFMax", kind: "number", note: "音域上限" },
  { js: "FRAME_LADDER", cpp: "kFrameLadder", kind: "intArray", note: "级联窗长" },
  { js: "MAX_FRAME", cpp: "kMaxFrame", kind: "number", note: "最大窗长" },
  { js: "YIN_THRESHOLD", cpp: "kYinThreshold", kind: "number", note: "YIN 阈值" },
  { js: "RMS_MIN", cpp: "kRmsMin", kind: "number", note: "绝对静音门槛" },
  { js: "RMS_REL_MIN", cpp: "kRmsRelMin", kind: "number", note: "相对静音门槛" },
  { js: "SHALLOW", cpp: "kShallowThreshold", kind: "number", note: "浅谷复核阈值" },
  { js: "REFINE_MIN_HZ", cpp: "kRefineMinHz", kind: "number", note: "频域精修下限" },
  { js: "REFINE_STEPS", cpp: "kRefineSteps", kind: "number", note: "频域精修步数" },
  { js: "REFINE_SPAN", cpp: "kRefineSpan", kind: "number", note: "频域精修半宽" },
  { js: "HARMONIC_MAX", cpp: "kHarmonicMax", kind: "number", note: "谐波最高次" },
  { js: "SUBHARMONIC_MIN_HZ", cpp: "kSubharmonicMinHz", kind: "number", note: "分频下限" },
];

/**
 * 还要比对的"内联字面量"：上游写在函数体里的魔法数，本项目提成了具名常量。
 * 这些没有同名标识符可查，只能按"该值必须在上游出现"来校验，防止抄错。
 */
const INLINE_LITERALS = [
  { cpp: "kPreferFundamentalRatio", jsValue: 1.15, note: "谐波改判优势倍数" },
  { cpp: "kOctaveToleranceCents", jsValue: 120.0, note: "八度折回容差（音分，在 octave-unifier.h）", header: "octave-unifier.h" },
];

/** 从 JS 源码抽取顶层 const/let 定义（数值、数组，以及"由数组长度计算的"标量）。 */
function parseJsConstants(source) {
  const out = new Map();

  // 数值：const NAME = 123;
  for (const m of source.matchAll(/^\s*(?:const|let)\s+([A-Z][A-Z0-9_]*)\s*=\s*(-?\d+(?:\.\d+)?)\s*;/gm)) {
    out.set(m[1], Number(m[2]));
  }
  // 数组：const NAME = [1, 2, 3];
  for (const m of source.matchAll(/^\s*(?:const|let)\s+([A-Z][A-Z0-9_]*)\s*=\s*\[([^\]]*)\]\s*;/gm)) {
    const items = m[2].split(",").map((s) => s.trim()).filter((s) => s.length > 0).map(Number);
    if (items.every((v) => Number.isFinite(v))) {
      out.set(m[1], items);
    }
  }
  // 计算值：const NAME = OTHER[OTHER.length - 1];  —— 这是上游的写法，必须支持，
  // 否则会把它误报成"上游未找到该常量"（本项目第一次跑就踩了这个误报）
  for (const m of source.matchAll(
    /^\s*(?:const|let)\s+([A-Z][A-Z0-9_]*)\s*=\s*([A-Z][A-Z0-9_]*)\s*\[\s*\2\.length\s*-\s*1\s*\]\s*;/gm)) {
    const src = out.get(m[2]);
    if (Array.isArray(src) && src.length > 0) {
      out.set(m[1], src[src.length - 1]);
    }
  }
  return out;
}

/** 从 C++ 源码抽取 kFoo 常量（顶层与类内均支持）。 */
function parseCppConstants(source) {
  const out = new Map();

  // 数值：支持 `inline constexpr double kFoo = 1.5;` 与类内 `static constexpr double kFoo = 1.5;`
  // （后者出现在 octave-unifier.h 的私有常量区，第一版解析器漏了它）
  for (const m of source.matchAll(
    /(?:inline\s+|static\s+)*constexpr\s+(?:double|int|std::size_t|unsigned)\s+(k[A-Za-z0-9_]+)\s*=\s*(-?\d+(?:\.\d+)?)\s*;/g)) {
    out.set(m[1], Number(m[2]));
  }
  // 数组：inline constexpr std::array<std::size_t, N> kFoo{1, 2, 3};
  for (const m of source.matchAll(
    /(?:inline\s+|static\s+)*constexpr\s+std::array<[^>]*>\s+(k[A-Za-z0-9_]+)\s*\{([^}]*)\}/g)) {
    const items = m[2].split(",").map((s) => s.trim()).filter((s) => s.length > 0).map(Number);
    if (items.every((v) => Number.isFinite(v))) {
      out.set(m[1], items);
    }
  }
  return out;
}

/* ============================ 主流程 ============================ */

if (!existsSync(UPSTREAM)) {
  console.error(`[FAIL] 找不到上游引擎：${UPSTREAM}`);
  console.error("      它是参数真值的唯一来源，缺失时不得静默跳过（pitfalls A7/A9）。");
  process.exit(1);
}
if (!existsSync(CPP_HEADER)) {
  console.error(`[FAIL] 找不到本地参数头文件：${CPP_HEADER}`);
  process.exit(1);
}

const jsSource = readFileSync(UPSTREAM, "utf8");
const jsConsts = parseJsConstants(jsSource);

const cppSource = readFileSync(CPP_HEADER, "utf8");
const cppConsts = parseCppConstants(cppSource);

// 附加头文件（如 octave-unifier.h）里的内联字面量
const extraHeaders = new Map();
for (const item of INLINE_LITERALS) {
  if (item.header) {
    const p = join(REPO, "src", "core", item.header);
    if (existsSync(p)) {
      const src = readFileSync(p, "utf8");
      for (const [k, v] of parseCppConstants(src)) {
        extraHeaders.set(k, v);
      }
    }
  }
}

console.log("参数同源检查（C++ 常数 vs 上游 pitch-engine.js）");
console.log(`  上游: ${UPSTREAM}`);
console.log(`  本地: ${CPP_HEADER}`);
console.log("");
console.log(`  上游抽取到 ${jsConsts.size} 个常量，本地抽取到 ${cppConsts.size} 个常量`);
console.log("");

let failures = 0;
let checked = 0;

function numEq(a, b, tol = 1e-12) {
  return Math.abs(a - b) <= tol * Math.max(1, Math.abs(a), Math.abs(b));
}

console.log("| 上游常量 | 本地常量 | 上游值 | 本地值 | 结果 |");
console.log("|---|---|---|---|---|");

for (const m of MAPPING) {
  checked++;
  const jsVal = jsConsts.get(m.js);
  const cppVal = cppConsts.get(m.cpp);

  let jsText = "缺失";
  let cppText = "缺失";
  let ok = false;
  let detail = "";

  if (jsVal === undefined) {
    detail = "上游未找到该常量（可能已改名，需更新映射表）";
  } else if (cppVal === undefined) {
    detail = "本地未找到该常量（可能已改名，需更新映射表）";
  } else if (m.kind === "intArray") {
    jsText = `[${jsVal.join(",")}]`;
    cppText = `[${cppVal.join(",")}]`;
    ok = Array.isArray(jsVal) && Array.isArray(cppVal) && jsVal.length === cppVal.length &&
         jsVal.every((v, i) => v === cppVal[i]);
  } else {
    jsText = String(jsVal);
    cppText = String(cppVal);
    ok = numEq(jsVal, cppVal);
  }

  if (!ok) failures++;
  const resultText = ok ? "一致" : `**不一致**${detail ? " — " + detail : ""}`;
  console.log(`| ${m.js}（${m.note}） | ${m.cpp} | ${jsText} | ${cppText} | ${resultText} |`);
}

console.log("");
console.log("| 上游字面量 | 本地常量 | 上游值 | 本地值 | 结果 |");
console.log("|---|---|---|---|---|");

for (const lit of INLINE_LITERALS) {
  checked++;
  const cppVal = lit.header ? extraHeaders.get(lit.cpp) : cppConsts.get(lit.cpp);
  const cppText = cppVal === undefined ? "缺失" : String(cppVal);
  let ok = false;
  if (cppVal === undefined) {
    // 允许该常量不在 pitch-types.h（例如在 octave-unifier.h）
    console.log(`| ${lit.note} | ${lit.cpp} | ${lit.jsValue} | ${cppText} | **未找到**（需检查声明位置） |`);
    failures++;
    continue;
  }
  ok = numEq(cppVal, lit.jsValue);
  if (!ok) failures++;
  console.log(`| ${lit.note} | ${lit.cpp} | ${lit.jsValue} | ${cppText} | ${ok ? "一致" : "**不一致**"} |`);
}

// 反向检查：本地声明了但映射表未覆盖的数值常量（提示可能的漏比）
const mappedCpp = new Set([...MAPPING.map((m) => m.cpp), ...INLINE_LITERALS.map((l) => l.cpp)]);
const unmapped = [...cppConsts.keys()].filter((k) => !mappedCpp.has(k) && k !== "kNoteNames" && k !== "kLutSize");

console.log("");
if (unmapped.length) {
  console.log(`提示：本地另有 ${unmapped.length} 个常量未纳入比对（多为实现细节而非上游参数）：`);
  console.log(`  ${unmapped.join(", ")}`);
  console.log("");
}

console.log(`比对 ${checked} 项，不一致 ${failures} 项。`);
if (failures > 0) {
  console.error("[FAIL] 参数不同源——必须与上游一致，或在 dev-docs 的 ADR 中显式说明差异并附实测数据。");
  process.exit(1);
}
console.log("[PASS] 全部参数与上游一致。");

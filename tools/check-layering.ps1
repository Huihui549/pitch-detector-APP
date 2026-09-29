# Layering gate (executor of verify.md C4)
#
# Why: the biggest structural risk of this project is "algorithm leaked into the UI layer"
# (upstream pitfalls #22 once produced three copies of the algorithm and divergent results).
# This turns the layering rule into a mechanically checkable gate.
#
# Checks:
#   1. src/core/ must not include any Qt header (core stays pure C++, testable without Qt)
#   2. qml/ must not contain any algorithm trace (thresholds, window sizes, range, conversion)
#   3. no second definition of an algorithm function anywhere under src/
#
# IMPORTANT (upstream pitfalls #11): keep this file PURE ASCII and avoid PS7-only parameters.
# Windows PowerShell 5.1 decodes a BOM-less file as ANSI; Chinese text then corrupts the parser.
# It also has no -Recurse on Select-String, hence the Get-ChildItem | Select-String form below.
#
# Usage (repo root):
#   powershell -ExecutionPolicy Bypass -File tools/check-layering.ps1

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$script:fail = 0

function Report {
    param([string]$Name, [bool]$Pass, [string]$Detail)
    if ($Pass) {
        Write-Output "[PASS] $Name"
    } else {
        Write-Output "[FAIL] $Name"
        if ($Detail) { Write-Output "       $Detail" }
        $script:fail++
    }
}

# ASCII-only way to print the handful of Chinese words we want in the summary.
$cnQmlNotCreated = [string]([char]0x5C1A + [char]0x672A + [char]0x521B + [char]0x5EFA)
$cnFailedItems = [string]([char]0x5931 + [char]0x8D25 + [char]0x9879)

function Search-Files {
    param([string]$Dir, [string]$Pattern, [switch]$Recurse)
    $files = if ($Recurse) {
        Get-ChildItem -Path $Dir -Recurse -File -ErrorAction SilentlyContinue
    } else {
        Get-ChildItem -Path $Dir -File -ErrorAction SilentlyContinue
    }
    if (-not $files) { return @() }
    return @($files | Select-String -Pattern $Pattern -ErrorAction SilentlyContinue)
}

# ---------- 1. src/core must not include Qt headers ----------
$coreDir = Join-Path $root "src\core"
if (Test-Path $coreDir) {
    $hits = Search-Files -Dir $coreDir -Pattern '#include\s*<Q'
    $detail = ""
    if ($hits.Count -gt 0) {
        $detail = ($hits | ForEach-Object { "  $($_.Filename):$($_.LineNumber)" }) -join "`n"
    }
    Report "src/core has no Qt dependency" ($hits.Count -eq 0) $detail
} else {
    Report "src/core has no Qt dependency" $false "missing directory: $coreDir"
}

# ---------- 2. qml/ must not contain algorithm traces ----------
$qmlDir = Join-Path $root "qml"
if (Test-Path $qmlDir) {
    # These symbols belong to the C++ algorithm layer only.
    $banned = @('4300', '16384', '\b4096\b', '\b0\.3\b', 'log2', 'FRAME_LADDER', '\bYIN\b')
    $qmlHits = @()
    foreach ($pattern in $banned) {
        $qmlHits += Search-Files -Dir $qmlDir -Pattern $pattern -Recurse
    }
    $qmlDetail = ($qmlHits | ForEach-Object { "  $($_.Filename):$($_.LineNumber) $($_.Line.Trim())" }) -join "`n"
    Report "qml has no algorithm trace" ($qmlHits.Count -eq 0) $qmlDetail
} else {
    Write-Output "[SKIP] qml/ $cnQmlNotCreated"
}

# ---------- 3. algorithm definitions must be unique ----------
# Overloads of the SAME function inside the same class/file are legitimate (e.g. detect() with and
# without the debug-curve out-parameters), so the signature tail is part of the key. What must not
# happen is the same name+signature implemented in two different files (upstream pitfalls #22).
$srcDir = Join-Path $root "src"
$defPattern = '^\s*(?:double|int|NoteInfo|std::optional<PitchResult>|std::vector<RealtimeFrame>)\s+(?:PitchEngine|NoteConverter|OctaveUnifier|AnalysisRunner|RealtimeRunner)::(detect|detectWithLadder|detectWithLadderSizes|refineTau|refineFreqByCorrelation|harmonicScore|preferFundamental|fromFrequency|fromMidi|apply|analyze|run)\s*\(([^)]*)\)'
$defs = @{}
$defHits = Search-Files -Dir $srcDir -Pattern $defPattern -Recurse
foreach ($m in $defHits) {
    # key = class::name + normalized parameter list (whitespace collapsed)
    $params = ($m.Matches[0].Groups[2].Value -replace '\s+', ' ').Trim()
    $key = $m.Matches[0].Groups[0].Value -replace '^.*?(\w+)::(\w+)\s*\(([^)]*)\).*$', '$1::$2'
    $key = "$key($params)"
    if (-not $defs.ContainsKey($key)) { $defs[$key] = @() }
    $defs[$key] += "$($m.Filename):$($m.LineNumber)"
}
$dupDetail = ""
$dupFound = $false
foreach ($k in $defs.Keys) {
    # 同一签名出现在多于一个文件里才算重复实现
    $files = $defs[$k] | ForEach-Object { ($_ -split ':')[0] } | Select-Object -Unique
    if ($files.Count -gt 1) {
        $dupFound = $true
        $dupDetail += "  $k defined at $($defs[$k] -join ', ')`n"
    }
}
Report "algorithm definitions are unique" (-not $dupFound) $dupDetail.TrimEnd()

# ---------- summary ----------
Write-Output ""
if ($script:fail -eq 0) {
    Write-Output "Layering gate: all checks passed."
    exit 0
} else {
    Write-Output "Layering gate: $($script:fail) $cnFailedItems"
    exit 1
}

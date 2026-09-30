# check-theme.ps1 -- UI style gate for qml/
#
# WHY THIS EXISTS
#   "Keep the same style" cannot rely on memory. The style is defined ONCE in
#   src/app/theme.h (see ADR-0012 and dev-docs/pitch-detector-APP/design/ui-style.md).
#   This script fails the build if qml/ introduces a second place that defines
#   a colour, a font size or a corner radius.
#
# FORBIDDEN in qml/**.qml (unless the line carries the ignore marker below)
#   1) hex colour literals ................ #rgb / #rrggbb / #aarrggbb
#   2) raw colour constructors ............ Qt.rgba( / Qt.hsla(
#      (Qt.darker()/lighter() on a Theme token is allowed: the token stays the source)
#   3) hard-coded font size ............... font.pixelSize: <number>
#   4) hard-coded corner radius ........... radius: <number>
#
# ESCAPE HATCH
#   Append   <- theme-check: ignore   to a line to exempt it (use sparingly and
#   say why in a comment above it).
#
# ENCODING / COMPATIBILITY (see project pitfall A12)
#   ASCII ONLY, and Windows PowerShell 5.1 compatible (no PS7-only parameters).
#   Any non-ASCII text here would be mis-decoded by PowerShell 5.1.
#
# NEGATIVE VERIFICATION (project pitfall A13: a gate must be proven able to FAIL)
#   A gate that always passes is worse than no gate. After changing this script,
#   run it against a deliberately broken probe, e.g.:
#       Set-Content qml/_probe.qml 'import QtQuick
# Rectangle { color: "#ff0000" }'
#       powershell -ExecutionPolicy Bypass -File tools/check-theme.ps1   # must FAIL
#       Remove-Item qml/_probe.qml

param(
    [string]$RepoRoot = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrEmpty($RepoRoot)) {
    $RepoRoot = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
}
$QmlDir = Join-Path $RepoRoot "qml"

Write-Output "repo root : $RepoRoot"
Write-Output "scanning  : $QmlDir"

if (-not (Test-Path $QmlDir)) {
    Write-Output "[FAIL] qml/ directory not found"
    exit 1
}

$files = Get-ChildItem -Path $QmlDir -Recurse -File -Filter "*.qml"
$violations = @()

foreach ($file in $files) {
    $lineNo = 0
    foreach ($line in (Get-Content -LiteralPath $file.FullName)) {
        $lineNo = $lineNo + 1
        if ($line -match "theme-check: ignore") { continue }
        # skip whole-line comments: documentation often has to SHOW a literal colour
        # (e.g. Icon.qml explains that #ffffff is required). Trailing comments are still
        # checked, which is what we want.
        if ($line.TrimStart().StartsWith("//")) { continue }

        if ($line -match '#[0-9a-fA-F]{3,8}') {
            $violations += "{0}:{1}: hex colour literal -> use a Theme token`n    {2}" -f $file.Name, $lineNo, $line.Trim()
        }
        if ($line -match 'Qt\.(rgba|hsla)\s*\(') {
            $violations += "{0}:{1}: raw colour constructor -> use a Theme token`n    {2}" -f $file.Name, $lineNo, $line.Trim()
        }
        if ($line -match 'font\.pixelSize\s*:\s*-*[0-9]') {
            $violations += "{0}:{1}: hard-coded font size -> use Theme.font*`n    {2}" -f $file.Name, $lineNo, $line.Trim()
        }
        if ($line -match '(^|[^\.\w])radius\s*:\s*-*[0-9]') {
            $violations += "{0}:{1}: hard-coded radius -> use Theme.radius*`n    {2}" -f $file.Name, $lineNo, $line.Trim()
        }
    }
}

Write-Output ("scanned   : {0} qml files" -f $files.Count)

if ($violations.Count -gt 0) {
    Write-Output ""
    Write-Output "[FAIL] qml/ must not define colours / font sizes / radii outside Theme:"
    foreach ($v in $violations) {
        Write-Output ("  " + $v)
    }
    Write-Output ""
    Write-Output ("{0} violation(s). Add a token to src/app/theme.h instead." -f $violations.Count)
    exit 1
}

# ---- icon assets: must be pure white so they can be tinted by Icon.qml --------------
# Lucide ships stroke="currentColor", which Qt renders BLACK; and MultiEffect mixes
# colorizationColor BY SOURCE LUMINANCE, so black stays black (invisible on dark UI).
# See resources/ATTRIBUTION.md ("local modification") for the full story.
$IconDir = Join-Path $RepoRoot "resources\icons"
$iconFiles = @()
if (Test-Path $IconDir) {
    $iconFiles = Get-ChildItem -Path $IconDir -File -Filter "*.svg"
}
$iconViolations = @()
foreach ($file in $iconFiles) {
    $raw = Get-Content -LiteralPath $file.FullName -Raw
    if ($raw -match "currentColor") {
        $iconViolations += ("{0}: contains currentColor -> replace with #ffffff" -f $file.Name)
    }
    foreach ($m in [regex]::Matches($raw, "#[0-9a-fA-F]{3,8}")) {
        if ($m.Value.ToLower() -ne "#ffffff") {
            $iconViolations += ("{0}: colour {1} -> icons must be pure white to be tintable" -f $file.Name, $m.Value)
        }
    }
}
Write-Output ("scanned   : {0} icon files" -f $iconFiles.Count)

if ($iconViolations.Count -gt 0) {
    Write-Output ""
    Write-Output "[FAIL] icon assets must be pure white (see resources/ATTRIBUTION.md):"
    foreach ($v in $iconViolations) {
        Write-Output ("  " + $v)
    }
    exit 1
}

Write-Output "[PASS] icon assets are pure white (tintable)"
exit 0

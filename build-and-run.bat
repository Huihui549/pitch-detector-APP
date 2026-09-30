@echo off
REM ------------------------------------------------------------------
REM One-click build + deploy + run (qmake build system).
REM
REM Purpose: double-click after editing code; does build -> deploy -> start.
REM Always re-runs qmake (qmake uses -incremental and does NOT re-read the
REM .pro, see pitfall A22).
REM
REM ================== ENCODING: THIS FILE MUST STAY PURE ASCII ==================
REM cmd.exe decodes .bat files BYTE-WISE using the local code page (GBK here).
REM A GBK double-byte sequence swallows the following byte, so a single
REM non-ASCII character corrupts the parsing of the NEXT line as well --
REM observed symptoms: "'cho.' is not recognized", "The system cannot find
REM the path specified", and `set "PATH=..."` being shredded so the build
REM silently never starts.
REM   * UTF-8 without BOM  -> broken (bytes mis-decoded as GBK)
REM   * UTF-8 with BOM     -> still broken (cmd does not honour the BOM here)
REM   * `chcp 65001` first -> still broken (cmd reads and executes line by
REM                            line, so the code page switches too late)
REM Therefore: keep this file ASCII-only. Chinese explanations live in
REM AGENTS.md and dev-docs/pitch-detector-APP/pitfalls.md (pitfall A34).
REM Verify with:  node tools/check-ascii-bat.mjs
REM
REM ================== MACHINE-INDEPENDENT BY DESIGN ==================
REM This project is developed on two computers, so repository location differs.
REM This script therefore contains NO absolute machine path. Qt is located by:
REM   1) environment variable PITCH_QT_ROOT   -- explicit and most reliable
REM   2) reverse-lookup from qmake.exe on PATH -- always true in a Qt Creator
REM      terminal
REM If both fail it reports clearly instead of silently using a wrong path.
REM The same order is implemented in tools/_path-policy.mjs for the Node tools.
REM ------------------------------------------------------------------

setlocal enabledelayedexpansion

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"

REM ---------------- 1. locate the Qt installation root ----------------
set "QT_ROOT="

if defined PITCH_QT_ROOT (
    if exist "%PITCH_QT_ROOT%\6.8.3\mingw_64\bin\qmake.exe" (
        set "QT_ROOT=%PITCH_QT_ROOT%"
        echo [info] Qt root taken from PITCH_QT_ROOT
    ) else (
        echo [warn] PITCH_QT_ROOT is invalid: %PITCH_QT_ROOT%
        echo        expected: %%PITCH_QT_ROOT%%\6.8.3\mingw_64\bin\qmake.exe
    )
)

if not defined QT_ROOT (
    for %%Q in (qmake.exe) do (
        set "QMAKE_FOUND=%%~$PATH:Q"
    )
    if defined QMAKE_FOUND (
        REM Shape is QtRoot\6.8.3\mingw_64\bin\qmake.exe -> go up 3 levels.
        for %%Q in ("!QMAKE_FOUND!") do (
            set "QT_ROOT=%%~dpQ..\..\.."
        )
        for %%Q in ("!QT_ROOT!") do set "QT_ROOT=%%~fQ"
        set "QMAKE_FOUND="
        echo [info] Qt root derived from qmake on PATH: !QT_ROOT!
    )
)

if not defined QT_ROOT (
    echo(
    echo [ERROR] Qt installation root not found.
    echo         Expected layout: QtRoot\6.8.3\mingw_64\bin\qmake.exe
    echo(
    echo         Two ways to fix it, either is enough:
    echo           1^) set the environment variable PITCH_QT_ROOT to your Qt
    echo              installation root, for example:
    echo                setx PITCH_QT_ROOT QtRoot
    echo           2^) open a terminal from Qt Creator and run this script;
    echo              its PATH already contains Qt.
    echo(
    echo         Two cmd pitfalls already hit in this exact block, keep them
    echo         in mind when editing it:
    echo           - an unbalanced double quote inside an echo line breaks
    echo             block parsing: 'set was unexpected at this time'
    echo           - a bare 'echo.' inside a parenthesised block makes cmd
    echo             try to run '.': '. was unexpected at this time'
    echo         Use 'echo(' instead of 'echo.' here.
    echo(
    pause
    exit /b 1
)

set "QT_BIN=%QT_ROOT%\6.8.3\mingw_64\bin"
set "QT_PLUGINS=%QT_ROOT%\6.8.3\mingw_64\plugins"
set "MINGW_BIN=%QT_ROOT%\Tools\mingw1310_64\bin"
set "JOM=%QT_ROOT%\Tools\jom\jom.exe"

if not exist "%QT_BIN%\qmake.exe" (
    echo [ERROR] qmake not found: %QT_BIN%\qmake.exe
    pause
    exit /b 1
)
if not exist "%MINGW_BIN%\g++.exe" (
    echo [ERROR] MinGW compiler not found: %MINGW_BIN%\g++.exe
    echo         Check whether QtRoot\Tools\mingw1310_64 is complete.
    pause
    exit /b 1
)

REM Fall back to mingw32-make when jom is absent.
REM jom ships with the Qt SDK itself (Tools\jom) since Qt Creator was unbundled;
REM the old Tools\QtCreator\bin\jom path no longer exists here.
if not exist "%JOM%" (
    if exist "%MINGW_BIN%\mingw32-make.exe" (
        set "JOM=%MINGW_BIN%\mingw32-make.exe"
        echo [info] jom not found, falling back to mingw32-make
    ) else (
        echo [ERROR] neither jom nor mingw32-make found
        pause
        exit /b 1
    )
)

set "PATH=%QT_BIN%;%MINGW_BIN%;%PATH%"
cd /d "%ROOT%"

REM Kill a running instance first: on Windows a running exe is locked and the
REM linker reports "cannot open output file ...: Permission denied" (observed).
taskkill /IM pitch-detector-APP.exe /F >nul 2>&1
if errorlevel 1 (
    REM No running instance, which is normal.
) else (
    echo [info] terminated the previous running instance
)

echo [1/5] qmake ...
"%QT_BIN%\qmake.exe" pitch-detector-APP.pro CONFIG+=release
if errorlevel 1 goto :fail

echo [2/5] build ...
"%JOM%"
if errorlevel 1 goto :fail

echo [3/5] deploy Qt runtime (windeployqt) ...
if not exist "run\" mkdir run
copy /y "bin\pitch-detector-APP.exe" "run\" >nul
"%QT_BIN%\windeployqt.exe" --release --qmldir qml --no-translations --no-system-d3d-compiler --no-opengl-sw "run\pitch-detector-APP.exe" >nul
if errorlevel 1 goto :fail

echo [4/5] add the dependency windeployqt does not handle (Qt Multimedia) ...
REM windeployqt only infers from the executable's direct dependencies, while
REM Qt Multimedia is loaded at runtime through a plugin backend. It must be
REM copied explicitly, otherwise the microphone is unavailable (observed:
REM --devices reports a missing module when they are absent).
if exist "%QT_BIN%\Qt6Multimedia.dll" (
    copy /y "%QT_BIN%\Qt6Multimedia.dll" "run\" >nul
    if exist "%QT_BIN%\Qt6Network.dll" copy /y "%QT_BIN%\Qt6Network.dll" "run\" >nul
    if exist "%QT_BIN%\Qt6MultimediaQuick.dll" copy /y "%QT_BIN%\Qt6MultimediaQuick.dll" "run\" >nul
    if not exist "run\multimedia\" mkdir "run\multimedia"
    copy /y "%QT_PLUGINS%\multimedia\*.dll" "run\multimedia\" >nul
)

echo [5/5] start ...
start "" "run\pitch-detector-APP.exe"
exit /b 0

:fail
echo.
echo [FAILED] build or deploy failed, see the output above.
pause
exit /b 1

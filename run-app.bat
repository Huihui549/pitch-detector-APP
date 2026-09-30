@echo off
REM ------------------------------------------------------------------
REM Run the pitch detection tool (self-contained deployed build).
REM
REM Just double-click. The run\ directory is produced by windeployqt and
REM contains the Qt runtime plus standard QML modules; this project's own QML
REM is compiled into the exe resources, so it does not depend on any QML
REM layout on disk (a side benefit of switching to qmake).
REM
REM To redeploy after changing code: run build-and-run.bat instead. It does
REM qmake -> build -> windeployqt -> start and locates Qt by itself, which is
REM why this file needs no machine-specific path at all.
REM
REM NOTE: keep this file PURE ASCII -- see the encoding note in
REM build-and-run.bat and pitfall A34.
REM ------------------------------------------------------------------

setlocal
set "APP=%~dp0run\pitch-detector-APP.exe"

if not exist "%APP%" (
    echo [ERROR] executable not found: %APP%
    echo         Run build-and-run.bat first to build and deploy.
    pause
    exit /b 1
)

start "" "%APP%"
endlocal

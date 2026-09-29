@echo off
REM ------------------------------------------------------------------
REM 一键构建 + 部署 + 运行（qmake 构建系统）
REM
REM 用途：改了代码后直接双击本文件，自动完成 构建 -> 部署 -> 启动。
REM 每次都重新 qmake（qmake 用 -incremental，不重读 .pro，见坑 A22）。
REM ------------------------------------------------------------------

setlocal

set "QT_BIN=D:\Qt\6.8.3\mingw_64\bin"
set "QT_PLUGINS=D:\Qt\6.8.3\mingw_64\plugins"
set "MINGW_BIN=D:\Qt\Tools\mingw1310_64\bin"
set "JOM=D:\Qt\Tools\QtCreator\bin\jom\jom.exe"
set "ROOT=%~dp0"

if not exist "%QT_BIN%\qmake.exe" (
    echo [错误] 找不到 Qt：%QT_BIN%
    pause
    exit /b 1
)

set "PATH=%QT_BIN%;%MINGW_BIN%;%PATH%"
cd /d "%ROOT%"

REM 先结束正在运行的实例：Windows 上运行中的 exe 会被锁住，
REM 链接器会报 "cannot open output file ...: Permission denied"（实测踩过）。
taskkill /IM pitch-detector-APP.exe /F >nul 2>&1
if errorlevel 1 (
    REM 没有运行中的实例，属正常
) else (
    echo [信息] 已结束正在运行的旧实例
)

echo [1/5] qmake ...
"%QT_BIN%\qmake.exe" pitch-detector-APP.pro CONFIG+=release
if errorlevel 1 goto :fail

echo [2/5] 构建 ...
"%JOM%"
if errorlevel 1 goto :fail

echo [3/5] 部署 Qt 运行库（windeployqt）...
if not exist "run\" mkdir run
copy /y "bin\pitch-detector-APP.exe" "run\" >nul
"%QT_BIN%\windeployqt.exe" --release --qmldir qml --no-translations --no-system-d3d-compiler --no-opengl-sw "run\pitch-detector-APP.exe" >nul
if errorlevel 1 goto :fail

echo [4/5] 补齐 windeployqt 不会自动处理的依赖（Qt Multimedia）...
REM windeployqt 只按可执行文件的直接依赖推断；Qt Multimedia 在运行期按需加载
REM （插件式后端），必须显式带上，否则麦克风不可用（实测：不拷贝时 --devices 报模块缺失）。
if exist "%QT_BIN%\Qt6Multimedia.dll" (
    copy /y "%QT_BIN%\Qt6Multimedia.dll" "run\" >nul
    if exist "%QT_BIN%\Qt6Network.dll" copy /y "%QT_BIN%\Qt6Network.dll" "run\" >nul
    if exist "%QT_BIN%\Qt6MultimediaQuick.dll" copy /y "%QT_BIN%\Qt6MultimediaQuick.dll" "run\" >nul
    if not exist "run\multimedia\" mkdir "run\multimedia"
    copy /y "%QT_PLUGINS%\multimedia\*.dll" "run\multimedia\" >nul
)

echo [5/5] 启动 ...
start "" "run\pitch-detector-APP.exe"
exit /b 0

:fail
echo.
echo [失败] 构建或部署出错，请查看上面的输出。
pause
exit /b 1

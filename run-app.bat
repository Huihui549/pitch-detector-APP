@echo off
REM ------------------------------------------------------------------
REM 运行音高检测工具（自包含部署版）
REM
REM 直接双击本文件即可。run\ 目录由 windeployqt 生成，含 Qt 运行库与
REM 标准 QML 模块；本项目自己的 QML 界面已编进 exe 的资源里，
REM 因此不依赖任何磁盘 QML 布局（这是改用 qmake 的额外收益）。
REM
REM 重新部署（改了代码之后）：
REM   D:\Qt\6.8.3\mingw_64\bin\qmake.exe pitch-detector-APP.pro CONFIG+=release
REM   D:\Qt\Tools\QtCreator\bin\jom\jom.exe
REM   D:\Qt\6.8.3\mingw_64\bin\windeployqt.exe --release --qmldir qml --no-translations --no-system-d3d-compiler --no-opengl-sw run\pitch-detector-APP.exe
REM ------------------------------------------------------------------

setlocal
set "APP=%~dp0run\pitch-detector-APP.exe"

if not exist "%APP%" (
    echo [错误] 找不到程序：%APP%
    echo         请先构建并部署（命令见本文件顶部注释），或改用 build-and-run.bat。
    pause
    exit /b 1
)

start "" "%APP%"
endlocal

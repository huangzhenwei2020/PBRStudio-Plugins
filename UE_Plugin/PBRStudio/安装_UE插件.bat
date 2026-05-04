@echo off
chcp 65001 >nul
title PBRStudio UE 插件安装程序 v1.1.3

echo.
echo   ╔══════════════════════════════════════════════╗
echo   ║     PBR Studio — UE 插件安装程序 v1.1.3      ║
echo   ╚══════════════════════════════════════════════╝
echo.
echo   此脚本将 PBRStudio 插件安装到你的 UE 项目中。
echo.

REM ── 获取用户项目路径 ──────────────────────────────
:GET_PROJECT_PATH
set /p PROJECT_PATH="  请输入 UE 项目根目录路径 (例: D:\MyProject): "

if "%PROJECT_PATH%"=="" (
    echo   路径不能为空，请重新输入。
    echo.
    goto GET_PROJECT_PATH
)

REM 去掉末尾的引号和反斜杠
set PROJECT_PATH=%PROJECT_PATH:"=%
if "%PROJECT_PATH:~-1%"=="\" set PROJECT_PATH=%PROJECT_PATH:~0,-1%

REM 检查目录是否存在
if not exist "%PROJECT_PATH%" (
    echo   目录 "%PROJECT_PATH%" 不存在，请重新输入。
    echo.
    goto GET_PROJECT_PATH
)

REM 检查是否是 UE 项目 (查找 .uproject 文件)
dir /b "%PROJECT_PATH%\*.uproject" >nul 2>&1
if errorlevel 1 (
    echo.
    echo   ⚠ 警告: 未在指定目录找到 .uproject 文件。
    echo   这似乎不是一个 UE 项目目录。
    set /p CONFIRM="  是否继续安装? (Y/N): "
    if /i not "%CONFIRM%"=="Y" goto GET_PROJECT_PATH
)

REM ── 创建 Plugins 目录 ─────────────────────────────
set PLUGINS_DIR=%PROJECT_PATH%\Plugins
if not exist "%PLUGINS_DIR%" (
    mkdir "%PLUGINS_DIR%"
    echo   已创建 Plugins 目录: %PLUGINS_DIR%
)

REM ── 检查是否已安装 ────────────────────────────────
set INSTALL_DIR=%PLUGINS_DIR%\PBRStudio
if exist "%INSTALL_DIR%" (
    echo.
    echo   ⚠ PBRStudio 已存在于目标目录。
    set /p OVERWRITE="  是否覆盖安装? (Y/N): "
    if /i not "%OVERWRITE%"=="Y" (
        echo   安装已取消。
        pause
        exit /b 0
    )
    echo   正在删除旧版本...
    rmdir /s /q "%INSTALL_DIR%"
)

REM ── 复制插件文件 ──────────────────────────────────
echo.
echo   正在安装 PBRStudio 到 %INSTALL_DIR% ...

REM 获取当前脚本所在目录 (插件根目录)
set SCRIPT_DIR=%~dp0
set SCRIPT_DIR=%SCRIPT_DIR:~0,-1%

echo   复制文件中...
xcopy /E /I /Y /Q "%SCRIPT_DIR%" "%INSTALL_DIR%" 2>nul

REM 删除不需要的安装脚本本身
del /q "%INSTALL_DIR%\安装_UE插件.bat" 2>nul

REM ── 验证安装 ──────────────────────────────────────
if exist "%INSTALL_DIR%\PBRStudio.uplugin" (
    echo.
    echo   ╔══════════════════════════════════════════════╗
    echo   ║          ✓ 安装成功!                         ║
    echo   ╚══════════════════════════════════════════════╝
    echo.
    echo   插件已安装到:
    echo     %INSTALL_DIR%
    echo.
    echo   【下一步】
    echo   1. 打开你的 UE 项目
    echo   2. 如果提示插件需要重新编译，点击"是"
    echo      (预编译版本在相同 UE 版本下可直接使用)
    echo   3. 在 UE 菜单栏中找到 "PBR Studio" 工具窗口
    echo.
    echo   【如果遇到编译错误】
    echo   需要安装 Visual Studio 2022 C++ 工具链后重新编译。
    echo.
) else (
    echo.
    echo   ╔══════════════════════════════════════════════╗
    echo   ║          ✗ 安装失败                           ║
    echo   ╚══════════════════════════════════════════════╝
    echo.
    echo   插件文件复制失败，请检查磁盘空间或权限。
    echo.
)

pause

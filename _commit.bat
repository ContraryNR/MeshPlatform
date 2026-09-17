@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion

echo [MeshPlatform] Git Commit Script
echo =================================

echo Setting up Git proxy...
git config --global http.proxy http://127.0.0.1:7890
git config --global https.proxy http://127.0.0.1:7890
echo Proxy configured: http://127.0.0.1:7890

echo.
echo Git status:
git status

echo.
echo [0] Cancel
echo [1] Use timestamp as commit message
echo [2] Input commit message manually
set /p choice="Choose (0/1/2): "

if "%choice%"=="0" (
    echo Cancelled.
    pause
    exit /b 0
) else if "%choice%"=="1" (
    for /f %%I in ('powershell -NoProfile -Command "Get-Date -Format 'yyyy-MM-dd HH:mm:ss'"') do set timestamp=%%I
    set commitMsg=Auto commit: %timestamp%
) else if "%choice%"=="2" (
    set /p commitMsg="Input commit message: "
    if "!commitMsg!"=="" (
        echo Error: commit message cannot be empty!
        pause
        exit /b 1
    )
) else (
    echo Invalid choice!
    pause
    exit /b 1
)

echo.
echo Committing...
git add .
git commit -m "%commitMsg%"

if %errorlevel% equ 0 (
    echo.
    echo Commit succeeded!
) else (
    echo.
    echo Commit failed!
)

pause

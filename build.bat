@echo off
setlocal enabledelayedexpansion
title Build POE2AssetBrowser
cd /d "%~dp0"

:: Close any running instance so the linker can overwrite the .exe (avoids LNK1104).
taskkill /im POE2AssetBrowser.exe /f >nul 2>&1

echo ============================================================
echo  POE2AssetBrowser - build
echo  (native C++/Qt6 - the first build compiles Qt6 via vcpkg,
echo   which is slow: 30-90 min and several GB. Later builds use
echo   rebuild.bat and take seconds.)
echo ============================================================
echo.

:: 1. Make sure MSVC (cl.exe / cmake / ninja) is on PATH; if not, run vcvars64.
where cl >nul 2>&1
if errorlevel 1 (
    echo [1/4] Initializing Visual Studio 2022 build tools...
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "!VSWHERE!" (
        echo   ERROR: Visual Studio 2022 not found.
        echo   Install "Visual Studio 2022" with the "Desktop development with C++" workload.
        pause & exit /b 1
    )
    set "VSPATH="
    for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
    if not defined VSPATH ( echo   ERROR: MSVC C++ tools not found in your VS install. & pause & exit /b 1 )
    call "!VSPATH!\VC\Auxiliary\Build\vcvars64.bat"
    if errorlevel 1 ( echo   ERROR: vcvars64 failed. & pause & exit /b 1 )
) else ( echo [1/4] MSVC already on PATH. )

:: 2. vcpkg.
if not defined VCPKG_ROOT (
    echo   ERROR: VCPKG_ROOT is not set. One-time setup:
    echo       git clone https://github.com/microsoft/vcpkg
    echo       .\vcpkg\bootstrap-vcpkg.bat
    echo       setx VCPKG_ROOT C:\path\to\vcpkg
    echo   ^(then open a NEW terminal and re-run build.bat^)
    pause & exit /b 1
)
if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
    echo   ERROR: VCPKG_ROOT="%VCPKG_ROOT%" is not a vcpkg checkout.
    pause & exit /b 1
)

echo [2/4] Pinning vcpkg baseline...
"%VCPKG_ROOT%\vcpkg.exe" x-update-baseline --add-initial-baseline

echo [3/4] Configuring (the first vcpkg Qt6 build can take a long time)...
echo.
powershell -NoProfile -ExecutionPolicy Bypass -Command "cmake --preset windows-msvc-release 2>&1 | Tee-Object -FilePath '%~dp0build_log.txt'; exit $LASTEXITCODE"
set "RC=%errorlevel%"
if not "%RC%"=="0" (
    powershell -NoProfile -ExecutionPolicy Bypass -Command "$e = Select-String -Path '%~dp0build_log.txt' -Pattern 'CMake Error','error','Failed to find','FAILED' | ForEach-Object { $_.Line } | Select-Object -First 40 ; $e; $e | Out-File -FilePath '%~dp0build_errors.txt' -Encoding utf8"
    echo   CONFIGURE FAILED ^(see build_errors.txt^). & pause & exit /b 1
)

echo [4/4] Building...
echo.
powershell -NoProfile -ExecutionPolicy Bypass -Command "cmake --build --preset release 2>&1 | Tee-Object -FilePath '%~dp0build_log.txt'; exit $LASTEXITCODE"
set "RC=%errorlevel%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$e = Select-String -Path '%~dp0build_log.txt' -Pattern 'error C',': error','error LNK','fatal error','FAILED','ninja: build stopped' | ForEach-Object { $_.Line } | Select-Object -First 40 ; $e; $e | Out-File -FilePath '%~dp0build_errors.txt' -Encoding utf8"
if not "%RC%"=="0" ( echo   BUILD FAILED ^(see build_errors.txt^). & pause & exit /b 1 )

echo.
echo ============================================================
echo  BUILD OK.  Launch with:  run.bat
echo ============================================================
if not defined NOPAUSE pause

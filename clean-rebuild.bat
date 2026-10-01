@echo off
setlocal enabledelayedexpansion
title CLEAN Rebuild POE2AssetBrowser
cd /d "%~dp0"

:: GUARANTEED-CLEAN rebuild: wipes every compiled object first so EVERY source file is recompiled
:: against the CURRENT headers (no stale-object / ABI mismatch). Does NOT rebuild the vcpkg deps.

taskkill /im POE2AssetBrowser.exe /f >nul 2>&1

where cl >nul 2>&1
if errorlevel 1 (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    set "VSPATH="
    for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
    if not defined VSPATH ( echo ERROR: Visual Studio 2022 C++ tools not found. & pause & exit /b 1 )
    call "!VSPATH!\VC\Auxiliary\Build\vcvars64.bat" >nul
)

if not exist "build\release\CMakeCache.txt" (
    echo No build yet - run build.bat first.
    pause & exit /b 1
)

echo [1/2] Wiping all compiled objects (ninja clean)...
cmake --build --preset release --target clean >nul 2>&1

echo [2/2] Full recompile...
echo.
powershell -NoProfile -ExecutionPolicy Bypass -Command "cmake --build --preset release 2>&1 | Tee-Object -FilePath '%~dp0build_log.txt'; exit $LASTEXITCODE"
set "RC=%errorlevel%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$e = Select-String -Path '%~dp0build_log.txt' -Pattern 'error C',': error','error LNK','fatal error','FAILED','ninja: build stopped' | ForEach-Object { $_.Line } | Select-Object -First 40 ; $e; $e | Out-File -FilePath '%~dp0build_errors.txt' -Encoding utf8"
if not "%RC%"=="0" ( echo. & echo BUILD FAILED - errors in build_errors.txt & pause & exit /b 1 )

echo.
echo Clean build OK. Deploying + launching...
call "%~dp0run.bat"

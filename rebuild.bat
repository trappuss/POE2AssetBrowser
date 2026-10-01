@echo off
setlocal enabledelayedexpansion
title Rebuild POE2AssetBrowser (fast)
cd /d "%~dp0"

:: Close any running instance so the linker can overwrite the .exe (avoids LNK1104).
taskkill /im POE2AssetBrowser.exe /f >nul 2>&1

:: Pre-build source checks (verify-src.py): delimiter balance, header-only helpers used without a
:: real #include, printf format/arg mismatches, settings keys written and never read, and
:: classification by a name substring. Non-blocking - a checker false positive never stops a build.
set "PYEXE="
py -3 -c "import sys" >nul 2>&1 && set "PYEXE=py -3"
if not defined PYEXE python -c "import sys" >nul 2>&1 && set "PYEXE=python"
if not defined PYEXE python3 -c "import sys" >nul 2>&1 && set "PYEXE=python3"
if defined PYEXE (
    %PYEXE% "%~dp0verify-src.py" --quiet
    if errorlevel 1 (
        echo.
        echo  ^>^> verify-src found problems ^(listed above^). Building anyway - Ctrl+C to stop.
        echo.
    )
)

where cl >nul 2>&1
if errorlevel 1 (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    set "VSPATH="
    for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
    if not defined VSPATH ( echo ERROR: Visual Studio 2022 C++ tools not found. & pause & exit /b 1 )
    call "!VSPATH!\VC\Auxiliary\Build\vcvars64.bat" >nul
)

if not exist "build\release\CMakeCache.txt" (
    echo No build yet - run build.bat first ^(it does the one-time dependency build^).
    pause & exit /b 1
)

echo Building (LIVE output; the first build of each file may take a moment)...
echo.
powershell -NoProfile -ExecutionPolicy Bypass -Command "cmake --build --preset release 2>&1 | Tee-Object -FilePath '%~dp0build_log.txt'; exit $LASTEXITCODE"
set "RC=%errorlevel%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$e = Select-String -Path '%~dp0build_log.txt' -Pattern 'error C',': error','error LNK','fatal error','FAILED','ninja: build stopped' | ForEach-Object { $_.Line } | Select-Object -First 40 ; $e; $e | Out-File -FilePath '%~dp0build_errors.txt' -Encoding utf8"
if not "%RC%"=="0" (
    echo.
    echo BUILD FAILED - errors in build_errors.txt
    pause & exit /b 1
)

echo.
echo Done. Deploying + launching...
call "%~dp0run.bat"

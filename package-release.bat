@echo off
setlocal enabledelayedexpansion
title Package POE2AssetBrowser release
cd /d "%~dp0"

:: Assembles a standalone Windows package (exe + Qt runtime DLLs + plugins) and zips it into dist\,
:: ready for publish.bat to attach to a GitHub release. Run build.bat first, then this, then publish.bat.
:: The DLL/plugin set mirrors exactly what run.bat deploys, so the zip runs the same as a local build.

set "SRC=build\release"
set "EXE=%SRC%\POE2AssetBrowser.exe"
if not exist "%EXE%" (
    echo   POE2AssetBrowser.exe not found at %EXE%
    echo   Build it first:  build.bat
    pause & exit /b 1
)

:: Version from the VERSION file (falls back to 0.0.0).
set "VER=0.0.0"
if exist VERSION set /p VER=<VERSION

set "OUT=dist"
set "STAGE=%OUT%\POE2AssetBrowser"
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%" >nul 2>&1

echo [1/3] Copying the executable...
copy /y "%EXE%" "%STAGE%\" >nul

echo [2/3] Copying the Qt runtime (DLLs + plugins)...
set "VINST=%SRC%\vcpkg_installed\x64-windows"
copy /y "%VINST%\bin\*.dll" "%STAGE%\" >nul 2>&1
for %%P in (platforms imageformats styles iconengines) do (
    if not exist "%STAGE%\%%P" mkdir "%STAGE%\%%P" >nul 2>&1
    copy /y "%VINST%\Qt6\plugins\%%P\*.dll" "%STAGE%\%%P\" >nul 2>&1
)
if not exist "%STAGE%\Qt6Core.dll" (
    echo   WARNING: no Qt DLLs were copied from %VINST%.
    echo   The zip may not run on a machine without Qt. Check that run.bat works locally first.
)

> "%STAGE%\README.txt" echo POE2AssetBrowser v%VER%
>> "%STAGE%\README.txt" echo.
>> "%STAGE%\README.txt" echo Run POE2AssetBrowser.exe, then File - Set Path of Exile 2 folder and point it
>> "%STAGE%\README.txt" echo at your install (the folder that contains Bundles2). Docs: the project wiki.

echo [3/3] Zipping...
set "ZIP=%OUT%\POE2AssetBrowser-v%VER%-win64.zip"
if exist "%ZIP%" del /q "%ZIP%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path '%STAGE%\*' -DestinationPath '%ZIP%' -Force"
if not exist "%ZIP%" ( echo   ZIP FAILED. & pause & exit /b 1 )

echo.
echo ============================================================
echo  Packaged:  %ZIP%
echo  Next:      publish.bat   (attaches everything in dist\ to a v%VER% release)
echo ============================================================
if not defined NOPAUSE pause

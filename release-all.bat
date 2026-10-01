@echo off
setlocal
title Release POE2AssetBrowser  (build -> package -> publish)
cd /d "%~dp0"

echo ============================================================
echo  POE2AssetBrowser - full release, in one go
echo    1) build the exe
echo    2) package the win64 zip into dist\
echo    3) publish main + a GitHub release + the wiki
echo.
echo  The first run may install Git and the GitHub CLI and ask
echo  you to sign in to GitHub. If it says to run this again,
echo  just double-click it once more - step 1 and 2 are quick
echo  the second time.
echo ============================================================
echo.

:: Let build.bat / package-release.bat skip their "press any key" on success
:: so the three steps run straight through. (Failures still pause so you see them.)
set "NOPAUSE=1"

echo [1/3] Building...
call "%~dp0build.bat"
if errorlevel 1 (
    echo.
    echo   BUILD FAILED - see build_errors.txt. Stopping before publish.
    set "NOPAUSE="
    pause
    exit /b 1
)

echo.
echo [2/3] Packaging the win64 zip...
call "%~dp0package-release.bat"
if errorlevel 1 (
    echo.
    echo   PACKAGING FAILED. Stopping before publish.
    set "NOPAUSE="
    pause
    exit /b 1
)

echo.
echo [3/3] Publishing to GitHub...
:: publish.bat is interactive (sign-in, prompts) - let it behave normally.
set "NOPAUSE="
call "%~dp0publish.bat"

echo.
echo ============================================================
echo  Finished. If publish asked you to sign in or re-run,
echo  just double-click this file again.
echo ============================================================
pause

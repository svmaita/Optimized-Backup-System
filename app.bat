@echo off
setlocal
cd /d "%~dp0"

if not exist "C:\Program Files\CMake\bin\cmake.exe" (
    echo.
    echo CMake Is Not Available On PATH.
    echo Install CMake Or Add It To PATH, Then Run This File Again.
    echo.
    pause
    exit /b 1
)

"C:\Program Files\CMake\bin\cmake.exe" -S . -B build-msvc -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo.
    echo Failed To Configure The Project With CMake.
    echo Check That Visual Studio Build Tools Are Installed.
    echo.
    pause
    exit /b 1
)

"C:\Program Files\CMake\bin\cmake.exe" --build build-msvc --config Release
if errorlevel 1 (
    echo.
    echo The Project Build Failed.
    echo Check That Visual Studio Build Tools Are Installed.
    echo.
    pause
    exit /b 1
)

if exist ".\build-msvc\Release\OptimizedBackup.exe" (
    copy /Y ".\build-msvc\Release\OptimizedBackup.exe" ".\OptimizedBackup.exe" >nul
    start "" ".\build-msvc\Release\OptimizedBackup.exe"
    exit /b 0
)

echo.
echo Build Completed, But OptimizedBackup.exe Was Not Created.
echo.
pause
exit /b 1
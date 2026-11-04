@echo off
setlocal
set "ROOT=%~dp0.."
for %%I in ("%ROOT%") do set "ROOT=%%~fI"
set "BUILD_DIR=%ROOT%\out\installer-build\bin\Release"

where cmake >nul 2>nul || goto :missing_cmake
where go >nul 2>nul || goto :missing_go
where npm >nul 2>nul || goto :missing_npm
where cargo >nul 2>nul || goto :missing_cargo
if not defined VCINSTALLDIR (
    echo Run this script from an x64 Visual Studio Developer Command Prompt.
    exit /b 1
)

set "QT_BIN=C:\Qt\6.11.2\msvc2022_64\bin"
if not exist "%QT_BIN%\windeployqt.exe" (
    echo Qt windeployqt was not found at "%QT_BIN%".
    echo Update QT_BIN in this script to match your Qt MSVC installation.
    exit /b 1
)

set "ISCC="
for %%I in (ISCC.exe) do set "ISCC=%%~$PATH:I"
if not defined ISCC if exist "%ProgramFiles(x86)%\Inno Setup 7\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup 7\ISCC.exe"
if not defined ISCC if exist "%ProgramFiles%\Inno Setup 7\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 7\ISCC.exe"
if not defined ISCC if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not defined ISCC (
    echo Inno Setup compiler ISCC.exe was not found.
    echo Install Inno Setup 6 or 7, or add ISCC.exe to PATH, then run this script again.
    exit /b 1
)

cmake -S "%ROOT%" -B "%ROOT%\out\installer-build" -A x64
if errorlevel 1 exit /b %errorlevel%
cmake --build "%ROOT%\out\installer-build" --config Release
if errorlevel 1 exit /b %errorlevel%

"%QT_BIN%\windeployqt.exe" --release --compiler-runtime --no-translations "%BUILD_DIR%\JustInTime.exe"
if errorlevel 1 exit /b %errorlevel%
"%QT_BIN%\windeployqt.exe" --release --compiler-runtime --no-translations "%BUILD_DIR%\JustInTimeDashboard.exe"
if errorlevel 1 exit /b %errorlevel%

if not exist "%BUILD_DIR%\dashboard-go" mkdir "%BUILD_DIR%\dashboard-go"
pushd "%ROOT%\DashboardWebApp"
go build -trimpath -ldflags="-s -w" -o "%BUILD_DIR%\dashboard-go\dashboard.exe" .\cmd\dashboard
if errorlevel 1 (
    popd
    exit /b 1
)
popd

pushd "%ROOT%\JITDashboard"
if not exist node_modules call npm ci
if errorlevel 1 (
    popd
    exit /b 1
)
call npm run tauri -- build --no-bundle
if errorlevel 1 (
    popd
    exit /b 1
)
popd

pushd "%ROOT%\installer"
"%ISCC%" JustInTime.iss
set "RESULT=%errorlevel%"
popd
exit /b %RESULT%

:missing_cmake
echo CMake was not found in PATH.
exit /b 1
:missing_go
echo Go was not found in PATH.
exit /b 1
:missing_npm
echo npm was not found in PATH.
exit /b 1
:missing_cargo
echo Rust Cargo was not found in PATH.
exit /b 1
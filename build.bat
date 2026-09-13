@echo off
rem Builds Amalgam.sln. Optional arg: configuration name (default Release).
rem The project targets the v143 (VS2022) toolset, but this machine only has
rem the v145 toolset from VS 18 Build Tools, so we override it here.

setlocal

rem Locate MSBuild: prefer VS 18 Build Tools, otherwise ask vswhere
set "MSBUILD=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe"
if not exist "%MSBUILD%" (
    set "MSBUILD="
    for /f "usebackq tokens=*" %%i in (`"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\amd64\MSBuild.exe" 2^>nul`) do set "MSBUILD=%%i"
)

if not defined MSBUILD (
    echo [build.bat] MSBuild not found - install VS Build Tools with the C++ workload.
    exit /b 1
)

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

echo [build.bat] Building Amalgam %CONFIG% x64 with toolset v145...
"%MSBUILD%" "%~dp0Amalgam.sln" -p:Configuration=%CONFIG% -p:Platform=x64 -p:PlatformToolset=v145 -m -v:m -nologo
if errorlevel 1 (
    echo [build.bat] Build FAILED.
    exit /b 1
)
echo [build.bat] Build OK.

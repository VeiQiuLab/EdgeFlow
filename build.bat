@echo off
setlocal

rem Build EdgeFlow.exe into build/Release
rem Requires Visual Studio 2022 Build Tools (MSVC) and CMake 3.20+

set ROOT=%~dp0
set BUILD=%ROOT%build

if not exist "%BUILD%" mkdir "%BUILD%"

cmake -S "%ROOT%" -B "%BUILD%" -G "Visual Studio 17 2022" -A x64
if errorlevel 1 goto fail

cmake --build "%BUILD%" --config Release
if errorlevel 1 goto fail

echo.
echo Build OK: %BUILD%\Release\EdgeFlow.exe
exit /b 0

:fail
echo.
echo Build FAILED.
exit /b 1

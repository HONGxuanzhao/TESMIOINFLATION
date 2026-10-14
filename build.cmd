@echo off
setlocal
cd /d "%~dp0"
where cl >nul 2>nul
if not errorlevel 1 goto ready
if defined VSINSTALLDIR call "%VSINSTALLDIR%\VC\Auxiliary\Build\vcvars64.bat"
where cl >nul 2>nul
if not errorlevel 1 goto ready
if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
where cl >nul 2>nul
if not errorlevel 1 goto ready
echo Run this script in an x64 Native Tools Command Prompt with the Windows SDK installed.
exit /b 1
:ready
if not exist build mkdir build
cl /nologo /utf-8 /LD /MT /O2 /EHsc /W4 /D_CRT_SECURE_NO_WARNINGS /I"src" "source\TESMIOINFLATION.cpp" /Fe"build\TESMIOINFLATION.dll" /Fo"build\TESMIOINFLATION.obj" /link /INCREMENTAL:NO /IMPLIB:"build\TESMIOINFLATION.lib" /OPT:REF user32.lib
exit /b %errorlevel%

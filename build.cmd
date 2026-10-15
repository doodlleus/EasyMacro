@echo off
rem Builds build\EasyMacro.exe with the Visual Studio C++ tools (no C runtime, no dependencies).
setlocal
cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
rem Newest install with a complete C++ toolset (-latest alone can pick e.g. SSMS, which has none).
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -products * -sort -find VC\Auxiliary\Build\vcvarsall.bat`) do (
    if not defined VCVARS set "VCVARS=%%i"
)
if not defined VCVARS (
    echo Visual Studio with the C++ build tools was not found.
    exit /b 1
)
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%VCVARS%" x64 >nul || exit /b 1

if not exist build mkdir build
rc /nologo /fo build\easymacro.res res\easymacro.rc || exit /b 1
cl /nologo /c /std:c++17 /W4 /O1 /Oi /GS- /Gy /GR- /EHs-c- /Zl /fp:fast /DNDEBUG /DUNICODE /D_UNICODE ^
   /Fobuild\ src\main.cpp src\ui.cpp src\gfx.cpp src\macro.cpp src\crt.cpp || exit /b 1
link /nologo /NODEFAULTLIB /ENTRY:EntryPoint /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /MANIFEST:NO ^
     /OUT:build\EasyMacro.exe build\main.obj build\ui.obj build\gfx.obj build\macro.obj build\crt.obj build\easymacro.res ^
     kernel32.lib user32.lib gdi32.lib shell32.lib advapi32.lib dwmapi.lib || exit /b 1

echo Built build\EasyMacro.exe

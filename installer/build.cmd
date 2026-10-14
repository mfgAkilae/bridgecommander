@echo off
rem Builds the installer out\BC_MP_fix.exe (32-bit). Run ..\build.cmd first: the installer embeds
rem out\dinput.dll, scripts\bcmpfix_mp.py, release\bcmpfix.ini and release\README.md.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist ..\out\dinput.dll (
    echo out\dinput.dll missing - run build.cmd first
    exit /b 1
)
if not exist ..\obj mkdir ..\obj
rc /nologo /fo ..\obj\setup.res setup.rc || exit /b 1
cl /nologo /O1 /W3 /MT /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE /Fo..\obj\ setup.c ..\obj\setup.res ^
   /Fe..\out\BC_MP_fix.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:NO ^
   user32.lib shell32.lib ole32.lib comctl32.lib advapi32.lib uuid.lib || exit /b 1
echo BUILD OK

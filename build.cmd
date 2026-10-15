@echo off
rem Builds bcmpfix as dinput.dll (32-bit) into bcmpfix\out
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist out mkdir out
if not exist obj mkdir obj
cl /nologo /O2 /W3 /MT /GS- /Zi /D_CRT_SECURE_NO_WARNINGS /D_WINSOCK_DEPRECATED_NO_WARNINGS /Foobj\ /Fdobj\ ^
   src\main.c src\util.c src\input.c src\net.c src\netfix.c src\pyext.c src\torpedo.c src\lead.c src\relay.c src\scripterr.c src\orient.c src\velo.c src\join.c src\phaser.c ^
   /LD /Feout\dinput.dll /link /DEF:src\dinput.def /PDB:out\dinput.pdb /DEBUG /OPT:REF ^
   ws2_32.lib winmm.lib user32.lib kernel32.lib || exit /b 1
echo BUILD OK

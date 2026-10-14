@echo off
rem Usage: build32.cmd <cl args...>   (runs cl.exe in an x86 MSVC environment)
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
cl %*

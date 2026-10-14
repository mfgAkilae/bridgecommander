@echo off
rem Usage: asm.cmd <outfile> <hexaddr> [<hexaddr> ...]  - dumps disassembly of containing functions
rem Needs GHIDRA_HOME (Ghidra install), JAVA_HOME (JDK 21) and BC_RE (folder with ghidra_proj\ and the
rem project stbc11 that holds stbc11.exe, a copy of stbc.exe 1.1).
set PATH=%JAVA_HOME%\bin;%PATH%
call "%GHIDRA_HOME%\support\analyzeHeadless.bat" %BC_RE%\ghidra_proj stbc11 -process stbc11.exe -noanalysis -readOnly -scriptPath "%~dp0ghidra" -postScript DumpAsm.java %* > %BC_RE%\asm_last.log 2>&1

@echo off
set "scriptdir=%~dp0"
set "filename=%scriptdir%\build"
set "exename=%filename%.exe"
clang -g -Wall -Wextra %filename%.c -o %exename% -Wl,-incremental:no
%exename% %*
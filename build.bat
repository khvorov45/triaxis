@echo off
set "scriptdir=%~dp0"
clang -g -Wall -Wextra %scriptdir%\triaxis.c -o %scriptdir%\triaxis.exe -luser32 -lgdi32 -lWinmm.lib -Wl,-incremental:no
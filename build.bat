@echo off
set "scriptdir=%~dp0"
clang -g -Wall -Wextra -march=native %scriptdir%\triaxis.c -o %scriptdir%\triaxis.exe -Wl,-incremental:no
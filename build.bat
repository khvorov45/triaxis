@echo off
set "scriptdir=%~dp0"
clang -g -Wall -Wextra %scriptdir%\triaxis.c -o %scriptdir%\triaxis.exe -Wl,-incremental:no
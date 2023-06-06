@echo off
set "scriptdir=%~dp0"
clang -g %scriptdir%\tracy\public\TracyClient.cpp -DTRACY_ENABLE -c -o %scriptdir%\TracyClient.obj
clang -g -Wall -Wextra -march=native -DTRACY_ENABLE %scriptdir%\triaxis.c %scriptdir%\TracyClient.obj -o %scriptdir%\triaxis.exe -Wl,-incremental:no
@echo off
if exist shell.bat call shell.bat
clang -g -Wall -Wextra build.c -o build.exe && build.exe %*
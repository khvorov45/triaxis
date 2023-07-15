@echo off
clang -g -Wall -Wextra build.c -o build.exe && build.exe %*
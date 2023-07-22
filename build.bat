@echo off

if not exist build mkdir build
del /q build\*

start /b clang H:/Projects/triaxis/code/triaxis.c ^
    -march=native -Wall -Wextra -fno-caret-diagnostics -Wl,-incremental:no -g ^
    -DTRIAXIS_debuginfo -DTRIAXIS_profile -DTRIAXIS_asserts -DTRIAXIS_tests -DTRIAXIS_bench ^
    -o H:/Projects/triaxis/build/triaxis_debuginfo_profile_asserts_tests_bench.exe

start /b clang H:/Projects/triaxis/code/triaxis.c ^
    -march=native -Wall -Wextra -fno-caret-diagnostics -Wl,-incremental:no -g -O3 ^
    -DTRIAXIS_debuginfo -DTRIAXIS_optimise -DTRIAXIS_profile -DTRIAXIS_bench ^
    -o H:/Projects/triaxis/build/triaxis_debuginfo_optimise_profile_bench.exe

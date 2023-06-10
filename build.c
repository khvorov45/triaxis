#include "cbuild.h"

#define assert(x) prb_assert(x)
#define STR(x) prb_STR(x)
#define LIT(x) prb_LIT(x)
#define function static

typedef int32_t  i32;
typedef uint64_t u64;
typedef intptr_t isize;

typedef prb_Arena Arena;
typedef prb_Str   Str;

function void
execCmd(Arena* arena, Str cmd) {
    prb_writelnToStdout(arena, cmd);
    prb_Process proc = prb_createProcess(cmd, (prb_ProcessSpec) {});
    assert(prb_launchProcesses(arena, &proc, 1, prb_Background_No));
}

int
main() {
    prb_TimeStart scriptStart = prb_timeStart();

    Arena  arena_ = prb_createArenaFromVmem(1 * prb_GIGABYTE);
    Arena* arena = &arena_;

    if (false) {
        for (isize ind = 0; ind < 64; ind++) {
            u64 pow2 = 1ULL << (u64)ind;
            u64 mask = pow2 - 1;
            prb_writeToStdout(prb_fmt(arena, "%016llx\n", mask));
        }
    }

    Str*           args = prb_getCmdArgs(arena);
    i32            argsIndex = 1;
    prb_Arena      argsDefinesBuilderArena = prb_createArenaFromArena(arena, 1 * prb_MEGABYTE);
    prb_GrowingStr argsDefinesBuilder = prb_beginStr(&argsDefinesBuilderArena);
    prb_GrowingStr nameBuilder = prb_beginStr(arena);
    prb_addStrSegment(&nameBuilder, "triaxis");

#define boolarg(name) \
    assert(arrlen(args) > argsIndex); \
    assert(prb_strStartsWith(args[argsIndex], STR(#name "="))); \
    bool name = prb_strEndsWith(args[argsIndex++], STR("yes")); \
    if (name) { \
        prb_addStrSegment(&nameBuilder, "_" #name); \
        prb_addStrSegment(&argsDefinesBuilder, " -DTRIAXIS_" #name); \
    } \
    do { \
    } while (0)

    boolarg(debuginfo);
    boolarg(optimise);
    boolarg(profile);
    boolarg(asserts);
    boolarg(tests);
    boolarg(bench);
#undef boolarg

    Str outputName = prb_endStr(&nameBuilder);
    Str argsDefines = prb_endStr(&argsDefinesBuilder);

    Str rootDir = prb_getParentDir(arena, STR(__FILE__));
    Str codeDir = prb_pathJoin(arena, rootDir, STR("code"));
    Str buildDir = prb_pathJoin(arena, rootDir, STR("build"));
    Str tracyDir = prb_pathJoin(arena, rootDir, STR("tracy"));

    prb_createDirIfNotExists(arena, buildDir);
    // prb_clearDir(arena, buildDir);

    Str tracyIncludeFlag = prb_fmt(arena, "-I%.*s/public/tracy", LIT(tracyDir));
    Str tracyClientObj = prb_pathJoin(arena, buildDir, STR("TracyClient.obj"));
    if (!prb_isFile(arena, tracyClientObj)) {
        Str src = prb_pathJoin(arena, tracyDir, STR("public/TracyClient.cpp"));
        Str cmd = prb_fmt(arena, "clang %.*s -march=native -O3 -DTRACY_ENABLE -Wno-format -c %.*s -o %.*s", LIT(tracyIncludeFlag), LIT(src), LIT(tracyClientObj));
        execCmd(arena, cmd);
    }

    {
        Str flags = {};
        {
            prb_GrowingStr builder = prb_beginStr(arena);
            prb_addStrSegment(&builder, "%.*s%.*s", LIT(tracyIncludeFlag), LIT(argsDefines));
            if (debuginfo) {
                prb_addStrSegment(&builder, " -g");
            }
            if (optimise) {
                // NOTE(khvorov) -fno-builtin is to prevent generating calls to memset and such
                prb_addStrSegment(&builder, " -O3 -fno-builtin");
            }
            if (profile) {
                prb_addStrSegment(&builder, "-DTRACY_ENABLE");
            }
            flags = prb_endStr(&builder);
        }

        Str mainObj = {};
        {
            Str src = prb_pathJoin(arena, codeDir, STR("triaxis.c"));
            Str name = prb_fmt(arena, "%.*s.obj", LIT(outputName));
            mainObj = prb_pathJoin(arena, buildDir, name);
            Str cmd = prb_fmt(arena, "clang %.*s -Wall -Wextra -c -march=native %.*s -o %.*s", LIT(flags), LIT(src), LIT(mainObj));
            execCmd(arena, cmd);
        }

        {
            Str src = {};
            {
                prb_GrowingStr builder = prb_beginStr(arena);
                prb_addStrSegment(&builder, "%.*s", LIT(mainObj));
                if (profile) {
                    prb_addStrSegment(&builder, " %.*s", LIT(tracyClientObj));
                }
                src = prb_endStr(&builder);
            }

            Str exe = prb_replaceExt(arena, mainObj, STR("exe"));
            Str cmd = prb_fmt(arena, "clang %.*s -o %.*s -Wl,-incremental:no", LIT(src), LIT(exe));
            execCmd(arena, cmd);
        }
        
    }

    prb_writeToStdout(prb_fmt(arena, "total: %.2fms\n", prb_getMsFrom(scriptStart)));
    return 0;
}

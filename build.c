#include "cbuild.h"

#define assert(x) prb_assert(x)
#define STR(x) prb_STR(x)
#define LIT(x) prb_LIT(x)
#define function static

typedef int32_t  i32;
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

    bool debugInfo = false;
    bool optimise = false;
    bool profile = false;
    {
        prb_Str* args = prb_getCmdArgs(arena);
        prb_assert(arrlen(args) == 4);

        prb_assert(prb_strStartsWith(args[1], STR("debuginfo=")));
        debugInfo = prb_strEndsWith(args[1], STR("yes"));

        prb_assert(prb_strStartsWith(args[2], STR("optimise=")));
        optimise = prb_strEndsWith(args[2], STR("yes"));

        prb_assert(prb_strStartsWith(args[3], STR("profile=")));
        profile = prb_strEndsWith(args[3], STR("yes"));
    }

    Str rootDir = prb_getParentDir(arena, STR(__FILE__));
    Str codeDir = prb_pathJoin(arena, rootDir, STR("code"));
    Str buildDir = prb_pathJoin(arena, rootDir, STR("build"));
    Str tracyDir = prb_pathJoin(arena, rootDir, STR("tracy"));

    prb_createDirIfNotExists(arena, buildDir);

    Str alwaysFlags = prb_fmt(arena, "-march=native -I%.*s/public/tracy", LIT(tracyDir));
    Str optFlags = STR("-O3");
    Str debugInfoFlags = STR("-g");

    Str tracyEnableFlag = STR("-DTRACY_ENABLE");
    Str tracyClientObj = prb_pathJoin(arena, buildDir, STR("TracyClient.obj"));
    if (!prb_isFile(arena, tracyClientObj)) {
        Str src = prb_pathJoin(arena, tracyDir, STR("public/TracyClient.cpp"));
        Str cmd = prb_fmt(arena, "clang %.*s %.*s %.*s %.*s -c -o %.*s", LIT(alwaysFlags), LIT(optFlags), LIT(tracyEnableFlag), LIT(src), LIT(tracyClientObj));
        execCmd(arena, cmd);
    }

    {
        Str mainSrc = prb_pathJoin(arena, codeDir, STR("triaxis.c"));
        Str outName = STR("triaxis");

        Str* src = 0;
        arrput(src, mainSrc);
        Str* flags = 0;
        arrput(flags, alwaysFlags);
        if (debugInfo) {
            arrput(flags, debugInfoFlags);
            outName = prb_fmt(arena, "%.*s_debuginfo", LIT(outName));
        }
        if (optimise) {
            arrput(flags, optFlags);
            outName = prb_fmt(arena, "%.*s_opt", LIT(outName));
        }
        if (profile) {
            arrput(flags, tracyEnableFlag);
            arrput(src, tracyClientObj);
            outName = prb_fmt(arena, "%.*s_profile", LIT(outName));
        }
        Str flagsStr = prb_stringsJoin(arena, flags, arrlen(flags), STR(" "));
        outName = prb_fmt(arena, "%.*s.exe", LIT(outName));
        Str out = prb_pathJoin(arena, buildDir, outName);
        Str srcStr = prb_stringsJoin(arena, src, arrlen(src), prb_STR(" "));

        Str cmd = prb_fmt(arena, "clang %.*s -Wall -Wextra %.*s -o %.*s -Wl,-incremental:no", LIT(flagsStr), LIT(srcStr), LIT(out));
        execCmd(arena, cmd);
    }

    prb_writeToStdout(prb_fmt(arena, "total: %.2fms\n", prb_getMsFrom(scriptStart)));
    return 0;
}

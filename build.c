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

typedef struct Opt {
    bool debuginfo;
    bool optimise;
    bool profile;
    bool asserts;
    bool tests;
    bool bench;
} Opt;

Str globalRootDir = {};
Str globalCodeDir = {};
Str globalBuildDir = {};
Str globalTracyDir = {};

function void
execCmd(Arena* arena, Str cmd) {
    prb_writelnToStdout(arena, cmd);
    prb_Process proc = prb_createProcess(cmd, (prb_ProcessSpec) {});
    assert(prb_launchProcesses(arena, &proc, 1, prb_Background_No));
}

function void
compile(Arena* arena, Opt opt) {
    prb_Arena      argsDefinesBuilderArena = prb_createArenaFromArena(arena, 1 * prb_MEGABYTE);
    prb_GrowingStr argsDefinesBuilder = prb_beginStr(&argsDefinesBuilderArena);
    prb_GrowingStr nameBuilder = prb_beginStr(arena);

#define boolarg(name) \
    do { \
        if (opt.name) { \
            prb_addStrSegment(&nameBuilder, "_" #name); \
            prb_addStrSegment(&argsDefinesBuilder, " -DTRIAXIS_" #name); \
        } \
    } while (0)

    boolarg(debuginfo);
    boolarg(optimise);
    boolarg(profile);
    boolarg(asserts);
    boolarg(tests);
    boolarg(bench);
#undef boolarg

    Str outputSuffix = prb_endStr(&nameBuilder);
    Str argsDefines = prb_endStr(&argsDefinesBuilder);

    Str tracyIncludeFlag = prb_fmt(arena, "-I%.*s/public/tracy", LIT(globalTracyDir));
    Str tracyClientObj = prb_pathJoin(arena, globalBuildDir, STR("TracyClient.obj"));
    if (!prb_isFile(arena, tracyClientObj)) {
        Str src = prb_pathJoin(arena, globalTracyDir, STR("public/TracyClient.cpp"));
        Str cmd = prb_fmt(arena, "clang %.*s -march=native -O3 -DTRACY_ENABLE -Wno-format -c %.*s -o %.*s", LIT(tracyIncludeFlag), LIT(src), LIT(tracyClientObj));
        execCmd(arena, cmd);
    }

    {
        Str flags = {};
        {
            prb_GrowingStr builder = prb_beginStr(arena);
            prb_addStrSegment(&builder, "%.*s%.*s", LIT(tracyIncludeFlag), LIT(argsDefines));
            if (opt.debuginfo) {
                prb_addStrSegment(&builder, " -g");
            }
            if (opt.optimise) {
                // NOTE(khvorov) -fno-builtin is to prevent generating calls to memset and such
                prb_addStrSegment(&builder, " -O3 -fno-builtin");
            }
            if (opt.profile) {
                prb_addStrSegment(&builder, " -DTRACY_ENABLE");
            }
            flags = prb_endStr(&builder);
        }

        Str mainPath = prb_pathJoin(arena, globalCodeDir, STR("triaxis_windows.cpp"));

        {
            Str src = {};
            {
                prb_GrowingStr builder = prb_beginStr(arena);
                prb_addStrSegment(&builder, "%.*s", LIT(mainPath));
                if (opt.profile) {
                    prb_addStrSegment(&builder, " %.*s", LIT(tracyClientObj));
                }
                src = prb_endStr(&builder);
            }

            Str exe = {};
            {
                Str codeFileName = prb_getLastEntryInPath(mainPath);
                Str nameNoExt = {};
                {
                    if (prb_strEndsWith(codeFileName, STR(".c"))) {
                        nameNoExt = prb_strSlice(codeFileName, 0, codeFileName.len - 2);
                    } else if (prb_strEndsWith(codeFileName, STR(".cpp"))) {
                        nameNoExt = prb_strSlice(codeFileName, 0, codeFileName.len - 4);
                    } else {
                        assert(!"unexpected source");
                    }
                }
                Str exeFileName = prb_fmt(arena, "%.*s%.*s.exe", LIT(nameNoExt), LIT(outputSuffix));
                exe = prb_pathJoin(arena, globalBuildDir, exeFileName);
            }

            Str cmd = prb_fmt(arena, "clang -march=native -Wall -Wextra -fno-caret-diagnostics %.*s %.*s -o %.*s -Wl,-incremental:no", LIT(flags), LIT(src), LIT(exe));
            prb_writelnToStdout(arena, cmd);
            prb_Process proc = prb_createProcess(cmd, (prb_ProcessSpec) {});
            prb_Status  result = prb_launchProcesses(arena, &proc, 1, prb_Background_No);
            assert(result);
        }
    }
}

int
main() {
    prb_TimeStart scriptStart = prb_timeStart();

    Arena  arena_ = prb_createArenaFromVmem(1 * prb_GIGABYTE);
    Arena* arena = &arena_;

    globalRootDir = prb_getParentDir(arena, STR(__FILE__));
    globalCodeDir = prb_pathJoin(arena, globalRootDir, STR("code"));
    globalBuildDir = prb_pathJoin(arena, globalRootDir, STR("build"));
    globalTracyDir = prb_pathJoin(arena, globalRootDir, STR("tracy"));

    prb_createDirIfNotExists(arena, globalBuildDir);
    // prb_clearDir(arena, globalBuildDir);

    // NOTE(khvorov) Codegen
    {
        prb_GrowingStr builder = prb_beginStr(arena);
        prb_addStrSegment(&builder, "// generated by build.c\n// do not edit by hand\n\n");

        prb_addStrSegment(&builder, "const static __mmask64 globalTailByteMasks512[64] = {\n");
        for (isize ind = 0; ind < 64; ind++) {
            u64 pow2 = 1ULL << (u64)ind;
            u64 mask = pow2 - 1;
            prb_addStrSegment(&builder, "    0x%016llx,\n", mask);
        }
        prb_addStrSegment(&builder, "};\n");

        Str gen = prb_endStr(&builder);
        Str path = prb_pathJoin(arena, globalCodeDir, STR("generated.c"));
        assert(prb_writeEntireFile(arena, path, gen.ptr, gen.len));
    }

    Opt debug = {.debuginfo = true, .asserts = true, .tests = true, .bench = true};
    compile(arena, debug);

    // Opt profile = {.debuginfo = true, .optimise = true, .profile = true, .bench = true};
    // compile(arena, profile);

    prb_writeToStdout(prb_fmt(arena, "total: %.2fms\n", prb_getMsFrom(scriptStart)));
    return 0;
}

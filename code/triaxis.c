#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wc99-designator"
#pragma clang diagnostic ignored "-Wreorder-init-list"

#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>
#include <immintrin.h>
#include <float.h>

#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wsign-compare"
#ifndef TRIAXIS_optimise
// NOTE(khvorov) Debuginfo is busted for force-inlined functions
#define SPALL_FORCEINLINE
#endif
#include "spall.h"

// clang-format off
#define Byte (1)
#define Kilobyte (1024 * Byte)
#define Megabyte (1024 * Kilobyte)
#define Gigabyte (1024 * Megabyte)
#define PI (3.14159)
#define function static
#define globalvar static
#define arrayCount(x) (int)(sizeof(x) / sizeof(x[0]))
#define unused(x) (x) = (x)
#define min(x, y) (((x) < (y)) ? (x) : (y))
#define max(x, y) (((x) > (y)) ? (x) : (y))
#define clamp(val, low, high) (max(low, min(val, high)))
#define absval(x) ((x) >= 0 ? (x) : -(x))
#define arenaAllocArray(arena, type, count) (type*)arenaAlloc(arena, sizeof(type)*count, alignof(type))
#define arenaAllocCap(arena, type, maxbytes, arr) arr.cap = maxbytes / sizeof(type); arr.ptr = arenaAllocArray(arena, type, arr.cap)
#define arrpush(arr, val) (((arr).len < (arr).cap) ? ((arr).ptr[(arr).len] = (val), (arr).len++) : (__debugbreak(), 0))
#define arrget(arr, i) (arr.ptr[((i) < (arr).len ? (i) : (__debugbreak(), 0))])
#define swap(type, v1, v2) do {type temp = (v1); (v1) = (v2); (v2) = temp;} while (0)

#define STR(x) ((Str) {.ptr = x, .len = sizeof(x) - 1})
#define STRARG(x) x, sizeof(x) - 1

#ifdef TRIAXIS_profile
#define timedSectionStart(name) timedSectionStart_(STRARG(name))
#define timedSectionEnd() timedSectionEnd_()
#else
#define timedSectionStart(name)
#define timedSectionEnd()
#endif

#ifdef TRIAXIS_asserts
#define assert(cond) do { if (cond) {} else { __debugbreak(); }} while (0)
#else
#pragma clang diagnostic ignored "-Wunused-value"
#define assert(cond) cond
#endif
// clang-format on

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t  i32;
typedef int64_t  i64;
typedef intptr_t isize;
typedef float    f32;
typedef double   f64;

const static __mmask64 globalTailByteMasks512[64] = {0x0, 0x1, 0x3, 0x7, 0xf, 0x1f, 0x3f, 0x7f, 0xff, 0x1ff, 0x3ff, 0x7ff, 0xfff, 0x1fff, 0x3fff, 0x7fff, 0xffff, 0x1ffff, 0x3ffff, 0x7ffff, 0xfffff, 0x1fffff, 0x3fffff, 0x7fffff, 0xffffff, 0x1ffffff, 0x3ffffff, 0x7ffffff, 0xfffffff, 0x1fffffff, 0x3fffffff, 0x7fffffff, 0x0, 0x1, 0x3, 0x7, 0xf, 0x1f, 0x3f, 0x7f, 0xff, 0x1ff, 0x3ff, 0x7ff, 0xfff, 0x1fff, 0x3fff, 0x7fff, 0xffff, 0x1ffff, 0x3ffff, 0x7ffff, 0xfffff, 0x1fffff, 0x3fffff, 0x7fffff, 0xffffff, 0x1ffffff, 0x3ffffff, 0x7ffffff, 0xfffffff, 0x1fffffff, 0x3fffffff, 0x7fffffff};
const static __mmask16 globalTailDWordMasks512[16] = {0x0, 0x1, 0x3, 0x7, 0xf, 0x1f, 0x3f, 0x7f, 0xff, 0x1ff, 0x3ff, 0x7ff, 0xfff, 0x1fff, 0x3fff, 0x7fff};

//
// SECTION Profile
//

globalvar SpallBuffer globalSpallBuffer;

function void
timedSectionStart_(const char* name, isize nameLen) {
    isize size = sizeof(SpallBeginEvent) + nameLen;
    isize remaining = globalSpallBuffer.length - globalSpallBuffer.head;
    if (size <= remaining) {
        SpallBeginEventMax* event = (SpallBeginEventMax*)(globalSpallBuffer.data + globalSpallBuffer.head);
        event->event.type = SpallEventType_Begin;
        event->event.category = 0;
        event->event.pid = 0;
        event->event.tid = 0;
        event->event.when = __rdtsc();
        event->event.name_length = nameLen;
        event->event.args_length = 0;
        memcpy(event->name_bytes, name, nameLen);

        globalSpallBuffer.head += size;
    }
}

function void
timedSectionEnd_(void) {
    isize size = sizeof(SpallEndEvent);
    isize remaining = globalSpallBuffer.length - globalSpallBuffer.head;
    if (size <= remaining) {
        SpallEndEvent* event = (SpallEndEvent*)(globalSpallBuffer.data + globalSpallBuffer.head);
        event->type = SpallEventType_End;
        event->pid = 0;
        event->tid = 0;
        event->when = __rdtsc();

        globalSpallBuffer.head += size;
    }
}

//
// SECTION Memory
//

function bool
isPowerOf2(isize value) {
    bool result = (value > 0) && ((value & (value - 1)) == 0);
    return result;
}

function isize
getOffsetForAlignment(void* ptr, isize align) {
    assert(isPowerOf2(align));
    isize mask = align - 1;
    isize misalignment = (isize)ptr & mask;
    isize offset = 0;
    if (misalignment > 0) {
        offset = align - misalignment;
    }
    return offset;
}

function bool
memeq(const void* ptr1, const void* ptr2, isize len) {
    bool result = true;
    for (isize ind = 0; ind < len; ind++) {
        if (((u8*)ptr1)[ind] != ((u8*)ptr2)[ind]) {
            result = false;
            break;
        }
    }
    return result;
}

function void
memset_(void* ptr, int val, isize len) {
    assert(val >= 0 && val <= 0xFF);
    isize    wholeCount = len / sizeof(__m512i);
    __m512i* ptr512 = (__m512i*)ptr;
    __m512i  val512 = _mm512_set1_epi8(val);

    for (isize ind = 0; ind < wholeCount; ind++) {
        _mm512_storeu_si512(ptr512 + ind, val512);
    }

    isize     remaining = len - wholeCount * sizeof(__m512i);
    __mmask64 tailMask = globalTailByteMasks512[remaining];
    _mm512_mask_storeu_epi8(ptr512 + wholeCount, tailMask, val512);
}

function void
fltset(f32* ptr, f32 val, isize len) {
    isize   wholeCount = len * sizeof(f32) / sizeof(__m512);
    __m512* ptr512 = (__m512*)ptr;
    __m512  val512 = _mm512_set1_ps(val);

    for (isize ind = 0; ind < wholeCount; ind++) {
        _mm512_storeu_ps(ptr512 + ind, val512);
    }

    isize     remaining = len - wholeCount * sizeof(__m512) / sizeof(f32);
    __mmask16 tailMask = globalTailDWordMasks512[remaining];
    _mm512_mask_storeu_ps(ptr512 + wholeCount, tailMask, val512);
}

function void
zeromem(void* ptr, isize len) {
    memset_(ptr, 0, len);
}

function void
memcpy_(void* dest, const void* src, isize len) {
    assert(((u8*)src < (u8*)dest && (u8*)src + len <= (u8*)dest) || ((u8*)dest < (u8*)src && (u8*)dest + len <= (u8*)src));

    isize wholeTransferCount = len / sizeof(__m512i);

    __m512i* src512 = (__m512i*)src;
    __m512i* dest512 = (__m512i*)dest;
    for (isize ind = 0; ind < wholeTransferCount; ind++) {
        __m512i val = _mm512_loadu_si512(src512 + ind);
        _mm512_storeu_si512(dest512 + ind, val);
    }

    __m512i zeros512 = _mm512_set1_epi8(0);

    isize     remaining = len - wholeTransferCount * sizeof(__m512i);
    __mmask64 tailMask = globalTailByteMasks512[remaining];

    __m512i tail = _mm512_mask_loadu_epi8(zeros512, tailMask, src512 + wholeTransferCount);
    _mm512_mask_storeu_epi8(dest512 + wholeTransferCount, tailMask, tail);
}

typedef struct Arena {
    void* base;
    isize size;
    isize used;
    isize tempCount;
} Arena;

function void*
arenaFreePtr(Arena* arena) {
    assert(arena->used <= arena->size);
    void* result = (u8*)arena->base + arena->used;
    return result;
}

function isize
arenaFreeSize(Arena* arena) {
    assert(arena->used <= arena->size);
    isize result = arena->size - arena->used;
    return result;
}

function void
arenaChangeUsed(Arena* arena, isize size) {
    arena->used += size;
    assert(arena->used <= arena->size);
}

function void
arenaAlign(Arena* arena, isize align) {
    isize offset = getOffsetForAlignment(arenaFreePtr(arena), align);
    arenaChangeUsed(arena, offset);
}

function void*
arenaAlloc(Arena* arena, isize size, isize align) {
    arenaAlign(arena, align);
    void* result = arenaFreePtr(arena);
    arenaChangeUsed(arena, size);
    return result;
}

function Arena
createArenaFromArena(Arena* parent, isize size) {
    Arena arena = {.base = arenaFreePtr(parent), .size = size};
    arenaChangeUsed(parent, size);
    return arena;
}

typedef struct TempMemory {
    Arena* arena;
    isize  usedAtBegin;
    isize  tempCountAtBegin;
} TempMemory;

function TempMemory
beginTempMemory(Arena* arena) {
    TempMemory result = {arena, arena->used, arena->tempCount};
    arena->tempCount += 1;
    return result;
}

function void
endTempMemory(TempMemory temp) {
    assert(temp.arena->used >= temp.usedAtBegin);
    assert(temp.arena->tempCount == temp.tempCountAtBegin + 1);
    temp.arena->used = temp.usedAtBegin;
    temp.arena->tempCount = temp.tempCountAtBegin;
}

//
// SECTION Strings
//

typedef struct Str {
    const char* ptr;
    isize       len;
} Str;

function bool
streq(Str s1, Str s2) {
    bool result = false;
    if (s1.len == s2.len) {
        result = memeq(s1.ptr, s2.ptr, s1.len);
    }
    return result;
}

//
// SECTION String formatting
//

typedef struct StrBuilder {
    union {
        struct {
            char* ptr;
            isize len;
            isize cap;
        };
        Str str;
    };
} StrBuilder;

function void
fmtStr(StrBuilder* builder, Str str) {
    assert(builder->len + str.len <= builder->cap);
    memcpy_(builder->ptr + builder->len, str.ptr, str.len);
    builder->len += str.len;
}

typedef enum FmtAlign {
    FmtAlign_Left,
    FmtAlign_Right,
} FmtAlign;

typedef struct FmtInt {
    isize    chars;
    FmtAlign align;
    bool     disallowOverflow;
    char     padChar;
} FmtInt;

function void
fmtInt(StrBuilder* builder, isize val, FmtInt spec) {
    assert(spec.chars >= 0);

    if (spec.padChar == '\0') {
        spec.padChar = ' ';
    }

    isize valAbs = absval(val);

    isize pow10 = 1;
    isize digitCount = 1 + (val < 0);
    for (isize valCopy = valAbs / 10; valCopy / pow10 > 0; pow10 *= 10, digitCount++) {}

    if (spec.disallowOverflow && spec.chars < digitCount) {
        assert(!"overflow");
    }

    if (spec.align == FmtAlign_Right) {
        while (spec.chars > digitCount) {
            arrpush(*builder, spec.padChar);
            spec.chars -= 1;
        }
    }

    if (val < 0) {
        arrpush(*builder, '-');
        spec.chars -= 1;
    }

    for (isize curVal = valAbs; pow10 > 0 && spec.chars > 0; pow10 /= 10, spec.chars--) {
        isize digit = curVal / pow10;
        assert(digit >= 0 && digit <= 9);
        char digitChar = digit + '0';
        arrpush(*builder, digitChar);
        curVal -= digit * pow10;
    }

    if (spec.align == FmtAlign_Left) {
        while (spec.chars > 0) {
            arrpush(*builder, spec.padChar);
            spec.chars -= 1;
        }
    }
}

typedef struct FmtF32 {
    isize charsLeft;
    isize charsRight;
} FmtF32;

function void
fmtF32(StrBuilder* builder, f32 val, FmtF32 spec) {
    isize maxValLeft = 1;
    for (isize ind = 0; ind < spec.charsLeft - (val < 0); ind++) {
        maxValLeft *= 10;
    }

    isize maxValRight = 1;
    for (isize ind = 0; ind < spec.charsRight; ind++) {
        maxValRight *= 10;
    }

    f32 valAbs = absval(val);

    bool  overflow = false;
    isize whole = (isize)valAbs;
    if (whole > maxValLeft - 1) {
        overflow = true;
        f32 epsilon = 1.0f / (f32)maxValRight;
        valAbs = maxValLeft - epsilon;
        whole = (isize)valAbs;
    }
    assert(whole <= maxValLeft - 1);

    f32   frac = valAbs - (f32)whole;
    isize fracInt = (isize)((frac * (f32)(maxValRight)) + 0.5f);

    fmtInt(builder, val < 0 ? -whole : whole, (FmtInt) {.chars = spec.charsLeft, .align = FmtAlign_Right, .disallowOverflow = true});
    fmtStr(builder, STR("."));
    fmtInt(builder, fracInt, (FmtInt) {.chars = spec.charsRight, .align = FmtAlign_Left, .padChar = '0'});

    if (overflow) {
        builder->ptr[builder->len - 1] = '+';
    }
}

function void
fmtNull(StrBuilder* builder) {
    arrpush(*builder, 0);
}

//
// SECTION Math
//

function bool
feqEplislon(f32 v1, f32 v2, f32 epsilon) {
    bool result = absval(v1 - v2) < epsilon;
    return result;
}

function f32
squareRootf(f32 val) {
    __m128 val128 = _mm_set_ss(val);
    __m128 result128 = _mm_sqrt_ss(val128);
    f32    result = _mm_cvtss_f32(result128);
    return result;
}

function f32
squaref(f32 val) {
    f32 result = val * val;
    return result;
}

function f32
safeRatio1f(f32 v1, f32 v2) {
    f32 result = 1;
    if (v2 != 0) {
        result = v1 / v2;
    }
    return result;
}

// https://en.wikipedia.org/wiki/Bhaskara_I%27s_sine_approximation_formula
function f32
sinef(f32 valTurns) {
    f32 val01 = valTurns - (f32)(i32)valTurns;
    if (val01 < 0) {
        val01 += 1.0f;
    }
    f32 result = 0;
    f32 x = val01;
    f32 f = 5.0f / 64.0f;
    if (val01 < 0.5f) {
        f32 r = x * (0.5f - x);
        result = r / (f - 0.25f * r);
    } else {
        f32 r = (x - 0.5f) * (1.0f - x);
        result = -(r / (f - 0.25f * r));
    }
    return result;
}

function f32
cosinef(f32 valTurns) {
    f32 result = sinef(valTurns + 0.25);
    return result;
}

function f32
tangentf(f32 valTurns) {
    f32 result = safeRatio1f(sinef(valTurns), cosinef(valTurns));
    return result;
}

// Start with
// f(x) = a x / b + sqrt(c + x^2)
// Find a, b and c by solving
// f(1) = pi / 4
// lim(x->infinity)(f(x)) = pi / 2
// f'(0) = 1
// Then divide by 2pi to get turns from radians
// https://math.stackexchange.com/a/2394071
function f32
arctanf(f32 valTurns) {
    f32 x = valTurns;
    f32 a = 0.25f;
    f32 rootc = (1.0f / (4.0f - PI) + PI * 0.25f - 1.0f);
    f32 b = PI * 0.5f - rootc;
    f32 c = squaref(rootc);
    f32 result = a * x / (b + squareRootf(c + squaref(x)));
    return result;
}

function f32
lerpf(f32 start, f32 end, f32 by) {
    f32 result = start + (end - start) * by;
    return result;
}

typedef struct V2f {
    f32 x, y;
} V2f;

typedef union V3f {
    struct {
        f32 x, y, z;
    };
    struct {
        V2f xy;
        f32 z_;
    };
} V3f;

function bool
v2feq(V2f v1, V2f v2) {
    bool result = v1.x == v2.x && v1.y == v2.y;
    return result;
}

function bool
v3feq(V3f v1, V3f v2) {
    bool result = v1.x == v2.x && v1.y == v2.y && v1.z == v2.z;
    return result;
}

function V2f
v2freverse(V2f v1) {
    V2f result = {-v1.x, -v1.y};
    return result;
}

function V3f
v3freverse(V3f v1) {
    V3f result = {.x = -v1.x, .y = -v1.y, .z = -v1.z};
    return result;
}

function V2f
v2fadd(V2f v1, V2f v2) {
    V2f result = {v1.x + v2.x, v1.y + v2.y};
    return result;
}

function V3f
v3fadd(V3f v1, V3f v2) {
    V3f result = {.x = v1.x + v2.x, .y = v1.y + v2.y, .z = v1.z + v2.z};
    return result;
}

function V2f
v2fsub(V2f v1, V2f v2) {
    V2f result = {v1.x - v2.x, v1.y - v2.y};
    return result;
}

function V3f
v3fsub(V3f v1, V3f v2) {
    V3f result = {.x = v1.x - v2.x, .y = v1.y - v2.y, .z = v1.z - v2.z};
    return result;
}

function V2f
v2fhadamard(V2f v1, V2f v2) {
    V2f result = {v1.x * v2.x, v1.y * v2.y};
    return result;
}

function V3f
v3fhadamard(V3f v1, V3f v2) {
    V3f result = {.x = v1.x * v2.x, .y = v1.y * v2.y, .z = v1.z * v2.z};
    return result;
}

function V2f
v2fscale(V2f v1, f32 by) {
    V2f result = {v1.x * by, v1.y * by};
    return result;
}

function V3f
v3fscale(V3f v1, f32 by) {
    V3f result = {.x = v1.x * by, .y = v1.y * by, .z = v1.z * by};
    return result;
}

function f32
v2fdot(V2f v1, V2f v2) {
    f32 result = v1.x * v2.x + v1.y * v2.y;
    return result;
}

function f32
v3fdot(V3f v1, V3f v2) {
    f32 result = v1.x * v2.x + v1.y * v2.y + v1.z * v2.z;
    return result;
}

function f32
v2flen(V2f v1) {
    f32 result = squareRootf(v2fdot(v1, v1));
    return result;
}

function f32
v3flen(V3f v1) {
    f32 result = squareRootf(v3fdot(v1, v1));
    return result;
}

function V2f
v2fnormalise(V2f v1) {
    f32 len = v2flen(v1);
    V2f result = v2fscale(v1, safeRatio1f(1, len));
    return result;
}

function V3f
v3fnormalise(V3f v1) {
    f32 len = v3flen(v1);
    V3f result = v3fscale(v1, safeRatio1f(1, len));
    return result;
}

function V2f
v2flerp(V2f from, V2f to, f32 by) {
    V2f result = {
        .x = lerpf(from.x, to.x, by),
        .y = lerpf(from.y, to.y, by),
    };
    return result;
}

function V3f
v3flerp(V3f from, V3f to, f32 by) {
    V3f result = {
        .x = lerpf(from.x, to.x, by),
        .y = lerpf(from.y, to.y, by),
        .z = lerpf(from.z, to.z, by),
    };
    return result;
}

typedef struct Rotor3f {
    f32 dt;
    f32 xy;
    f32 xz;
    f32 yz;
} Rotor3f;

function Rotor3f
createRotor3f(void) {
    Rotor3f result = {.dt = 1};
    return result;
}

function Rotor3f
createRotor3fAnglePlane(f32 angleTurns, f32 xy, f32 xz, f32 yz) {
    assert(xy != 0 || xz != 0 || yz != 0);
    f32 area = squareRootf(squaref(xy) + squaref(xz) + squaref(yz));

    f32 halfAngleTurns = angleTurns / 2;
    f32 sina = sinef(halfAngleTurns);
    f32 cosa = cosinef(halfAngleTurns);

    Rotor3f result = {
        .dt = cosa,
        .xy = -safeRatio1f(xy, area) * sina,
        .xz = -safeRatio1f(xz, area) * sina,
        .yz = -safeRatio1f(yz, area) * sina,
    };
    return result;
}

function bool
rotor3fEq(Rotor3f r1, Rotor3f r2) {
    bool result = r1.dt == r2.dt && r1.xy == r2.xy && r1.xz == r2.xz && r1.yz == r2.yz;
    return result;
}

function Rotor3f
rotor3fNormalise(Rotor3f r) {
    f32 l = squareRootf(squaref(r.dt) + squaref(r.xy) + squaref(r.xz) + squaref(r.yz));
    assert(l > 0);
    Rotor3f result = {
        .dt = r.dt / l,
        .xy = r.xy / l,
        .xz = r.xz / l,
        .yz = r.yz / l,
    };
    return result;
}

function Rotor3f
rotor3fReverse(Rotor3f r) {
    Rotor3f result = {r.dt, -r.xy, -r.xz, -r.yz};
    return result;
}

function Rotor3f
rotor3fMulRotor3f(Rotor3f p, Rotor3f q) {
    Rotor3f result = {
        .dt = p.dt * q.dt - p.xy * q.xy - p.xz * q.xz - p.yz * q.yz,
        .xy = p.xy * q.dt + p.dt * q.xy + p.yz * q.xz - p.xz * q.yz,
        .xz = p.xz * q.dt + p.dt * q.xz - p.yz * q.xy + p.xy * q.yz,
        .yz = p.yz * q.dt + p.dt * q.yz + p.xz * q.xy - p.xy * q.xz,
    };
    return result;
}

function V3f
rotor3fRotateV3f(Rotor3f r, V3f v) {
    f32 x = r.dt * v.x + v.y * r.xy + v.z * r.xz;
    f32 y = r.dt * v.y - v.x * r.xy + v.z * r.yz;
    f32 z = r.dt * v.z - v.x * r.xz - v.y * r.yz;
    f32 t = v.x * r.yz - v.y * r.xz + v.z * r.xy;

    V3f result = {
        .x = r.dt * x + y * r.xy + z * r.xz + t * r.yz,
        .y = r.dt * y - x * r.xy - t * r.xz + z * r.yz,
        .z = r.dt * z + t * r.xy - x * r.xz - y * r.yz,
    };

    return result;
}

function Rotor3f
rotor3fLerpN(Rotor3f r1, Rotor3f r2, f32 by) {
    Rotor3f resultNotNorm = {
        .dt = lerpf(r1.dt, r2.dt, by),
        .xy = lerpf(r1.xy, r2.xy, by),
        .xz = lerpf(r1.xz, r2.xz, by),
        .yz = lerpf(r1.yz, r2.yz, by),
    };
    Rotor3f result = rotor3fNormalise(resultNotNorm);
    return result;
}

typedef struct Rect2f {
    V2f topleft;
    V2f dim;
} Rect2f;

function Rect2f
rect2fCenterDim(V2f center, V2f dim) {
    Rect2f result = {v2fsub(center, v2fhadamard(dim, (V2f) {0.5, 0.5})), dim};
    return result;
}

function bool
rect2fEq(Rect2f r1, Rect2f r2) {
    bool result = v2feq(r1.topleft, r2.topleft) && v2feq(r1.dim, r2.dim);
    return result;
}

function Rect2f
rect2fClip(Rect2f rect, Rect2f clip) {
    f32 x0 = max(rect.topleft.x, clip.topleft.x);
    f32 y0 = max(rect.topleft.y, clip.topleft.y);

    V2f rectBottomRight = v2fadd(rect.topleft, rect.dim);
    V2f clipBottomRight = v2fadd(clip.topleft, clip.dim);
    f32 x1 = min(rectBottomRight.x, clipBottomRight.x);
    f32 y1 = min(rectBottomRight.y, clipBottomRight.y);

    V2f topleft = {x0, y0};
    V2f dim = v2fsub((V2f) {x1, y1}, topleft);

    Rect2f result = {topleft, dim};
    return result;
}

typedef struct Color255 {
    u8 r, g, b, a;
} Color255;

typedef struct Color01 {
    f32 r, g, b, a;
} Color01;

function u32
color255tou32(Color255 color) {
    u32 coloru32 = (color.a << 24) | (color.r << 16) | (color.g << 8) | (color.b << 0);
    return coloru32;
}

function Color255
coloru32to255(u32 color) {
    Color255 result = {.r = (u8)((color >> 16) & 0xff), .g = (u8)((color >> 8) & 0xff), .b = (u8)(color & 0xff), .a = (u8)(color >> 24)};
    return result;
}

function Color01
color255to01(Color255 color) {
    Color01 result = {.r = ((f32)color.r) / 255.0f, .g = ((f32)color.g) / 255.0f, .b = ((f32)color.b) / 255.0f, .a = ((f32)color.a) / 255.0f};
    return result;
}

function Color255
color01to255(Color01 color) {
    Color255 result = {
        .r = (u8)(color.r * 255.0f + 0.5f),
        .g = (u8)(color.g * 255.0f + 0.5f),
        .b = (u8)(color.b * 255.0f + 0.5f),
        .a = (u8)(color.a * 255.0f + 0.5f),
    };
    return result;
}

function bool
color01eq(Color01 c1, Color01 c2) {
    bool result = c1.a == c2.a && c1.r == c2.r && c1.g == c2.g && c1.b == c2.b;
    return result;
}

function Color01
color01Lerp(Color01 c1, Color01 c2, f32 by) {
    Color01 result = {
        .r = lerpf(c1.r, c2.r, by),
        .g = lerpf(c1.g, c2.g, by),
        .b = lerpf(c1.b, c2.b, by),
        .a = lerpf(c1.a, c2.a, by),
    };
    return result;
}

function Color01
color01add(Color01 c1, Color01 c2) {
    Color01 result = {.r = c1.r + c2.r, .g = c1.g + c2.g, .b = c1.b + c2.b, .a = c1.a + c2.a};
    return result;
}

function Color01
color01scale(Color01 c1, f32 by) {
    Color01 result = {.r = c1.r * by, .g = c1.g * by, .b = c1.b * by, .a = c1.a * by};
    return result;
}

function Color01
color01mul(Color01 c1, Color01 c2) {
    Color01 result = {.r = c1.r * c2.r, .g = c1.g * c2.g, .b = c1.b * c2.b, .a = c1.a * c2.a};
    return result;
}

function u32
coloru32GetContrast(u32 cu32) {
    Color255 c255 = coloru32to255(cu32);
    Color01  c01 = color255to01(c255);

    f32 luminance = c01.r * 0.2126 + c01.g * 0.7152 + c01.b * 0.0722;

    Color01 result01 = {.a = c01.a};
    if (luminance >= 0.5) {
        result01.r = c01.r * 0.3;
        result01.g = c01.g * 0.3;
        result01.b = c01.b * 0.3;
    } else {
        result01.r = (c01.r + 1) / 2;
        result01.g = (c01.g + 1) / 2;
        result01.b = (c01.b + 1) / 2;
    }

    Color255 result255 = color01to255(result01);
    u32      resultu32 = color255tou32(result255);
    return resultu32;
}

function f32
edgeWedge(V2f v1, V2f v2, V2f pt) {
    V2f v1v2 = v2fsub(v2, v1);
    V2f v1pt = v2fsub(pt, v1);
    f32 result = v1v2.x * v1pt.y - v1v2.y * v1pt.x;
    return result;
}

function bool
isTopLeft(V2f v1, V2f v2) {
    V2f  v1v2 = v2fsub(v2, v1);
    bool isFlatTop = v1v2.y == 0 && v1v2.x > 0;
    bool isLeft = v1v2.y < 0;
    bool result = isFlatTop || isLeft;
    return result;
}

#define NK_PRIVATE 1
#define NK_INCLUDE_FIXED_TYPES 1
#define NK_INCLUDE_STANDARD_BOOL 1
#define NK_ASSERT(cond) assert(cond)
#define NK_MEMSET(ptr, val, size) memset_(ptr, val, size)
#define NK_MEMCPY(dest, src, size) memcpy_(dest, src, size)
#define NK_INV_SQRT(n) (1 / squareRootf(n))
#define NK_IMPLEMENTATION 1
#include "nuklear.h"

//
// SECTION Meshes
//

typedef struct TriangleIndices {
    i32 i1, i2, i3;
} TriangleIndices;

typedef struct IndexArrDyn {
    TriangleIndices* ptr;
    isize            len;
    isize            cap;
} IndexArrDyn;

typedef struct V3fArrDyn {
    V3f*  ptr;
    isize len;
    isize cap;
} V3fArrDyn;

typedef struct Color01ArrDyn {
    Color01* ptr;
    isize    len;
    isize    cap;
} Color01ArrDyn;

typedef struct Mesh {
    struct {
        V3f*  ptr;
        isize len;
    } vertices;

    struct {
        TriangleIndices* ptr;
        isize            len;
    } indices;

    struct {
        Color01* ptr;
        isize    len;
    } colors;

    V3f     pos;
    Rotor3f orientation;
} Mesh;

typedef struct MeshStorage {
    V3fArrDyn     vertices;
    IndexArrDyn   indices;
    Color01ArrDyn colors;
} MeshStorage;

typedef struct MeshBuilder {
    MeshStorage* storage;
    isize        vertexLenBefore;
    isize        indexLenBefore;
    isize        colorsLenBefore;
} MeshBuilder;

function MeshStorage
createMeshStorage(Arena* arena, isize bytes) {
    MeshStorage storage = {};
    isize       bytesPerBuffer = bytes / 3;
    arenaAllocCap(arena, V3f, bytesPerBuffer, storage.vertices);
    arenaAllocCap(arena, TriangleIndices, bytesPerBuffer, storage.indices);
    arenaAllocCap(arena, Color01, bytesPerBuffer, storage.colors);
    return storage;
}

function void
meshStorageClearBuffers(MeshStorage* storage) {
    storage->vertices.len = 0;
    storage->colors.len = 0;
    storage->indices.len = 0;
}

function MeshBuilder
beginMesh(MeshStorage* storage) {
    MeshBuilder builder = {storage, storage->vertices.len, storage->indices.len, storage->colors.len};
    return builder;
}

function Mesh
endMesh(MeshBuilder builder, V3f pos, Rotor3f orientation) {
    Mesh mesh = {
        .vertices.ptr = builder.storage->vertices.ptr + builder.vertexLenBefore,
        .vertices.len = builder.storage->vertices.len - builder.vertexLenBefore,
        .indices.ptr = builder.storage->indices.ptr + builder.indexLenBefore,
        .indices.len = builder.storage->indices.len - builder.indexLenBefore,
        .colors.ptr = builder.storage->colors.ptr + builder.colorsLenBefore,
        .colors.len = builder.storage->colors.len - builder.colorsLenBefore,
        .pos = pos,
        .orientation = orientation,
    };
    assert(mesh.vertices.len >= 0);
    assert(mesh.indices.len >= 0);
    assert(mesh.colors.len >= 0);
    assert(mesh.vertices.len == mesh.colors.len);
    for (i32 ind = 0; ind < mesh.indices.len; ind++) {
        TriangleIndices* trig = mesh.indices.ptr + ind;
        trig->i1 -= builder.vertexLenBefore;
        trig->i2 -= builder.vertexLenBefore;
        trig->i3 -= builder.vertexLenBefore;
    }
    return mesh;
}

function void
meshStorageAddTriangle(MeshStorage* storage, TriangleIndices trig) {
    i32 i1 = trig.i1;
    i32 i2 = trig.i2;
    i32 i3 = trig.i3;
    assert(i1 >= 0 && i2 >= 0 && i3 >= 0);
    assert(i1 < storage->vertices.len && i2 < storage->vertices.len && i3 < storage->vertices.len);
    assert(i1 < storage->colors.len && i2 < storage->colors.len && i3 < storage->colors.len);
    arrpush(storage->indices, trig);
}

function void
meshStorageAddQuad(MeshStorage* storage, i32 i1, i32 i2, i32 i3, i32 i4) {
    meshStorageAddTriangle(storage, (TriangleIndices) {i1, i2, i3});
    meshStorageAddTriangle(storage, (TriangleIndices) {i1, i3, i4});
}

function Mesh
createCubeMesh(MeshStorage* storage, f32 dim, V3f pos, Rotor3f orientation) {
    f32         halfdim = dim / 2;
    MeshBuilder cubeBuilder = beginMesh(storage);

    V3f frontTopLeft = {.x = -halfdim, .y = halfdim, .z = -halfdim};
    V3f frontTopRight = {.x = halfdim, .y = halfdim, .z = -halfdim};
    V3f frontBottomLeft = {.x = -halfdim, .y = -halfdim, .z = -halfdim};
    V3f frontBottomRight = {.x = halfdim, .y = -halfdim, .z = -halfdim};

    V3f backTopLeft = {.x = -halfdim, .y = halfdim, .z = halfdim};
    V3f backTopRight = {.x = halfdim, .y = halfdim, .z = halfdim};
    V3f backBottomLeft = {.x = -halfdim, .y = -halfdim, .z = halfdim};
    V3f backBottomRight = {.x = halfdim, .y = -halfdim, .z = halfdim};

    i32 frontTopLeftIndex = arrpush(storage->vertices, frontTopLeft);
    i32 frontTopRightIndex = arrpush(storage->vertices, frontTopRight);
    i32 frontBottomLeftIndex = arrpush(storage->vertices, frontBottomLeft);
    i32 frontBottomRightIndex = arrpush(storage->vertices, frontBottomRight);
    i32 backTopLeftIndex = arrpush(storage->vertices, backTopLeft);
    i32 backTopRightIndex = arrpush(storage->vertices, backTopRight);
    i32 backBottomLeftIndex = arrpush(storage->vertices, backBottomLeft);
    i32 backBottomRightIndex = arrpush(storage->vertices, backBottomRight);

    arrpush(storage->colors, ((Color01) {.r = 1, .a = 1}));
    arrpush(storage->colors, ((Color01) {.g = 1, .a = 1}));
    arrpush(storage->colors, ((Color01) {.b = 1, .a = 1}));
    arrpush(storage->colors, ((Color01) {.r = 1, .g = 1, .a = 1}));
    arrpush(storage->colors, ((Color01) {.r = 1, .b = 1, .a = 1}));
    arrpush(storage->colors, ((Color01) {.g = 1, .b = 1, .a = 1}));
    arrpush(storage->colors, ((Color01) {.r = 1, .g = 1, .b = 1, .a = 1}));
    arrpush(storage->colors, ((Color01) {.r = 0.1, .g = 0.5, .b = 0.9, .a = 1}));

    meshStorageAddQuad(storage, frontTopLeftIndex, frontTopRightIndex, frontBottomRightIndex, frontBottomLeftIndex);
    meshStorageAddQuad(storage, frontTopRightIndex, backTopRightIndex, backBottomRightIndex, frontBottomRightIndex);
    meshStorageAddQuad(storage, frontTopLeftIndex, backTopLeftIndex, backTopRightIndex, frontTopRightIndex);
    meshStorageAddQuad(storage, frontTopLeftIndex, frontBottomLeftIndex, backBottomLeftIndex, backTopLeftIndex);
    meshStorageAddQuad(storage, frontBottomLeftIndex, frontBottomRightIndex, backBottomRightIndex, backBottomLeftIndex);
    meshStorageAddQuad(storage, backTopRightIndex, backTopLeftIndex, backBottomLeftIndex, backBottomRightIndex);

    Mesh cube = endMesh(cubeBuilder, pos, orientation);
    return cube;
}

//
// SECTION Camera
//

typedef struct Camera {
    V3f     pos;
    V2f     halfFovTurns;
    V2f     tanHalfFov;
    f32     nearClipZ;
    f32     farClipZ;
    Rotor3f currentOrientation;
    Rotor3f targetOrientation;
    f32     moveWUPerSec;
    f32     moveWUPerSecAccelerated;
    f32     rotTurnsPerSec;
} Camera;

function Camera
createCamera(V3f pos, f32 width, f32 height) {
    f32    fovxTurns = 0.25;
    f32    halfFovXTurns = fovxTurns / 2;
    f32    tanhalffovx = tangentf(halfFovXTurns);
    f32    tanhalffovy = height / width * tanhalffovx;
    f32    halfFovYTurns = arctanf(tanhalffovy);
    Camera camera = {
        .pos = pos,
        .halfFovTurns = {halfFovXTurns, halfFovYTurns},
        .tanHalfFov = {tanhalffovx, tanhalffovy},
        .nearClipZ = 0.001f,
        .farClipZ = 100.0f,
        .currentOrientation = createRotor3f(),
        .targetOrientation = createRotor3f(),
        .moveWUPerSec = 5,
        .moveWUPerSecAccelerated = 15,
        .rotTurnsPerSec = 0.2,
    };
    return camera;
}

//
// SECTION Input
//

typedef enum InputKey {
    InputKey_Forward,
    InputKey_Back,
    InputKey_Left,
    InputKey_Right,
    InputKey_Up,
    InputKey_Down,
    InputKey_MoveFaster,
    InputKey_RotateXY,
    InputKey_RotateYX,
    InputKey_RotateXZ,
    InputKey_RotateZX,
    InputKey_RotateYZ,
    InputKey_RotateZY,
    InputKey_ToggleDebugTriangles,
    InputKey_ToggleOutlines,
    InputKey_ToggleSW,
    InputKey_ToggleDebugUI,
    InputKey_Count,
} InputKey;

typedef struct KeyState {
    bool down;
    i32  halfTransitionCount;
} KeyState;

typedef struct MouseState {
    i32 x, y;
    i32 dx, dy;
} MouseState;

typedef struct Input {
    KeyState   keys[InputKey_Count];
    MouseState mouse;
} Input;

function void
inputClearKeys(Input* input) {
    for (i32 keyIndex = 0; keyIndex < InputKey_Count; keyIndex++) {
        KeyState* state = input->keys + keyIndex;
        *state = (KeyState) {};
    }
}

function void
inputBeginFrame(Input* input) {
    for (i32 keyIndex = 0; keyIndex < InputKey_Count; keyIndex++) {
        KeyState* state = input->keys + keyIndex;
        state->halfTransitionCount = 0;
    }
    input->mouse.dx = 0;
    input->mouse.dy = 0;
}

function void
inputKeyDown(Input* input, InputKey key) {
    KeyState* state = input->keys + key;
    state->down = true;
    state->halfTransitionCount += 1;
}

function void
inputKeyUp(Input* input, InputKey key) {
    KeyState* state = input->keys + key;
    state->down = false;
    state->halfTransitionCount += 1;
}

function bool
inputKeyWasPressed(Input* input, InputKey key) {
    KeyState state = input->keys[key];
    bool     result = state.halfTransitionCount > 1 || (state.halfTransitionCount == 1 && state.down);
    return result;
}

//
// SECTION Texture
//

typedef struct Texture {
    u32*  ptr;
    isize width;
    isize height;
    isize pitch;
} Texture;

function void
swDrawLine(Texture texture, V2f v1, V2f v2, Color01 color) {
    i32 x0 = (i32)(v1.x + 0.5);
    i32 y0 = (i32)(v1.y + 0.5);
    i32 x1 = (i32)(v2.x + 0.5);
    i32 y1 = (i32)(v2.y + 0.5);

    i32 dx = absval(x1 - x0);
    i32 sx = x0 < x1 ? 1 : -1;
    i32 dy = -absval(y1 - y0);
    i32 sy = y0 < y1 ? 1 : -1;
    i32 error = dx + dy;

    u32 color32 = color255tou32(color01to255(color));

    for (;;) {
        if (y0 >= 0 && x0 >= 0 && y0 < texture.height && x0 < texture.width) {
            isize index = y0 * texture.pitch + x0;
            texture.ptr[index] = color32;
        }
        if (x0 == x1 && y0 == y1) {
            break;
        }
        i32 e2 = 2 * error;

        if (e2 >= dy) {
            if (x0 == x1) {
                break;
            }
            error = error + dy;
            x0 = x0 + sx;
        }

        if (e2 <= dx) {
            if (y0 == y1) {
                break;
            }
            error = error + dx;
            y0 = y0 + sy;
        }
    }
}

function void
swDrawRect(Texture texture, Rect2f rect, Color01 color) {
    Rect2f rectClipped = rect2fClip(rect, (Rect2f) {{-0.5, -0.5}, {(f32)texture.width, (f32)texture.height}});
    i32    x0 = (i32)(rectClipped.topleft.x + 0.5);
    i32    x1 = (i32)(rectClipped.topleft.x + rectClipped.dim.x + 0.5);
    i32    y0 = (i32)(rectClipped.topleft.y + 0.5);
    i32    y1 = (i32)(rectClipped.topleft.y + rectClipped.dim.y + 0.5);

    u32 color32 = color255tou32(color01to255(color));

    for (i32 ycoord = y0; ycoord < y1; ycoord++) {
        for (i32 xcoord = x0; xcoord < x1; xcoord++) {
            isize index = ycoord * texture.pitch + xcoord;
            texture.ptr[index] = color32;
        }
    }
}

//
// SECTION SWRenderer
//

typedef struct SWRenderer {
    union {
        struct {
            u32*   pixels;
            isize  width;
            isize  height;
            isize  pitch;
            float* depth;
            isize  cap;
        } image;
        Texture texture;
    };

    MeshStorage trisCamera;
    MeshStorage trisScreen;

    bool showDebugTriangles;
    bool showOutlines;
} SWRenderer;

function SWRenderer
createSWRenderer(Arena* arena, isize bytes) {
    SWRenderer renderer = {};

    isize forImage = bytes / 4 * 3;
    isize forTriangles = (bytes - forImage) / 2;

    renderer.image.cap = forImage / (sizeof(u32) + sizeof(float));
    renderer.image.pixels = arenaAllocArray(arena, u32, renderer.image.cap);
    renderer.image.depth = arenaAllocArray(arena, float, renderer.image.cap);

    renderer.trisCamera = createMeshStorage(arena, forTriangles);
    renderer.trisScreen = createMeshStorage(arena, forTriangles);

    return renderer;
}

function void
swRendererSetImageSize(SWRenderer* renderer, isize width, isize height) {
    isize pitch = width + 16;
    isize pixelCount = pitch * height;
    assert(pixelCount <= renderer->image.cap);
    renderer->image.width = width;
    renderer->image.height = height;
    renderer->image.pitch = pitch;
}

function void
swRendererClearImage(SWRenderer* renderer) {
    timedSectionStart(__FUNCTION__);
    isize entryCount = renderer->image.pitch * renderer->image.height;
    zeromem(renderer->image.pixels, entryCount * sizeof(u32));
    fltset(renderer->image.depth, FLT_MAX, entryCount);
    timedSectionEnd();
}

typedef struct Triangle {
    V2f     v1, v2, v3;
    f32     v1z, v2z, v3z;
    Color01 c1, c2, c3;
    f32     area;
} Triangle;

function Triangle
swRendererPullTriangle(SWRenderer* renderer, TriangleIndices trig) {
    V3f v1 = arrget(renderer->trisScreen.vertices, trig.i1);
    V3f v2 = arrget(renderer->trisScreen.vertices, trig.i2);
    V3f v3 = arrget(renderer->trisScreen.vertices, trig.i3);

    V2f halfImageDim = {(f32)renderer->image.width * 0.5f, (f32)renderer->image.height * 0.5f};

    Triangle result = {
        .v1 = v2fhadamard((V2f) {v1.x + 1, -v1.y + 1}, halfImageDim),
        .v2 = v2fhadamard((V2f) {v2.x + 1, -v2.y + 1}, halfImageDim),
        .v3 = v2fhadamard((V2f) {v3.x + 1, -v3.y + 1}, halfImageDim),

        .v1z = v1.z,
        .v2z = v2.z,
        .v3z = v3.z,

        .c1 = arrget(renderer->trisScreen.colors, trig.i1),
        .c2 = arrget(renderer->trisScreen.colors, trig.i2),
        .c3 = arrget(renderer->trisScreen.colors, trig.i3),
    };

    result.area = edgeWedge(result.v1, result.v2, result.v3);
    return result;
}

function void
swRendererFillTriangle(SWRenderer* renderer, TriangleIndices trig) {
    timedSectionStart(__FUNCTION__);

    Triangle tr = swRendererPullTriangle(renderer, trig);

    V2f v1 = tr.v1;
    V2f v2 = tr.v2;
    V2f v3 = tr.v3;

    if (tr.area > 0) {
        Color01 c1 = tr.c1;
        Color01 c2 = tr.c2;
        Color01 c3 = tr.c3;

        f32 z1inv = 1.0f / tr.v1z;
        f32 z2inv = 1.0f / tr.v2z;
        f32 z3inv = 1.0f / tr.v3z;

        __m512 z1inv512 = _mm512_set1_ps(z1inv);
        __m512 z2inv512 = _mm512_set1_ps(z2inv);
        __m512 z3inv512 = _mm512_set1_ps(z3inv);

        Color01 c1z = color01scale(c1, z1inv);
        Color01 c2z = color01scale(c2, z2inv);
        Color01 c3z = color01scale(c3, z3inv);

        __m512 c1zr512 = _mm512_set1_ps(c1z.r);
        __m512 c1zg512 = _mm512_set1_ps(c1z.g);
        __m512 c1zb512 = _mm512_set1_ps(c1z.b);
        __m512 c1za512 = _mm512_set1_ps(c1z.a);

        __m512 c2zr512 = _mm512_set1_ps(c2z.r);
        __m512 c2zg512 = _mm512_set1_ps(c2z.g);
        __m512 c2zb512 = _mm512_set1_ps(c2z.b);
        __m512 c2za512 = _mm512_set1_ps(c2z.a);

        __m512 c3zr512 = _mm512_set1_ps(c3z.r);
        __m512 c3zg512 = _mm512_set1_ps(c3z.g);
        __m512 c3zb512 = _mm512_set1_ps(c3z.b);
        __m512 c3za512 = _mm512_set1_ps(c3z.a);

        f32 xmin = min(v1.x, min(v2.x, v3.x));
        f32 ymin = min(v1.y, min(v2.y, v3.y));
        f32 xmax = max(v1.x, max(v2.x, v3.x));
        f32 ymax = max(v1.y, max(v2.y, v3.y));

        f32 dcross1x = v1.y - v2.y;
        f32 dcross2x = v2.y - v3.y;
        f32 dcross3x = v3.y - v1.y;

        __m512 dcross1x512 = _mm512_set1_ps(dcross1x);
        __m512 dcross2x512 = _mm512_set1_ps(dcross2x);
        __m512 dcross3x512 = _mm512_set1_ps(dcross3x);

        f32 dcross1y = v2.x - v1.x;
        f32 dcross2y = v3.x - v2.x;
        f32 dcross3y = v1.x - v3.x;

        i32 ystart = max((i32)ymin, 0);
        i32 xstart = max((i32)xmin, 0);
        i32 yend = min((i32)ymax, renderer->image.height - 1);
        i32 xend = min((i32)xmax, renderer->image.width - 1);

        V2f topleft = {(f32)(xstart), (f32)(ystart)};
        f32 cross1topleft = edgeWedge(v1, v2, topleft);
        f32 cross2topleft = edgeWedge(v2, v3, topleft);
        f32 cross3topleft = edgeWedge(v3, v1, topleft);

        f32    oneOverArea = 1 / tr.area;
        __m512 oneOverArea512 = _mm512_set1_ps(oneOverArea);

        __m512i seq0to15i = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
        __m512i n16_512i = _mm512_set1_epi32(16);
        __m512  n0_512f = _mm512_set1_ps(0);
        __m512  n1_512f = _mm512_set1_ps(1);
        __m512  n255_512f = _mm512_set1_ps(255);
        __m512i xstart512 = _mm512_set1_epi32(xstart);
        __m512i xfirst512 = _mm512_add_epi32(xstart512, seq0to15i);

        for (i32 ycoord = ystart; ycoord <= yend; ycoord++) {
            f32 yinc = (f32)(ycoord - ystart);

            f32 cross1row = cross1topleft + yinc * dcross1y;
            f32 cross2row = cross2topleft + yinc * dcross2y;
            f32 cross3row = cross3topleft + yinc * dcross3y;

            __m512 cross1row512 = _mm512_set1_ps(cross1row);
            __m512 cross2row512 = _mm512_set1_ps(cross2row);
            __m512 cross3row512 = _mm512_set1_ps(cross3row);

            i32 ycoordTimesPitch = ycoord * renderer->image.pitch;

            __m512i xcoord512 = xfirst512;
            __m512i xinc512i = seq0to15i;
            for (i32 simdXcoord = xstart; simdXcoord <= xend; simdXcoord += 16, xcoord512 = _mm512_add_epi32(xcoord512, n16_512i), xinc512i = _mm512_add_epi32(xinc512i, n16_512i)) {
                __m512 xinc512f = _mm512_cvtepi32_ps(xinc512i);
                __m512 cross1_512 = _mm512_add_ps(cross1row512, _mm512_mul_ps(xinc512f, dcross1x512));
                __m512 cross2_512 = _mm512_add_ps(cross2row512, _mm512_mul_ps(xinc512f, dcross2x512));
                __m512 cross3_512 = _mm512_add_ps(cross3row512, _mm512_mul_ps(xinc512f, dcross3x512));

                // NOTE(khvorov) >= here means edges of adjacent tris will be filled by both tris.
                // Not ideal but seems to be the least bad simple way to get around floating point imprecision-related inconsistencies
                __mmask16 cross1gt0_512 = _mm512_cmp_ps_mask(cross1_512, n0_512f, _CMP_GE_OQ);
                __mmask16 cross2gt0_512 = _mm512_cmp_ps_mask(cross2_512, n0_512f, _CMP_GE_OQ);
                __mmask16 cross3gt0_512 = _mm512_cmp_ps_mask(cross3_512, n0_512f, _CMP_GE_OQ);

                __mmask16 allpass512 = cross1gt0_512 & cross2gt0_512 & cross3gt0_512;

                __m512 cross1scaled512 = _mm512_mul_ps(cross1_512, oneOverArea512);
                __m512 cross2scaled512 = _mm512_mul_ps(cross2_512, oneOverArea512);
                __m512 cross3scaled512 = _mm512_mul_ps(cross3_512, oneOverArea512);

                __m512 z1inv512scaled = _mm512_mul_ps(z1inv512, cross2scaled512);
                __m512 z2inv512scaled = _mm512_mul_ps(z2inv512, cross3scaled512);
                __m512 z3inv512scaled = _mm512_mul_ps(z3inv512, cross1scaled512);

                __m512 zinterpinv512 = _mm512_add_ps(_mm512_add_ps(z1inv512scaled, z2inv512scaled), z3inv512scaled);
                __m512 zinterp512 = _mm512_div_ps(n1_512f, zinterpinv512);

                i32    addrOffset = ycoordTimesPitch + simdXcoord;
                f32*   depthAddr = renderer->image.depth + addrOffset;
                __m512 existingZ512 = _mm512_mask_loadu_ps(n0_512f, allpass512, depthAddr);

                __mmask16 zpass512 = _mm512_cmp_ps_mask(existingZ512, zinterp512, _CMP_GT_OQ);
                __mmask16 allPassAndZPass512 = allpass512 & zpass512;
                _mm512_mask_storeu_ps(depthAddr, allPassAndZPass512, zinterp512);

                __m512 c1zrscaled512 = _mm512_mul_ps(c1zr512, cross2scaled512);
                __m512 c1zgscaled512 = _mm512_mul_ps(c1zg512, cross2scaled512);
                __m512 c1zbscaled512 = _mm512_mul_ps(c1zb512, cross2scaled512);
                __m512 c1zascaled512 = _mm512_mul_ps(c1za512, cross2scaled512);

                __m512 c2zrscaled512 = _mm512_mul_ps(c2zr512, cross3scaled512);
                __m512 c2zgscaled512 = _mm512_mul_ps(c2zg512, cross3scaled512);
                __m512 c2zbscaled512 = _mm512_mul_ps(c2zb512, cross3scaled512);
                __m512 c2zascaled512 = _mm512_mul_ps(c2za512, cross3scaled512);

                __m512 c3zrscaled512 = _mm512_mul_ps(c3zr512, cross1scaled512);
                __m512 c3zgscaled512 = _mm512_mul_ps(c3zg512, cross1scaled512);
                __m512 c3zbscaled512 = _mm512_mul_ps(c3zb512, cross1scaled512);
                __m512 c3zascaled512 = _mm512_mul_ps(c3za512, cross1scaled512);

                __m512 color01zr512 = _mm512_add_ps(_mm512_add_ps(c1zrscaled512, c2zrscaled512), c3zrscaled512);
                __m512 color01zg512 = _mm512_add_ps(_mm512_add_ps(c1zgscaled512, c2zgscaled512), c3zgscaled512);
                __m512 color01zb512 = _mm512_add_ps(_mm512_add_ps(c1zbscaled512, c2zbscaled512), c3zbscaled512);
                __m512 color01za512 = _mm512_add_ps(_mm512_add_ps(c1zascaled512, c2zascaled512), c3zascaled512);

                __m512 color01r512 = _mm512_mul_ps(color01zr512, zinterp512);
                __m512 color01g512 = _mm512_mul_ps(color01zg512, zinterp512);
                __m512 color01b512 = _mm512_mul_ps(color01zb512, zinterp512);
                __m512 color01a512 = _mm512_mul_ps(color01za512, zinterp512);

                __m512 color255r512f = _mm512_mul_ps(color01r512, n255_512f);
                __m512 color255g512f = _mm512_mul_ps(color01g512, n255_512f);
                __m512 color255b512f = _mm512_mul_ps(color01b512, n255_512f);
                __m512 color255a512f = _mm512_mul_ps(color01a512, n255_512f);

                __m512i color255r512i = _mm512_cvt_roundps_epi32(color255r512f, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
                __m512i color255g512i = _mm512_cvt_roundps_epi32(color255g512f, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
                __m512i color255b512i = _mm512_cvt_roundps_epi32(color255b512f, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
                __m512i color255a512i = _mm512_cvt_roundps_epi32(color255a512f, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);

                __m512i color255r512shifted = _mm512_slli_epi32(color255r512i, 16);
                __m512i color255g512shifted = _mm512_slli_epi32(color255g512i, 8);
                __m512i color255b512shifted = color255b512i;
                __m512i color255a512shifted = _mm512_slli_epi32(color255a512i, 24);

                __m512i coloru32_512 = _mm512_or_epi32(
                    _mm512_or_epi32(color255r512shifted, color255g512shifted),
                    _mm512_or_epi32(color255b512shifted, color255a512shifted)
                );

                u32* pxAddr = renderer->image.pixels + addrOffset;
                _mm512_mask_storeu_epi32(pxAddr, allPassAndZPass512, coloru32_512);
            }
        }
    }

    timedSectionEnd();
}

function void
swRendererOutlineTriangle(SWRenderer* renderer, TriangleIndices trig) {
    Triangle tr = swRendererPullTriangle(renderer, trig);

    V2f v1 = tr.v1;
    V2f v2 = tr.v2;
    V2f v3 = tr.v3;

    Color01 color = {.r = 0.1, .g = 0.4, .b = 0.8, .a = 1};

    if (tr.area > 0) {
        swDrawLine(renderer->texture, v1, v2, color);
        swDrawLine(renderer->texture, v2, v3, color);
        swDrawLine(renderer->texture, v3, v1, color);

        V2f vertexRectDim = {10, 10};
        swDrawRect(renderer->texture, rect2fCenterDim(v1, vertexRectDim), color);
        swDrawRect(renderer->texture, rect2fCenterDim(v2, vertexRectDim), color);
        swDrawRect(renderer->texture, rect2fCenterDim(v3, vertexRectDim), color);
    }
}

function void
swRendererFillTriangles(SWRenderer* renderer) {
    for (i32 ind = 0; ind < renderer->trisScreen.indices.len; ind++) {
        TriangleIndices trig = renderer->trisScreen.indices.ptr[ind];
        swRendererFillTriangle(renderer, trig);
    }
}

function void
swRendererOutlineTriangles(SWRenderer* renderer) {
    for (i32 ind = 0; ind < renderer->trisScreen.indices.len; ind++) {
        TriangleIndices trig = renderer->trisScreen.indices.ptr[ind];
        swRendererOutlineTriangle(renderer, trig);
    }
}

typedef struct SWRendererMeshBuilder {
    SWRenderer* renderer;
    i32         firstVertexIndex;
    i32         firstIndexIndex;
} SWRendererMeshBuilder;

function SWRendererMeshBuilder
swRendererBeginMesh(SWRenderer* renderer) {
    SWRendererMeshBuilder builder = {renderer, (i32)renderer->trisCamera.vertices.len, (i32)renderer->trisCamera.indices.len};
    return builder;
}

function void
swRendererEndMesh(SWRendererMeshBuilder builder) {
    for (i32 indexIndex = builder.firstIndexIndex; indexIndex < builder.renderer->trisCamera.indices.len; indexIndex++) {
        TriangleIndices* trig = builder.renderer->trisCamera.indices.ptr + indexIndex;
        trig->i1 += builder.firstVertexIndex;
        trig->i2 += builder.firstVertexIndex;
        trig->i3 += builder.firstVertexIndex;
    }
}

function void
swRendererPushMesh(SWRenderer* renderer, Mesh mesh, Camera camera) {
    SWRendererMeshBuilder cubeInRendererBuilder = swRendererBeginMesh(renderer);

    for (i32 ind = 0; ind < mesh.vertices.len; ind++) {
        V3f vtxModel = mesh.vertices.ptr[ind];

        V3f vtxWorld = {};
        {
            V3f rot = rotor3fRotateV3f(mesh.orientation, vtxModel);
            V3f trans = v3fadd(rot, mesh.pos);
            vtxWorld = trans;
        }

        V3f vtxCamera = {};
        {
            V3f     trans = v3fsub(vtxWorld, camera.pos);
            Rotor3f cameraRotationRev = rotor3fReverse(camera.currentOrientation);
            V3f     rot = rotor3fRotateV3f(cameraRotationRev, trans);
            vtxCamera = rot;
        }

        arrpush(renderer->trisCamera.vertices, vtxCamera);
        arrpush(renderer->trisCamera.colors, arrget(mesh.colors, ind));
    }

    for (i32 ind = 0; ind < mesh.indices.len; ind++) {
        TriangleIndices trig = mesh.indices.ptr[ind];
        meshStorageAddTriangle(&renderer->trisCamera, trig);
    }

    swRendererEndMesh(cubeInRendererBuilder);
}

function void
swRendererDrawDebugTriangles(SWRenderer* renderer) {
    // NOTE(khvorov) Debug triangles from
    // https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-rasterizer-stage-rules
    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 0.5, .y = 0.5, .z = 1}));
    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 5.5, .y = 1.5, .z = 1}));
    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 1.5, .y = 3.5, .z = 1}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));

    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 4, .y = 0, .z = 1}));
    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 4, .y = 0, .z = 1}));
    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 4, .y = 0, .z = 1}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));

    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 5.75, .y = -0.25, .z = 1}));
    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 5.75, .y = 0.75, .z = 1}));
    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 4.75, .y = 0.75, .z = 1}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));

    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 7, .y = 0, .z = 1}));
    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 7, .y = 1, .z = 1}));
    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 6, .y = 1, .z = 1}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));

    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 7.25, .y = 2, .z = 1}));
    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 9.25, .y = 0.25, .z = 1}));
    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 11.25, .y = 2, .z = 1}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));

    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 7.25, .y = 2, .z = 1}));
    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 11.25, .y = 2, .z = 1}));
    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 9, .y = 4.75, .z = 1}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.g = 1, .a = 0.5}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.g = 1, .a = 0.5}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.g = 1, .a = 0.5}));

    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 13, .y = 1, .z = 1}));
    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 14.5, .y = -0.5, .z = 1}));
    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 14, .y = 2, .z = 1}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));

    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 13, .y = 1, .z = 1}));
    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 14, .y = 2, .z = 1}));
    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 14, .y = 4, .z = 1}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));

    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 0.5, .y = 5.5, .z = 1}));
    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 6.5, .y = 3.5, .z = 1}));
    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 4.5, .y = 5.5, .z = 1}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));

    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 4.5, .y = 5.5, .z = 1}));
    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 6.5, .y = 3.5, .z = 1}));
    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 7.5, .y = 6.5, .z = 1}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.g = 1, .a = 0.5}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.g = 1, .a = 0.5}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.g = 1, .a = 0.5}));

    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 6.5, .y = 3.5, .z = 1}));
    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 9, .y = 5, .z = 1}));
    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 7.5, .y = 6.5, .z = 1}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));

    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 9, .y = 7, .z = 1}));
    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 10, .y = 7, .z = 1}));
    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 9, .y = 9, .z = 1}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));

    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 11, .y = 4, .z = 1}));
    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 12, .y = 5, .z = 1}));
    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 11, .y = 6, .z = 1}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));

    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 13, .y = 5, .z = 1}));
    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 15, .y = 5, .z = 1}));
    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 13, .y = 7, .z = 1}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.r = 1, .a = 0.5}));

    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 15, .y = 5, .z = 1}));
    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 15, .y = 7, .z = 1}));
    arrpush(renderer->trisScreen.vertices, ((V3f) {.x = 13, .y = 7, .z = 1}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.g = 1, .a = 0.5}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.g = 1, .a = 0.5}));
    arrpush(renderer->trisScreen.colors, ((Color01) {.g = 1, .a = 0.5}));

    assert(renderer->trisScreen.colors.len == renderer->trisScreen.vertices.len);
    assert(renderer->trisScreen.vertices.len % 3 == 0);

    isize imageWidth = 16;
    isize imageHeight = 8;
    for (isize ind = 0; ind < renderer->trisScreen.vertices.len; ind++) {
        V2f* xy = &renderer->trisScreen.vertices.ptr[ind].xy;
        *xy = v2fhadamard(*xy, (V2f) {1.0f / (f32)imageWidth, 1.0f / (f32)imageHeight});
        *xy = v2fadd(v2fscale(*xy, 2), (V2f) {-1, -1});
        xy->y *= -1;
    }

    for (i32 ind = 0; ind < renderer->trisScreen.vertices.len; ind += 3) {
        TriangleIndices trig = {ind, ind + 1, ind + 2};
        meshStorageAddTriangle(&renderer->trisScreen, trig);
    }

    swRendererSetImageSize(renderer, imageWidth, imageHeight);
    swRendererClearImage(renderer);
    swRendererFillTriangles(renderer);
}

//
// SECTION Fonts
//

// clang-format off
// Taken from
// https://github.com/nakst/luigi
const u64 globalFontData[] = {
	0x0000000000000000UL, 0x0000000000000000UL, 0xBD8181A5817E0000UL, 0x000000007E818199UL, 0xC3FFFFDBFF7E0000UL, 0x000000007EFFFFE7UL, 0x7F7F7F3600000000UL, 0x00000000081C3E7FUL,
	0x7F3E1C0800000000UL, 0x0000000000081C3EUL, 0xE7E73C3C18000000UL, 0x000000003C1818E7UL, 0xFFFF7E3C18000000UL, 0x000000003C18187EUL, 0x3C18000000000000UL, 0x000000000000183CUL,
	0xC3E7FFFFFFFFFFFFUL, 0xFFFFFFFFFFFFE7C3UL, 0x42663C0000000000UL, 0x00000000003C6642UL, 0xBD99C3FFFFFFFFFFUL, 0xFFFFFFFFFFC399BDUL, 0x331E4C5870780000UL, 0x000000001E333333UL,
	0x3C666666663C0000UL, 0x0000000018187E18UL, 0x0C0C0CFCCCFC0000UL, 0x00000000070F0E0CUL, 0xC6C6C6FEC6FE0000UL, 0x0000000367E7E6C6UL, 0xE73CDB1818000000UL, 0x000000001818DB3CUL,
	0x1F7F1F0F07030100UL, 0x000000000103070FUL, 0x7C7F7C7870604000UL, 0x0000000040607078UL, 0x1818187E3C180000UL, 0x0000000000183C7EUL, 0x6666666666660000UL, 0x0000000066660066UL,
	0xD8DEDBDBDBFE0000UL, 0x00000000D8D8D8D8UL, 0x6363361C06633E00UL, 0x0000003E63301C36UL, 0x0000000000000000UL, 0x000000007F7F7F7FUL, 0x1818187E3C180000UL, 0x000000007E183C7EUL,
	0x1818187E3C180000UL, 0x0000000018181818UL, 0x1818181818180000UL, 0x00000000183C7E18UL, 0x7F30180000000000UL, 0x0000000000001830UL, 0x7F060C0000000000UL, 0x0000000000000C06UL,
	0x0303000000000000UL, 0x0000000000007F03UL, 0xFF66240000000000UL, 0x0000000000002466UL, 0x3E1C1C0800000000UL, 0x00000000007F7F3EUL, 0x3E3E7F7F00000000UL, 0x0000000000081C1CUL,
	0x0000000000000000UL, 0x0000000000000000UL, 0x18183C3C3C180000UL, 0x0000000018180018UL, 0x0000002466666600UL, 0x0000000000000000UL, 0x36367F3636000000UL, 0x0000000036367F36UL,
	0x603E0343633E1818UL, 0x000018183E636160UL, 0x1830634300000000UL, 0x000000006163060CUL, 0x3B6E1C36361C0000UL, 0x000000006E333333UL, 0x000000060C0C0C00UL, 0x0000000000000000UL,
	0x0C0C0C0C18300000UL, 0x0000000030180C0CUL, 0x30303030180C0000UL, 0x000000000C183030UL, 0xFF3C660000000000UL, 0x000000000000663CUL, 0x7E18180000000000UL, 0x0000000000001818UL,
	0x0000000000000000UL, 0x0000000C18181800UL, 0x7F00000000000000UL, 0x0000000000000000UL, 0x0000000000000000UL, 0x0000000018180000UL, 0x1830604000000000UL, 0x000000000103060CUL,
	0xDBDBC3C3663C0000UL, 0x000000003C66C3C3UL, 0x1818181E1C180000UL, 0x000000007E181818UL, 0x0C183060633E0000UL, 0x000000007F630306UL, 0x603C6060633E0000UL, 0x000000003E636060UL,
	0x7F33363C38300000UL, 0x0000000078303030UL, 0x603F0303037F0000UL, 0x000000003E636060UL, 0x633F0303061C0000UL, 0x000000003E636363UL, 0x18306060637F0000UL, 0x000000000C0C0C0CUL,
	0x633E6363633E0000UL, 0x000000003E636363UL, 0x607E6363633E0000UL, 0x000000001E306060UL, 0x0000181800000000UL, 0x0000000000181800UL, 0x0000181800000000UL, 0x000000000C181800UL,
	0x060C183060000000UL, 0x000000006030180CUL, 0x00007E0000000000UL, 0x000000000000007EUL, 0x6030180C06000000UL, 0x00000000060C1830UL, 0x18183063633E0000UL, 0x0000000018180018UL,
	0x7B7B63633E000000UL, 0x000000003E033B7BUL, 0x7F6363361C080000UL, 0x0000000063636363UL, 0x663E6666663F0000UL, 0x000000003F666666UL, 0x03030343663C0000UL, 0x000000003C664303UL,
	0x66666666361F0000UL, 0x000000001F366666UL, 0x161E1646667F0000UL, 0x000000007F664606UL, 0x161E1646667F0000UL, 0x000000000F060606UL, 0x7B030343663C0000UL, 0x000000005C666363UL,
	0x637F636363630000UL, 0x0000000063636363UL, 0x18181818183C0000UL, 0x000000003C181818UL, 0x3030303030780000UL, 0x000000001E333333UL, 0x1E1E366666670000UL, 0x0000000067666636UL,
	0x06060606060F0000UL, 0x000000007F664606UL, 0xC3DBFFFFE7C30000UL, 0x00000000C3C3C3C3UL, 0x737B7F6F67630000UL, 0x0000000063636363UL, 0x63636363633E0000UL, 0x000000003E636363UL,
	0x063E6666663F0000UL, 0x000000000F060606UL, 0x63636363633E0000UL, 0x000070303E7B6B63UL, 0x363E6666663F0000UL, 0x0000000067666666UL, 0x301C0663633E0000UL, 0x000000003E636360UL,
	0x18181899DBFF0000UL, 0x000000003C181818UL, 0x6363636363630000UL, 0x000000003E636363UL, 0xC3C3C3C3C3C30000UL, 0x00000000183C66C3UL, 0xDBC3C3C3C3C30000UL, 0x000000006666FFDBUL,
	0x18183C66C3C30000UL, 0x00000000C3C3663CUL, 0x183C66C3C3C30000UL, 0x000000003C181818UL, 0x0C183061C3FF0000UL, 0x00000000FFC38306UL, 0x0C0C0C0C0C3C0000UL, 0x000000003C0C0C0CUL,
	0x1C0E070301000000UL, 0x0000000040607038UL, 0x30303030303C0000UL, 0x000000003C303030UL, 0x0000000063361C08UL, 0x0000000000000000UL, 0x0000000000000000UL, 0x0000FF0000000000UL,
	0x0000000000180C0CUL, 0x0000000000000000UL, 0x3E301E0000000000UL, 0x000000006E333333UL, 0x66361E0606070000UL, 0x000000003E666666UL, 0x03633E0000000000UL, 0x000000003E630303UL,
	0x33363C3030380000UL, 0x000000006E333333UL, 0x7F633E0000000000UL, 0x000000003E630303UL, 0x060F0626361C0000UL, 0x000000000F060606UL, 0x33336E0000000000UL, 0x001E33303E333333UL,
	0x666E360606070000UL, 0x0000000067666666UL, 0x18181C0018180000UL, 0x000000003C181818UL, 0x6060700060600000UL, 0x003C666660606060UL, 0x1E36660606070000UL, 0x000000006766361EUL,
	0x18181818181C0000UL, 0x000000003C181818UL, 0xDBFF670000000000UL, 0x00000000DBDBDBDBUL, 0x66663B0000000000UL, 0x0000000066666666UL, 0x63633E0000000000UL, 0x000000003E636363UL,
	0x66663B0000000000UL, 0x000F06063E666666UL, 0x33336E0000000000UL, 0x007830303E333333UL, 0x666E3B0000000000UL, 0x000000000F060606UL, 0x06633E0000000000UL, 0x000000003E63301CUL,
	0x0C0C3F0C0C080000UL, 0x00000000386C0C0CUL, 0x3333330000000000UL, 0x000000006E333333UL, 0xC3C3C30000000000UL, 0x00000000183C66C3UL, 0xC3C3C30000000000UL, 0x0000000066FFDBDBUL,
	0x3C66C30000000000UL, 0x00000000C3663C18UL, 0x6363630000000000UL, 0x001F30607E636363UL, 0x18337F0000000000UL, 0x000000007F63060CUL, 0x180E181818700000UL, 0x0000000070181818UL,
	0x1800181818180000UL, 0x0000000018181818UL, 0x18701818180E0000UL, 0x000000000E181818UL, 0x000000003B6E0000UL, 0x0000000000000000UL, 0x63361C0800000000UL, 0x00000000007F6363UL,
};
// clang-format on

typedef struct Glyph {
    i32 x, y, w, h;
    i32 advance;
} Glyph;

typedef struct Font {
    Glyph ascii[128];
    i32   lineAdvance;
    u8*   atlas;
    i32   atlasW, atlasH;
} Font;

function void
initFont(Font* font, Arena* arena) {
    i32 chWidth = 8;
    i32 chHeight = 16;
    i32 chCount = 128;
    font->atlasW = chCount * chWidth;
    font->atlasH = chHeight;
    font->lineAdvance = chHeight;
    font->atlas = arenaAllocArray(arena, u8, font->atlasW * font->atlasH);
    for (u8 ch = 0; ch < chCount; ch++) {
        Glyph* glyph = font->ascii + ch;
        glyph->x = (i32)ch * chWidth;
        glyph->y = 0;
        glyph->w = chWidth;
        glyph->h = chHeight;
        glyph->advance = chWidth;

        u8* glyphBitmap = (u8*)globalFontData + ch * glyph->h;
        for (isize row = 0; row < glyph->h; row++) {
            u8 glyphRowBitmap = glyphBitmap[row];
            for (isize col = 0; col < glyph->w; col++) {
                u8    mask = 1 << col;
                u8    glyphRowMasked = glyphRowBitmap & mask;
                isize atlasIndex = (glyph->y + row) * font->atlasW + (glyph->x + col);
                font->atlas[atlasIndex] = 0;
                if (glyphRowMasked) {
                    font->atlas[atlasIndex] = 0xFF;
                }
            }
        }
    }
}

function void
swDrawGlyph(Font* font, Glyph glyph, Texture dest, i32 x, i32 y, Color01 color) {
    for (i32 row = 0; row < glyph.h; row++) {
        i32 destRow = y + row;
        assert(destRow < dest.height);
        i32 srcRow = glyph.y + row;
        for (i32 col = 0; col < glyph.w; col++) {
            i32 destCol = x + col;
            assert(destCol < dest.width);
            i32 srcCol = glyph.x + col;

            i32     atlasIndex = srcRow * font->atlasW + srcCol;
            u8      alpha = font->atlas[atlasIndex];
            Color01 thisColor = color;
            thisColor.a *= ((f32)alpha) / 255.0f;

            i32      destIndex = destRow * dest.pitch + destCol;
            u32      old32 = dest.ptr[destIndex];
            Color255 old255 = coloru32to255(old32);
            Color01  old01 = color255to01(old255);

            Color01 new01 = {
                .r = lerpf(old01.r, thisColor.r, thisColor.a),
                .g = lerpf(old01.g, thisColor.g, thisColor.a),
                .b = lerpf(old01.b, thisColor.b, thisColor.a),
                .a = 1,  // NOTE(khvorov) Assuming dest texture won't be blended further
            };

            Color255 new255 = color01to255(new01);
            u32      new32 = color255tou32(new255);
            dest.ptr[destIndex] = new32;
        }
    }
}

function i32
swDrawStr(Font* font, Str str, Texture dest, i32 left, i32 top, Color01 color) {
    i32 curX = left;
    for (isize ind = 0; ind < str.len; ind++) {
        char  ch = str.ptr[ind];
        Glyph glyph = font->ascii[(u8)ch];
        swDrawGlyph(font, glyph, dest, curX, top, color);
        curX += glyph.advance;
    }
    return font->lineAdvance;
}

//
// SECTION Misc
//

typedef enum IterResult {
    IterResult_NoMore,
    IterResult_More,
} IterResult;

typedef struct CircleIter {
    isize mostRecentIndex;
    isize totalEntries;
    isize windowSize;
    isize iterCount;
    isize currentIndex;
} CircleIter;

function void
circleIterSetMostRecent(CircleIter* iter, isize newMostRecent) {
    if (newMostRecent < 0) {
        newMostRecent = iter->totalEntries + newMostRecent;
    }
    if (newMostRecent >= iter->totalEntries) {
        newMostRecent = iter->totalEntries - newMostRecent;
    }
    assert(newMostRecent >= 0 && newMostRecent < iter->totalEntries);
    iter->mostRecentIndex = newMostRecent;
}

function CircleIter
createCircleIter(isize mostRecentIndex, isize totalEntries, isize windowSize) {
    assert(windowSize <= totalEntries && windowSize >= 0);
    CircleIter iter = {.totalEntries = totalEntries, .windowSize = windowSize};
    circleIterSetMostRecent(&iter, mostRecentIndex);
    return iter;
}

function IterResult
circleIterNext(CircleIter* iter) {
    IterResult result = IterResult_NoMore;
    if (iter->iterCount < iter->windowSize) {
        result = IterResult_More;

        isize firstIndex = (iter->mostRecentIndex + 1) - iter->windowSize;
        if (firstIndex < 0) {
            firstIndex = iter->totalEntries + firstIndex;
        }

        isize thisIndex = firstIndex + iter->iterCount;
        if (thisIndex >= iter->totalEntries) {
            thisIndex -= iter->totalEntries;
        }
        assert(thisIndex >= 0 && thisIndex < iter->totalEntries);
        iter->currentIndex = thisIndex;

        iter->iterCount += 1;
    }
    return result;
}

typedef struct LagCircleIter {
    CircleIter circle;
    isize      lag;
    isize      timesAdvanced;
} LagCircleIter;

function void
lagCircleIterResetAndSetMostRecent(LagCircleIter* iter, isize newMostRecent) {
    iter->circle.iterCount = 0;
    iter->timesAdvanced += 1;
    if (iter->timesAdvanced == iter->lag) {
        iter->timesAdvanced = 0;
        circleIterSetMostRecent(&iter->circle, newMostRecent);
    }
}

//
// SECTION Tests
//

function void
runTests(Arena* arena) {
    TempMemory temp = beginTempMemory(arena);

    {
        assert(!isPowerOf2(0));
        assert(!isPowerOf2(3));
        assert(!isPowerOf2(123));
        assert(!isPowerOf2(6));

        assert(isPowerOf2(1));
        assert(isPowerOf2(2));
        assert(isPowerOf2(4));
        assert(isPowerOf2(8));
    }

    {
        assert(getOffsetForAlignment((void*)1, 1) == 0);
        assert(getOffsetForAlignment((void*)2, 1) == 0);
        assert(getOffsetForAlignment((void*)3, 1) == 0);
        assert(getOffsetForAlignment((void*)4, 1) == 0);

        assert(getOffsetForAlignment((void*)11, 2) == 1);
        assert(getOffsetForAlignment((void*)13, 4) == 3);
    }

    u64 guardBeforeValue = 0xAAAAAAAAULL;
    u64 guardBetweenValue = 0xBBBBBBBBULL;
    u64 guardAfterValue = 0xCCCCCCCCULL;

    {
        u64* guardBefore = (u64*)arenaAlloc(arena, sizeof(u64), 1);
        guardBefore[0] = guardBeforeValue;

        isize bytes = 255;
        void* ptr = arenaAlloc(arena, bytes, 64);

        u64* guardAfter = (u64*)arenaAlloc(arena, sizeof(u64), 1);
        guardAfter[0] = guardAfterValue;

        for (isize misalign = 0; misalign < 254; misalign++) {
            isize thisBytes = bytes - misalign;
            void* thisPtr = (u8*)ptr + misalign;

            for (isize byteIndex = 0; byteIndex < thisBytes; byteIndex++) {
                u8 val = (u8)(byteIndex & 0xFF);
                ((u8*)thisPtr)[byteIndex] = val;
            }

            zeromem(thisPtr, thisBytes);
            assert(guardBefore[0] == guardBeforeValue);
            assert(guardAfter[0] == guardAfterValue);

            for (isize byteIndex = 0; byteIndex < thisBytes; byteIndex++) {
                assert(((u8*)thisPtr)[byteIndex] == 0);
            }
        }
    }

    {
        u64* guardBefore = (u64*)arenaAlloc(arena, sizeof(u64), 1);
        guardBefore[0] = guardBeforeValue;

        isize bufBytes = 255;
        void* src = arenaAlloc(arena, bufBytes, 64);

        u64* guardBetween = (u64*)arenaAlloc(arena, sizeof(u64), 1);
        guardBetween[0] = guardBetweenValue;

        void* dest = arenaAlloc(arena, bufBytes, 64);
        zeromem(dest, bufBytes);

        u64* guardAfter = (u64*)arenaAlloc(arena, sizeof(u64), 1);
        guardAfter[0] = guardAfterValue;

        for (isize byteIndex = 0; byteIndex < bufBytes; byteIndex++) {
            u8 val = (u8)(byteIndex & 0xFF);
            ((u8*)src)[byteIndex] = val;
        }

        for (isize misalign = 0; misalign < 254; misalign++) {
            isize thisBufBytes = bufBytes - misalign;
            void* thisSrc = (u8*)src + misalign;
            void* thisDest = dest;

            zeromem(thisDest, thisBufBytes);
            assert(!memeq(thisSrc, thisDest, thisBufBytes));
            memcpy_(thisDest, thisSrc, thisBufBytes);
            assert(memeq(thisSrc, thisDest, thisBufBytes));

            assert(guardBefore[0] == guardBeforeValue);
            assert(guardBetween[0] == guardBetweenValue);
            assert(guardAfter[0] == guardAfterValue);
        }

        for (isize misalign = 0; misalign < 254; misalign++) {
            isize thisBufBytes = bufBytes - misalign;
            void* thisSrc = src;
            void* thisDest = (u8*)dest + misalign;

            zeromem(thisDest, thisBufBytes);
            assert(!memeq(thisSrc, thisDest, thisBufBytes));
            memcpy_(thisDest, thisSrc, thisBufBytes);
            assert(memeq(thisSrc, thisDest, thisBufBytes));

            assert(guardBefore[0] == guardBeforeValue);
            assert(guardBetween[0] == guardBetweenValue);
            assert(guardAfter[0] == guardAfterValue);
        }

        for (isize misalign = 0; misalign < 254; misalign++) {
            isize thisBufBytes = bufBytes - misalign;
            void* thisSrc = (u8*)src + misalign;
            void* thisDest = (u8*)dest + misalign;

            zeromem(thisDest, thisBufBytes);
            assert(!memeq(thisSrc, thisDest, thisBufBytes));
            memcpy_(thisDest, thisSrc, thisBufBytes);
            assert(memeq(thisSrc, thisDest, thisBufBytes));

            assert(guardBefore[0] == guardBeforeValue);
            assert(guardBetween[0] == guardBetweenValue);
            assert(guardAfter[0] == guardAfterValue);
        }
    }

    {
        f32 before = 1234.5678;
        f32 after = 5678.1234;
        f32 floats[] = {
            before,
            1,
            2,
            3,
            4,
            5,
            6,
            7,
            8,
            1,
            2,
            3,
            4,
            5,
            6,
            7,
            8,
            1,
            2,
            3,
            4,
            5,
            6,
            7,
            after,
        };

        f32 val = 111.222;
        fltset(floats + 1, val, arrayCount(floats) - 2);
        assert(floats[0] == before);
        for (isize ind = 1; ind < arrayCount(floats) - 1; ind++) {
            assert(floats[ind] == val);
        }
        assert(floats[arrayCount(floats) - 1] == after);
    }

    {
        Arena arena = {.base = (void*)10, .size = 100};
        assert(arenaAlloc(&arena, 15, 1) == (void*)10);
        assert(arena.base == (void*)10);
        assert(arena.size == 100);
        assert(arena.used == 15);
        assert(arenaFreePtr(&arena) == (void*)25);
        assert(arenaFreeSize(&arena) == 85);

        assert(arenaAlloc(&arena, 16, 4) == (void*)28);
        assert(arena.base == (void*)10);
        assert(arena.size == 100);
        assert(arena.used == 34);
        assert(arenaFreePtr(&arena) == (void*)44);
        assert(arenaFreeSize(&arena) == 66);

        TempMemory temp = beginTempMemory(&arena);
        assert(arena.tempCount == 1);
        arenaAlloc(&arena, 10, 1);
        assert(arena.used == 44);
        endTempMemory(temp);
        assert(arena.used == 34);
        assert(arena.tempCount == 0);
    }

    {
        assert(squaref(5) == 25);
        assert(lerpf(5, 15, 0.3) == 8);

        assert(v2feq(v2fadd((V2f) {1, 2}, (V2f) {3, -5}), (V2f) {4, -3}));
        assert(v3feq(v3fadd((V3f) {.x = 1, .y = 2, .z = 3}, (V3f) {.x = 3, .y = -5, .z = 10}), (V3f) {.x = 4, .y = -3, .z = 13}));

        assert(v2feq(v2fsub((V2f) {1, 2}, (V2f) {3, -5}), (V2f) {-2, 7}));
        assert(v3feq(v3fsub((V3f) {.x = 1, .y = 2, .z = 3}, (V3f) {.x = 3, .y = -5, .z = 10}), (V3f) {.x = -2, .y = 7, .z = -7}));

        assert(v2feq(v2fhadamard((V2f) {1, 2}, (V2f) {3, 4}), (V2f) {3, 8}));
        assert(v3feq(v3fhadamard((V3f) {.x = 1, .y = 2, .z = 3}, (V3f) {.x = 3, .y = 4, .z = 5}), (V3f) {.x = 3, .y = 8, .z = 15}));

        assert(v2feq(v2fscale((V2f) {1, 2}, 5), (V2f) {5, 10}));
        assert(v3feq(v3fscale((V3f) {.x = 1, .y = 2, .z = 3}, 5), (V3f) {.x = 5, .y = 10, .z = 15}));

        assert(v2fdot((V2f) {1, 2}, (V2f) {4, 5}) == 14);
        assert(v3fdot((V3f) {.x = 1, .y = 2, .z = 3}, (V3f) {.x = 4, .y = 5, .z = 6}) == 32);
    }

    {
        f32 turns[] = {-1, -0.2, -0.5, -0.7, 0, 0.1, 0.5, 0.9, 1};
        f32 expectedSin[] = {2.44921270764475e-16, -0.951056516295154, -1.22460635382238e-16, 0.951056516295154, 0, 0.587785252292473, 1.22460635382238e-16, -0.587785252292473, -2.44921270764475e-16};
        for (i32 turnInd = 0; turnInd < arrayCount(turns); turnInd++) {
            f32 turn = turns[turnInd];

            f32 got = sinef(turn);
            f32 expected = expectedSin[turnInd];

            assert(feqEplislon(got, expected, 0.01));
        }
    }

    {
        f32 tanVals[] = {-1, -0.2, -0.5, -0.7, 0, 0.1, 0.5, 0.9, 1};
        f32 expectedTurns[] = {-0.125, -0.0314164790945006, -0.0737918088252166, -0.0972000561071074, 0, 0.0158627587152768, 0.0737918088252166, 0.116631145821713, 0.125};
        for (i32 turnInd = 0; turnInd < arrayCount(tanVals); turnInd++) {
            f32 turn = tanVals[turnInd];

            f32 got = arctanf(turn);
            f32 expected = expectedTurns[turnInd];

            assert(feqEplislon(got, expected, 0.01));
        }
    }

    {
        assert(rotor3fEq(rotor3fNormalise((Rotor3f) {4, 4, 4, 4}), (Rotor3f) {0.5, 0.5, 0.5, 0.5}));
        assert(rotor3fEq(rotor3fReverse((Rotor3f) {1, 2, 3, 4}), (Rotor3f) {1, -2, -3, -4}));

        {
            V3f result = rotor3fRotateV3f(createRotor3fAnglePlane(0.25, 1, 0, 0), (V3f) {.x = 1, .y = 0, .z = 0});
            assert(feqEplislon(result.y, 1, 0.01));
        }
        assert(feqEplislon(rotor3fRotateV3f(createRotor3fAnglePlane(0.25, -1, 0, 0), (V3f) {.x = 1, .y = 0, .z = 0}).y, -1, 0.01));

        {
            Rotor3f r1 = createRotor3fAnglePlane(0.1, 1, 0, 0);
            Rotor3f r2 = createRotor3fAnglePlane(0.15, 1, 0, 0);
            Rotor3f rmul = rotor3fMulRotor3f(r1, r2);
            V3f     vrot = rotor3fRotateV3f(rmul, (V3f) {.x = 1, .y = 0, .z = 0});
            assert(feqEplislon(vrot.y, 1, 0.01));
        }
    }

    {
        Rect2f rect = rect2fCenterDim((V2f) {1, 2}, (V2f) {4, 8});
        assert(v2feq(rect.topleft, (V2f) {-1, -2}));
        assert(v2feq(rect.dim, (V2f) {4, 8}));
    }

    {
        Rect2f rect = {{5, 5}, {10, 200}};
        Rect2f clip = {{0, 0}, {20, 10}};
        Rect2f clipped = rect2fClip(rect, clip);
        assert(rect2fEq(clipped, (Rect2f) {{5, 5}, {10, 5}}));
    }

    {
        assert(color01eq(color01add((Color01) {1, 2, 3, 4}, (Color01) {5, 6, 7, 8}), (Color01) {6, 8, 10, 12}));
        assert(color01eq(color01scale((Color01) {1, 2, 3, 4}, 2), (Color01) {2, 4, 6, 8}));
    }

    {
        assert(edgeWedge((V2f) {0, 0}, (V2f) {20, 0}, (V2f) {10, 10}) > 0);
        assert(edgeWedge((V2f) {0, 0}, (V2f) {20, 0}, (V2f) {-10, -10}) < 0);
        assert(edgeWedge((V2f) {0, 0}, (V2f) {20, 0}, (V2f) {10, 0}) == 0);
    }

    {
        assert(isTopLeft((V2f) {0, 0}, (V2f) {10, 0}));
        assert(!isTopLeft((V2f) {0, 0}, (V2f) {10, 1}));
        assert(isTopLeft((V2f) {100, 100}, (V2f) {0, 0}));
        assert(isTopLeft((V2f) {100, 100}, (V2f) {200, 0}));
    }

    {
        MeshStorage store = createMeshStorage(arena, Megabyte);

        Mesh cube1 = createCubeMesh(&store, 2, (V3f) {}, createRotor3f());
        assert(store.vertices.len == cube1.vertices.len);
        assert(store.indices.len == cube1.indices.len);
        assert(v3feq(cube1.vertices.ptr[0], (V3f) {.x = -1, .y = 1, .z = -1}));
        assert(cube1.indices.ptr[0].i1 == 0);

        Mesh cube2 = createCubeMesh(&store, 4, (V3f) {}, createRotor3f());
        assert(store.vertices.len == cube1.vertices.len + cube2.vertices.len);
        assert(store.indices.len == cube1.indices.len + cube2.indices.len);
        assert(cube2.vertices.ptr == cube1.vertices.ptr + cube1.vertices.len);
        assert(cube2.indices.ptr == cube1.indices.ptr + cube1.indices.len);
        assert(v3feq(cube2.vertices.ptr[0], (V3f) {.x = -2, .y = 2, .z = -2}));
        assert(cube2.indices.ptr[0].i1 == 0);
    }

    {
        StrBuilder builder = {};
        arenaAllocCap(arena, char, 1000, builder);

        fmtStr(&builder, STR("test"));
        assert(streq(builder.str, STR("test")));

        fmtStr(&builder, STR(" and 2 "));
        assert(streq(builder.str, STR("test and 2 ")));

        fmtInt(&builder, 123, (FmtInt) {.chars = 3});
        assert(streq(builder.str, STR("test and 2 123")));
        fmtStr(&builder, STR(" "));

        fmtInt(&builder, 0, (FmtInt) {.chars = 1});
        assert(streq(builder.str, STR("test and 2 123 0")));
        fmtStr(&builder, STR(" "));

        fmtF32(&builder, 123.4567, (FmtF32) {.charsLeft = 3, .charsRight = 2});
        assert(streq(builder.str, STR("test and 2 123 0 123.46")));

        fmtNull(&builder);
        assert(builder.ptr[builder.len - 1] == '\0');

        builder.len = 0;
        fmtInt(&builder, 123, (FmtInt) {.chars = 5, .align = FmtAlign_Left});
        assert(streq(builder.str, STR("123  ")));

        builder.len = 0;
        fmtF32(&builder, 123.0f, (FmtF32) {.charsLeft = 2, .charsRight = 2});
        assert(streq(builder.str, STR("99.9+")));

        builder.len = 0;
        fmtInt(&builder, -123, (FmtInt) {.chars = 5, .align = FmtAlign_Right});
        assert(streq(builder.str, STR(" -123")));
    }

    endTempMemory(temp);
}

//
// SECTION Bench
//

function void
runBench(Arena* arena) {
    TempMemory runBenchTemp = beginTempMemory(arena);
    zeromem(arenaFreePtr(arena), arenaFreeSize(arena));
#if TRIAXIS_profile
    isize samples = 10;
#else
    isize samples = 1;
#endif

    {
        isize toCopy = arenaFreeSize(arena) / 2 - 1024;

        for (isize ind = 0; ind < samples; ind++) {
            TempMemory temp = beginTempMemory(arena);

            u8* arr1 = (u8*)arenaAlloc(arena, toCopy, 64);
            u8* arr2 = (u8*)arenaAlloc(arena, toCopy, 64);

            timedSectionStart("bench copymem");
            memcpy_(arr1, arr2, toCopy);
            timedSectionEnd();

            endTempMemory(temp);
        }
    }

    {
        isize toZero = arenaFreeSize(arena);

        for (isize ind = 0; ind < samples; ind++) {
            TempMemory temp = beginTempMemory(arena);

            timedSectionStart("bench zeromem");
            zeromem(arenaFreePtr(arena), toZero);
            timedSectionEnd();

            endTempMemory(temp);
        }
    }

    endTempMemory(runBenchTemp);
}

//
// SECTION App
//

typedef struct State {
    SWRenderer  swRenderer;
    MeshStorage meshStorage;
    Font        font;
    Arena       perm;
    Arena       scratch;

    isize windowWidth;
    isize windowHeight;

    Camera camera;
    Input  input;

    struct {
        Mesh* ptr;
        isize len;
        isize cap;
    } meshes;

    bool useSW;

    bool showDebugUI;

    struct nk_context   ui;
    struct nk_user_font uifont;
} State;

function float
initState_uifontTextWidthCalc(nk_handle handle, float height, const char* text, int len) {
    unused(height);
    unused(text);
    Font* font = (Font*)handle.ptr;
    // NOTE(khvorov) Assume all characters are the same width
    float result = len * font->ascii->w;
    return result;
}

function State*
initState(void* mem, isize bytes) {
    assert(mem);
    assert(bytes > 0);

    Arena arena = {.base = mem, .size = bytes};

#ifdef TRIAXIS_profile
    {
        Arena spallArena = createArenaFromArena(&arena, 100 * Megabyte);
        globalSpallBuffer = (SpallBuffer) {.data = spallArena.base, .length = spallArena.size};
    }
#else
    unused(rdtscFreqPerMicrosecond);
#endif

#ifdef TRIAXIS_tests
    runTests(&arena);
#endif
#ifdef TRIAXIS_bench
    runBench(&arena);
#endif

    State* state = arenaAllocArray(&arena, State, 1);

    initFont(&state->font, &arena);
    state->scratch = createArenaFromArena(&arena, 10 * Megabyte);
    state->perm = createArenaFromArena(&arena, 10 * Megabyte);

    {
        state->uifont.userdata.ptr = &state->font;
        state->uifont.height = state->font.lineAdvance;
        state->uifont.width = initState_uifontTextWidthCalc;

        isize nkMemSize = 16 * Megabyte;
        u8*   nkmem = arenaAllocArray(&arena, u8, nkMemSize);
        nk_init_fixed(&state->ui, nkmem, nkMemSize, &state->uifont);
    }

    isize perSystem = arenaFreeSize(&arena) / 3;
    state->swRenderer = createSWRenderer(&arena, perSystem);
    state->meshStorage = createMeshStorage(&arena, perSystem);
    arenaAllocCap(&arena, Mesh, perSystem, state->meshes);

    state->windowWidth = 1600;
    state->windowHeight = 800;

    state->camera = createCamera((V3f) {.x = 0, .y = 1, .z = 0}, (f32)state->windowWidth, (f32)state->windowHeight);
    state->input = (Input) {};

    arrpush(state->meshes, createCubeMesh(&state->meshStorage, 1, (V3f) {.x = 1, .y = 0, .z = 3}, createRotor3fAnglePlane(0, 1, 0, 0)));
    arrpush(state->meshes, createCubeMesh(&state->meshStorage, 1, (V3f) {.x = -1, .y = 0, .z = 3}, createRotor3fAnglePlane(0, 0, 1, 0)));

    // NOTE(khvorov) Ground
    {
        MeshBuilder builder = beginMesh(&state->meshStorage);

        V3f backLeft = {.x = -100, .z = 100};
        V3f backRight = {.x = 100, .z = 100};
        V3f frontRight = {.x = 100, .z = -100};
        V3f frontLeft = {.x = -100, .z = -100};

        i32 backLeftIndex = arrpush(state->meshStorage.vertices, backLeft);
        i32 backRightIndex = arrpush(state->meshStorage.vertices, backRight);
        i32 frontRightIndex = arrpush(state->meshStorage.vertices, frontRight);
        i32 frontLeftIndex = arrpush(state->meshStorage.vertices, frontLeft);

        Color01 color = {.r = 0.1, .g = 0.15, .b = 0.2, .a = 1};
        arrpush(state->meshStorage.colors, color);
        arrpush(state->meshStorage.colors, color);
        arrpush(state->meshStorage.colors, color);
        arrpush(state->meshStorage.colors, color);

        meshStorageAddQuad(&state->meshStorage, backLeftIndex, backRightIndex, frontRightIndex, frontLeftIndex);
        Mesh ground = endMesh(builder, (V3f) {}, createRotor3f());
        arrpush(state->meshes, ground);
    }

    state->showDebugUI = true;
    return state;
}

function void
update(State* state, f32 deltaSec) {
    timedSectionStart(__FUNCTION__);

    // NOTE(khvorov) Misc
    {
        if (inputKeyWasPressed(&state->input, InputKey_ToggleDebugTriangles)) {
            state->swRenderer.showDebugTriangles = !state->swRenderer.showDebugTriangles;
        }
        if (inputKeyWasPressed(&state->input, InputKey_ToggleOutlines)) {
            state->swRenderer.showOutlines = !state->swRenderer.showOutlines;
        }
        if (inputKeyWasPressed(&state->input, InputKey_ToggleSW)) {
            state->useSW = !state->useSW;
        }
        if (inputKeyWasPressed(&state->input, InputKey_ToggleDebugUI)) {
            state->showDebugUI = !state->showDebugUI;
        }
    }

    // NOTE(khvorov) Move camera
    {
        f32 moveInc = state->camera.moveWUPerSec;
        if (state->input.keys[InputKey_MoveFaster].down) {
            moveInc = state->camera.moveWUPerSecAccelerated;
        }
        moveInc *= deltaSec;

        V3f cameraMoveInCameraSpace = {};
        if (state->input.keys[InputKey_Forward].down) {
            cameraMoveInCameraSpace.z += 1;
        }
        if (state->input.keys[InputKey_Back].down) {
            cameraMoveInCameraSpace.z -= 1;
        }
        if (state->input.keys[InputKey_Right].down) {
            cameraMoveInCameraSpace.x += 1;
        }
        if (state->input.keys[InputKey_Left].down) {
            cameraMoveInCameraSpace.x -= 1;
        }
        if (state->input.keys[InputKey_Up].down) {
            cameraMoveInCameraSpace.y += 1;
        }
        if (state->input.keys[InputKey_Down].down) {
            cameraMoveInCameraSpace.y -= 1;
        }

        V3f cameraMoveInWorldSpace = rotor3fRotateV3f(state->camera.currentOrientation, cameraMoveInCameraSpace);
        V3f cameraMoveNorm = v3fnormalise(cameraMoveInWorldSpace);
        V3f cameraMoveScaled = v3fscale(cameraMoveNorm, moveInc);

        state->camera.pos = v3fadd(state->camera.pos, cameraMoveScaled);
    }

    // NOTE(khvorov) Camera rotation (key-based)
    {
        f32 xy = 0;
        f32 xz = 0;
        f32 yz = 0;
        if (state->input.keys[InputKey_RotateXY].down) {
            xy += 1;
        }
        if (state->input.keys[InputKey_RotateYX].down) {
            xy -= 1;
        }
        if (state->input.keys[InputKey_RotateXZ].down) {
            xz += 1;
        }
        if (state->input.keys[InputKey_RotateZX].down) {
            xz -= 1;
        }
        if (state->input.keys[InputKey_RotateYZ].down) {
            yz += 1;
        }
        if (state->input.keys[InputKey_RotateZY].down) {
            yz -= 1;
        }

        if (xy != 0 || xz != 0 || yz != 0) {
            f32     rotInc = state->camera.rotTurnsPerSec * deltaSec;
            Rotor3f rot = createRotor3fAnglePlane(rotInc, xy, xz, yz);
            state->camera.targetOrientation = rotor3fMulRotor3f(state->camera.targetOrientation, rot);
        }
    }

    // NOTE(khvorov) Camera rotation (mouse-based)
    if (!state->showDebugUI) {
        f32 xy = 0;
        f32 xz = -(f32)state->input.mouse.dx;
        f32 yz = (f32)state->input.mouse.dy;
        if (xy != 0 || xz != 0 || yz != 0) {
            V2f mouseSens = {0.25, 0.25};  // TODO(khvorov) Config
            V2f mouseMove = v2fhadamard(mouseSens, (V2f) {(f32)state->input.mouse.dx, (f32)state->input.mouse.dy});
            f32 mouseMoveCoef = v2flen(mouseMove);
            f32 rotInc = mouseMoveCoef * deltaSec;

            // NOTE(khvorov) XZ rotations happen around the world's xz
            if (xy != 0 || yz != 0) {
                Rotor3f rot = createRotor3fAnglePlane(rotInc, xy, 0, yz);
                state->camera.targetOrientation = rotor3fMulRotor3f(state->camera.targetOrientation, rot);
            }
            if (xz != 0) {
                Rotor3f rot = createRotor3fAnglePlane(rotInc, 0, xz, 0);
                state->camera.targetOrientation = rotor3fMulRotor3f(rot, state->camera.targetOrientation);
            }
        }
    }

    // NOTE(khvorov) Smooth camera update
    {
        f32 smoothUpdateCoef = 0.25f;  // TODO(khvorov) Config
        state->camera.currentOrientation = rotor3fLerpN(state->camera.currentOrientation, state->camera.targetOrientation, smoothUpdateCoef);
    }

    // TODO(khvorov) Entity update?
    if (false) {
        Rotor3f cubeRotation = createRotor3fAnglePlane(0.2 * deltaSec, 1, 1, 1);
        for (isize ind = 0; ind < 2; ind++) {
            Mesh* mesh = state->meshes.ptr + ind;
            mesh->orientation = rotor3fMulRotor3f(mesh->orientation, cubeRotation);
            cubeRotation = rotor3fReverse(cubeRotation);
        }
    }

    // NOTE(khvorov) Debug UI
    if (state->showDebugUI) {
        if (nk_begin(&state->ui, "debugui", nk_rect(0, 0, 500, 500), 0)) {
            StrBuilder builder = {.ptr = (char*)arenaFreePtr(&state->scratch), .cap = arenaFreeSize(&state->scratch)};

            nk_layout_row_static(&state->ui, state->font.lineAdvance, 100, 1);

            builder.len = 0;
            fmtStr(&builder, STR("mode: "));
            if (state->useSW) {
                fmtStr(&builder, STR("SW"));
            } else {
                fmtStr(&builder, STR("D3D11"));
            }
            nk_text(&state->ui, builder.ptr, builder.len, NK_TEXT_ALIGN_LEFT);

            nk_layout_row_static(&state->ui, state->font.lineAdvance, 5 * state->font.ascii->w, 4);

            nk_text(&state->ui, STRARG("pos"), NK_TEXT_ALIGN_CENTERED);
            nk_text(&state->ui, STRARG("x"), NK_TEXT_ALIGN_CENTERED);
            nk_text(&state->ui, STRARG("y"), NK_TEXT_ALIGN_CENTERED);
            nk_text(&state->ui, STRARG("z"), NK_TEXT_ALIGN_CENTERED);

            builder.len = 0;
            FmtF32 posFmt = {.charsLeft = 3, .charsRight = 1};
            fmtF32(&builder, state->camera.pos.x, posFmt);

            nk_text(&state->ui, STRARG("cmr"), NK_TEXT_ALIGN_CENTERED);
            nk_text(&state->ui, builder.ptr, builder.len, NK_TEXT_ALIGN_CENTERED);

            builder.len = 0;
            fmtF32(&builder, state->camera.pos.y, posFmt);
            nk_text(&state->ui, builder.ptr, builder.len, NK_TEXT_ALIGN_CENTERED);

            builder.len = 0;
            fmtF32(&builder, state->camera.pos.z, posFmt);
            nk_text(&state->ui, builder.ptr, builder.len, NK_TEXT_ALIGN_CENTERED);

            // TODO(khvorov) Frametime line
            {
                f32 values[] = {26.0f, 13.0f, 30.0f, 15.0f, 25.0f, 10.0f, 20.0f, 40.0f, 12.0f, 8.0f, 22.0f, 28.0f};
                nk_layout_row_dynamic(&state->ui, 150, 1);
                nk_chart_begin(&state->ui, NK_CHART_LINES, NK_LEN(values), 0, 50);
                for (isize i = 0; i < NK_LEN(values); ++i) {
                    nk_chart_push(&state->ui, values[i]);
                }
                nk_chart_end(&state->ui);
            }
        }
        nk_end(&state->ui);
    }

    timedSectionEnd();
}

function Color255
nkcolorTo255(struct nk_color c) {
    Color255 result = {.a = c.a, .r = c.r, .g = c.g, .b = c.b};
    return result;
}

#define MAX_TRI_BOX_INTERSECTION_VERTICES 9
typedef struct ClipPoly {
    V3f     vertices[MAX_TRI_BOX_INTERSECTION_VERTICES];
    Color01 colors[MAX_TRI_BOX_INTERSECTION_VERTICES];
    isize   count;
} ClipPoly;

typedef struct ClipPlane {
    V3f point;
    V3f normal;
} ClipPlane;

function void
swRender(State* state) {
    timedSectionStart(__FUNCTION__);

    meshStorageClearBuffers(&state->swRenderer.trisCamera);
    meshStorageClearBuffers(&state->swRenderer.trisScreen);
    if (state->swRenderer.showDebugTriangles) {
        swRendererDrawDebugTriangles(&state->swRenderer);
    } else {
        for (isize meshIndex = 0; meshIndex < state->meshes.len; meshIndex++) {
            Mesh mesh = state->meshes.ptr[meshIndex];
            swRendererPushMesh(&state->swRenderer, mesh, state->camera);
        }

        // NOTE(khvorov) Clip camera space tris
        timedSectionStart("sw clip");
        for (isize triIndex = 0; triIndex < state->swRenderer.trisCamera.indices.len; triIndex++) {
            TriangleIndices tri = state->swRenderer.trisCamera.indices.ptr[triIndex];

            V3f v1Camera = arrget(state->swRenderer.trisCamera.vertices, tri.i1);
            V3f v2Camera = arrget(state->swRenderer.trisCamera.vertices, tri.i2);
            V3f v3Camera = arrget(state->swRenderer.trisCamera.vertices, tri.i3);

            Color01 c1 = arrget(state->swRenderer.trisCamera.colors, tri.i1);
            Color01 c2 = arrget(state->swRenderer.trisCamera.colors, tri.i2);
            Color01 c3 = arrget(state->swRenderer.trisCamera.colors, tri.i3);

            ClipPoly poly = {
                .vertices[0] = v1Camera,
                .vertices[1] = v2Camera,
                .vertices[2] = v3Camera,

                .colors[0] = c1,
                .colors[1] = c2,
                .colors[2] = c3,

                .count = 3,
            };

            // NOTE(khvorov) Fov expansion for clipping is necessary to avoid clipping too early due to trig inaccuracies
            f32 fovExpansionCoeff = 1.01f;
            V2f halfFovExpanded = v2fscale(state->camera.halfFovTurns, fovExpansionCoeff);
            assert(halfFovExpanded.x < 0.25);
            assert(halfFovExpanded.y < 0.25);

            V3f     normalForward = {.z = 1};
            Rotor3f planesXZrot = createRotor3fAnglePlane(0.25 - halfFovExpanded.x, 0, 1, 0);
            Rotor3f planesYZrot = createRotor3fAnglePlane(0.25 - halfFovExpanded.y, 0, 0, 1);

            ClipPlane planes[] = {
                {(V3f) {.z = state->camera.nearClipZ}, normalForward},
                {(V3f) {.z = state->camera.farClipZ}, v3freverse(normalForward)},
                {(V3f) {}, rotor3fRotateV3f(rotor3fReverse(planesXZrot), normalForward)},
                {(V3f) {}, rotor3fRotateV3f(planesXZrot, normalForward)},
                {(V3f) {}, rotor3fRotateV3f(planesYZrot, normalForward)},
                {(V3f) {}, rotor3fRotateV3f(rotor3fReverse(planesYZrot), normalForward)},
            };

            for (isize planeIndex = 0; planeIndex < arrayCount(planes); planeIndex++) {
                ClipPlane plane = planes[planeIndex];
                ClipPoly  newPoly = {};

                for (isize vPairIndex = 0; vPairIndex < poly.count; vPairIndex++) {
                    isize index1 = vPairIndex;
                    isize index2 = (vPairIndex + 1) % poly.count;

                    V3f polyV1 = poly.vertices[index1];
                    V3f polyV2 = poly.vertices[index2];

                    Color01 polyC1 = poly.colors[index1];
                    Color01 polyC2 = poly.colors[index2];

                    V3f pointLine1 = v3fsub(polyV1, plane.point);
                    V3f pointLine2 = v3fsub(polyV2, plane.point);

                    f32 line1Normal = v3fdot(pointLine1, plane.normal);
                    f32 line2Normal = v3fdot(pointLine2, plane.normal);

                    if (line1Normal * line2Normal < 0) {
                        f32 line1NormalAbs = absval(line1Normal);
                        f32 line2NormalAbs = absval(line2Normal);

                        f32 normalTotal = line1NormalAbs + line2NormalAbs;
                        f32 from1 = line1NormalAbs / normalTotal;

                        V3f     vLerped = v3flerp(polyV1, polyV2, from1);
                        Color01 cLerped = color01Lerp(polyC1, polyC2, from1);

                        if (line1Normal > 0) {
                            assert(newPoly.count + 2 <= MAX_TRI_BOX_INTERSECTION_VERTICES);

                            newPoly.vertices[newPoly.count] = polyV1;
                            newPoly.vertices[newPoly.count + 1] = vLerped;

                            newPoly.colors[newPoly.count] = polyC1;
                            newPoly.colors[newPoly.count + 1] = cLerped;

                            newPoly.count += 2;
                        } else {
                            assert(newPoly.count + 1 <= MAX_TRI_BOX_INTERSECTION_VERTICES);
                            assert(line2Normal > 0);
                            newPoly.vertices[newPoly.count] = vLerped;
                            newPoly.colors[newPoly.count] = cLerped;
                            newPoly.count += 1;
                        }
                    } else if (line1Normal >= 0) {
                        assert(newPoly.count + 1 <= MAX_TRI_BOX_INTERSECTION_VERTICES);
                        assert(line2Normal >= 0);
                        newPoly.vertices[newPoly.count] = polyV1;
                        newPoly.colors[newPoly.count] = polyC1;
                        newPoly.count += 1;
                    }
                }

                poly = newPoly;
            }

            i32 firstVertexIndex = (i32)state->swRenderer.trisScreen.vertices.len;

            for (i32 polyVertexIndex = 0; polyVertexIndex < poly.count; polyVertexIndex++) {
                V3f     polyV = poly.vertices[polyVertexIndex];
                Color01 polyC = poly.colors[polyVertexIndex];

                V3f screenV = {
                    .x = polyV.x / polyV.z / state->camera.tanHalfFov.x,
                    .y = polyV.y / polyV.z / state->camera.tanHalfFov.y,
                    .z = polyV.z,
                };

                arrpush(state->swRenderer.trisScreen.vertices, screenV);
                arrpush(state->swRenderer.trisScreen.colors, polyC);
            }

            for (i32 clippedTriIndex = 0; clippedTriIndex < poly.count - 2; clippedTriIndex++) {
                i32 i1 = 0;
                i32 i2 = clippedTriIndex + 1;
                i32 i3 = clippedTriIndex + 2;

                TriangleIndices triPoly = {
                    i1 + firstVertexIndex,
                    i2 + firstVertexIndex,
                    i3 + firstVertexIndex,
                };

                meshStorageAddTriangle(&state->swRenderer.trisScreen, triPoly);
            }
        }
        timedSectionEnd();

        swRendererSetImageSize(&state->swRenderer, state->windowWidth, state->windowHeight);
        swRendererClearImage(&state->swRenderer);
        swRendererFillTriangles(&state->swRenderer);
        if (state->swRenderer.showOutlines) {
            swRendererOutlineTriangles(&state->swRenderer);
        }
    }

    if (state->showDebugUI) {
        timedSectionStart("sw debugui");
        Texture tex = state->swRenderer.texture;

        const struct nk_command* cmd = 0;
        nk_foreach(cmd, &state->ui) {
            switch (cmd->type) {
                case NK_COMMAND_NOP: break;
                case NK_COMMAND_SCISSOR: break;

                case NK_COMMAND_LINE: {
                    // TODO(khvorov) Implement
                } break;

                case NK_COMMAND_CURVE: break;
                case NK_COMMAND_RECT: break;

                case NK_COMMAND_RECT_FILLED: {
                    struct nk_command_rect_filled* rect = (struct nk_command_rect_filled*)cmd;

                    i32 y0 = clamp(rect->y, 0, tex.height);
                    i32 y1 = clamp(rect->y + rect->h, 0, tex.height);
                    i32 x0 = clamp(rect->x, 0, tex.width);
                    i32 x1 = clamp(rect->x + rect->w, 0, tex.width);

                    u32 color = color255tou32(nkcolorTo255(rect->color));

                    for (i32 ycoord = y0; ycoord < y1; ycoord++) {
                        for (i32 xcoord = x0; xcoord < x1; xcoord++) {
                            tex.ptr[ycoord * tex.pitch + xcoord] = color;
                        }
                    }
                } break;

                case NK_COMMAND_RECT_MULTI_COLOR: break;
                case NK_COMMAND_CIRCLE: break;
                case NK_COMMAND_CIRCLE_FILLED: break;
                case NK_COMMAND_ARC: break;
                case NK_COMMAND_ARC_FILLED: break;
                case NK_COMMAND_TRIANGLE: break;
                case NK_COMMAND_TRIANGLE_FILLED: break;
                case NK_COMMAND_POLYGON: break;
                case NK_COMMAND_POLYGON_FILLED: break;
                case NK_COMMAND_POLYLINE: break;

                case NK_COMMAND_TEXT: {
                    struct nk_command_text* text = (struct nk_command_text*)cmd;

                    Str     str = {text->string, text->length};
                    Color01 color = color255to01(nkcolorTo255(text->foreground));
                    swDrawStr(&state->font, str, tex, text->x, text->y, color);
                } break;

                case NK_COMMAND_IMAGE: break;
                case NK_COMMAND_CUSTOM: break;
            }
        }

        timedSectionEnd();
    }

    timedSectionEnd();
}

#undef function
#undef Byte
#define WIN32_LEAN_AND_MEAN 1
#define VC_EXTRALEAN 1
#include <Windows.h>
#include <hidusage.h>
#include <timeapi.h>

#pragma comment(lib, "gdi32")
#pragma comment(lib, "user32")
#pragma comment(lib, "Winmm")

//
// SECTION D3D11
//

#define COBJMACROS
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dxgidebug.h>

#pragma comment(lib, "d3d11")
#pragma comment(lib, "dxguid")

// TODO(khvorov) Precompile in release?
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler")

#define asserthr(x) assert(SUCCEEDED(x))

#define function static
#define Byte (1)

function Str
readEntireFile(Arena* arena, LPCWSTR path) {
    void*         buf = arenaFreePtr(arena);
    HANDLE        handle = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    DWORD         bytesRead = 0;
    LARGE_INTEGER filesize = {};
    assert(filesize.QuadPart <= arenaFreeSize(arena));
    GetFileSizeEx(handle, &filesize);
    ReadFile(handle, buf, filesize.QuadPart, &bytesRead, 0);
    assert(bytesRead == filesize.QuadPart);
    CloseHandle(handle);
    arenaChangeUsed(arena, bytesRead);
    Str result = (Str) {(char*)buf, (isize)bytesRead};
    return result;
}

// TODO(khvorov) Handle resizing

typedef struct D3D11Common {
    ID3D11DeviceContext*    context;
    ID3D11Device*           device;
    ID3D11RenderTargetView* rtView;
    IDXGISwapChain1*        swapChain;
} D3D11Common;

function D3D11Common
initD3D11Common(HWND window, isize viewportWidth, isize viewportHeight) {
    ID3D11Device*        device = 0;
    ID3D11DeviceContext* context = 0;
    {
        UINT flags = 0;
#ifdef TRIAXIS_debuginfo
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
        asserthr(D3D11CreateDevice(
            NULL,
            D3D_DRIVER_TYPE_HARDWARE,
            NULL,
            flags,
            levels,
            ARRAYSIZE(levels),
            D3D11_SDK_VERSION,
            &device,
            NULL,
            &context
        ));
    }

#ifdef TRIAXIS_asserts
    {
        ID3D11InfoQueue* info = 0;
        ID3D11Device_QueryInterface(device, &IID_ID3D11InfoQueue, (void**)&info);
        ID3D11InfoQueue_SetBreakOnSeverity(info, D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        ID3D11InfoQueue_SetBreakOnSeverity(info, D3D11_MESSAGE_SEVERITY_ERROR, TRUE);
        ID3D11InfoQueue_Release(info);
    }

    {
        HMODULE dxgiDebug = LoadLibraryA("dxgidebug.dll");
        if (dxgiDebug) {
            HRESULT(WINAPI * dxgiGetDebugInterface)
            (REFIID riid, void** ppDebug);

            *(FARPROC*)&dxgiGetDebugInterface = GetProcAddress(dxgiDebug, "DXGIGetDebugInterface");

            IDXGIInfoQueue* info = 0;
            asserthr(dxgiGetDebugInterface(&IID_IDXGIInfoQueue, (void**)&info));
            IDXGIInfoQueue_SetBreakOnSeverity(info, DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            IDXGIInfoQueue_SetBreakOnSeverity(info, DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, TRUE);
            IDXGIInfoQueue_Release(info);
        }
    }
#endif

    IDXGISwapChain1* swapChain = 0;
    {
        IDXGIDevice* dxgiDevice = 0;
        asserthr(ID3D11Device_QueryInterface(device, &IID_IDXGIDevice, (void**)&dxgiDevice));

        IDXGIAdapter* dxgiAdapter = 0;
        asserthr(IDXGIDevice_GetAdapter(dxgiDevice, &dxgiAdapter));

        IDXGIFactory2* factory = 0;
        asserthr(IDXGIAdapter_GetParent(dxgiAdapter, &IID_IDXGIFactory2, (void**)&factory));

        DXGI_SWAP_CHAIN_DESC1 desc = {
            .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
            .SampleDesc = {.Count = 1, .Quality = 0},
            .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
            .BufferCount = 2,
            .Scaling = DXGI_SCALING_NONE,
            .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
        };

        asserthr(IDXGIFactory2_CreateSwapChainForHwnd(factory, (IUnknown*)device, window, &desc, NULL, NULL, &swapChain));

        IDXGIFactory2_MakeWindowAssociation(factory, window, DXGI_MWA_NO_ALT_ENTER);

        IDXGIFactory2_Release(factory);
        IDXGIAdapter_Release(dxgiAdapter);
        IDXGIDevice_Release(dxgiDevice);
    }

    ID3D11RenderTargetView* rtView = 0;
    {
        asserthr(IDXGISwapChain1_ResizeBuffers(swapChain, 0, viewportWidth, viewportHeight, DXGI_FORMAT_UNKNOWN, 0));

        ID3D11Texture2D* backbuffer = 0;
        IDXGISwapChain1_GetBuffer(swapChain, 0, &IID_ID3D11Texture2D, (void**)&backbuffer);
        ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource*)backbuffer, NULL, &rtView);
        assert(rtView);
        ID3D11Texture2D_Release(backbuffer);
    }

    {
        D3D11_VIEWPORT viewport = {
            .TopLeftX = 0,
            .TopLeftY = 0,
            .Width = (FLOAT)viewportWidth,
            .Height = (FLOAT)viewportHeight,
            .MinDepth = 0,
            .MaxDepth = 1,
        };
        ID3D11DeviceContext_RSSetViewports(context, 1, &viewport);
    }

    D3D11Common common = {
        .context = context,
        .device = device,
        .rtView = rtView,
        .swapChain = swapChain,
    };
    return common;
}

function ID3DBlob*
compileShader(Str hlsl, const char* name, const char* kind) {
    UINT flags = D3DCOMPILE_PACK_MATRIX_ROW_MAJOR | D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS;
#ifdef TRIAXIS_debuginfo
    flags |= D3DCOMPILE_DEBUG;
#endif
#ifndef TRIAXIS_optimise
    flags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    ID3DBlob* error = 0;
    ID3DBlob* blob = 0;

    HRESULT result = D3DCompile(hlsl.ptr, hlsl.len, NULL, NULL, NULL, name, kind, flags, 0, &blob, &error);
    if (FAILED(result)) {
        char* msg = (char*)ID3D10Blob_GetBufferPointer(error);
        OutputDebugStringA(msg);
        assert(!"failed to compile");
    }

    return blob;
}

typedef struct VSPS {
    ID3D11VertexShader* vs;
    ID3D11InputLayout*  layout;
    ID3D11PixelShader*  ps;
} VSPS;

function VSPS
compileVSPS(D3D11_INPUT_ELEMENT_DESC* desc, isize descCount, LPCWSTR path, ID3D11Device* device, Arena* scratch) {
    TempMemory temp = beginTempMemory(scratch);

    VSPS result = {};

    Str hlsl = readEntireFile(scratch, path);

    {
        ID3DBlob* blob = compileShader(hlsl, "vs", "vs_5_0");
        ID3D11Device_CreateVertexShader(device, ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob), NULL, &result.vs);
        ID3D11Device_CreateInputLayout(device, desc, descCount, ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob), &result.layout);
        ID3D10Blob_Release(blob);
    }

    {
        ID3DBlob* pblob = compileShader(hlsl, "ps", "ps_5_0");
        ID3D11Device_CreatePixelShader(device, ID3D10Blob_GetBufferPointer(pblob), ID3D10Blob_GetBufferSize(pblob), NULL, &result.ps);
        ID3D10Blob_Release(pblob);
    }

    endTempMemory(temp);
    return result;
}

typedef struct D3D11BlitterVertex {
    f32 pos[2];
    f32 uv[2];
} D3D11BlitterVertex;

typedef struct D3D11Blitter {
    D3D11Common*           common;
    ID3D11Buffer*          vbuffer;
    VSPS                   vsps;
    ID3D11SamplerState*    sampler;
    ID3D11RasterizerState* rasterizerState;

    struct {
        ID3D11Texture2D*          tex2d;
        ID3D11ShaderResourceView* view;
        isize                     width, height;
    } tex;
} D3D11Blitter;

function D3D11Blitter
initD3D11Blitter(D3D11Common* common, isize textureWidth, isize textureHeight, Arena* scratch) {
    TempMemory temp = beginTempMemory(scratch);

    D3D11Blitter blitter = {.common = common};

    {
        D3D11BlitterVertex data[] = {
            {{-1.00f, +1.00f}, {0.0f, 0.0f}},
            {{+1.00f, +1.00f}, {1.0f, 0.0f}},
            {{-1.00f, -1.00f}, {0.0f, 1.0f}},
            {{+1.00f, -1.00f}, {1.0f, 1.0f}},
        };

        D3D11_BUFFER_DESC desc = {
            .ByteWidth = sizeof(data),
            .Usage = D3D11_USAGE_IMMUTABLE,
            .BindFlags = D3D11_BIND_VERTEX_BUFFER,
        };

        D3D11_SUBRESOURCE_DATA initial = {.pSysMem = data};
        ID3D11Device_CreateBuffer(common->device, &desc, &initial, &blitter.vbuffer);
    }

    {
        D3D11_INPUT_ELEMENT_DESC desc[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(D3D11BlitterVertex, pos), D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(D3D11BlitterVertex, uv), D3D11_INPUT_PER_VERTEX_DATA, 0},
        };

        blitter.vsps = compileVSPS(desc, arrayCount(desc), L"code/blitter.hlsl", common->device, scratch);
    }

    {
        D3D11_TEXTURE2D_DESC desc = {
            .Width = (UINT)textureWidth,
            .Height = (UINT)textureHeight,
            .MipLevels = 1,
            .ArraySize = 1,
            .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
            .SampleDesc = {.Count = 1, .Quality = 0},
            .Usage = D3D11_USAGE_DYNAMIC,
            .BindFlags = D3D11_BIND_SHADER_RESOURCE,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        };

        ID3D11Device_CreateTexture2D(common->device, &desc, 0, &blitter.tex.tex2d);
        ID3D11Device_CreateShaderResourceView(common->device, (ID3D11Resource*)blitter.tex.tex2d, NULL, &blitter.tex.view);

        blitter.tex.width = textureWidth;
        blitter.tex.height = textureHeight;
    }

    {
        D3D11_SAMPLER_DESC desc = {
            .Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
            .AddressU = D3D11_TEXTURE_ADDRESS_WRAP,
            .AddressV = D3D11_TEXTURE_ADDRESS_WRAP,
            .AddressW = D3D11_TEXTURE_ADDRESS_WRAP,
        };
        ID3D11Device_CreateSamplerState(common->device, &desc, &blitter.sampler);
    }

    {
        D3D11_RASTERIZER_DESC desc = {
            .FillMode = D3D11_FILL_SOLID,
            .CullMode = D3D11_CULL_NONE,
        };
        ID3D11Device_CreateRasterizerState(common->device, &desc, &blitter.rasterizerState);
    }

    endTempMemory(temp);
    return blitter;
}

function void
d3d11blit(D3D11Blitter blitter, Texture tex) {
    {
        UINT offset = 0;
        UINT stride = sizeof(D3D11BlitterVertex);
        ID3D11DeviceContext_IASetVertexBuffers(blitter.common->context, 0, 1, &blitter.vbuffer, &stride, &offset);
    }

    ID3D11DeviceContext_VSSetShader(blitter.common->context, blitter.vsps.vs, NULL, 0);
    ID3D11DeviceContext_IASetInputLayout(blitter.common->context, blitter.vsps.layout);
    ID3D11DeviceContext_IASetPrimitiveTopology(blitter.common->context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D11DeviceContext_PSSetShader(blitter.common->context, blitter.vsps.ps, NULL, 0);
    ID3D11DeviceContext_PSSetShaderResources(blitter.common->context, 0, 1, &blitter.tex.view);
    ID3D11DeviceContext_PSSetSamplers(blitter.common->context, 0, 1, &blitter.sampler);
    ID3D11DeviceContext_RSSetState(blitter.common->context, blitter.rasterizerState);

    {
        FLOAT color[] = {0.0f, 0.0, 0.0f, 1.f};
        ID3D11DeviceContext_ClearRenderTargetView(blitter.common->context, blitter.common->rtView, color);
    }

    {
        assert(tex.width <= blitter.tex.width);
        assert(tex.height <= blitter.tex.height);

        D3D11_MAPPED_SUBRESOURCE mappedTexture = {};
        ID3D11DeviceContext_Map(blitter.common->context, (ID3D11Resource*)blitter.tex.tex2d, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedTexture);
        u32* pixels = (u32*)mappedTexture.pData;
        timedSectionStart("present copymem");
        for (isize row = 0; row < tex.height; row++) {
            u32* srcRow = tex.ptr + row * tex.pitch;
            u32* destRow = pixels + row * blitter.tex.width;
            memcpy_(destRow, srcRow, tex.width * sizeof(u32));
        }
        timedSectionEnd();
        ID3D11DeviceContext_Unmap(blitter.common->context, (ID3D11Resource*)blitter.tex.tex2d, 0);
    }

    {
        ID3D11DeviceContext_OMSetRenderTargets(blitter.common->context, 1, &blitter.common->rtView, 0);
        ID3D11DeviceContext_Draw(blitter.common->context, 4, 0);
    }

    HRESULT presentResult = IDXGISwapChain1_Present(blitter.common->swapChain, 1, 0);
    asserthr(presentResult);
    if (presentResult == DXGI_STATUS_OCCLUDED) {
        Sleep(10);
    }
}

typedef struct D3D11ConstCamera {
    Rotor3f orientation;
    V3f     pos;
    f32     tanHalfFovX;
    f32     tanHalfFovY;
    f32     nearClipZ;
    f32     farClipZ;
    u8      pad[4];
} D3D11ConstCamera;

typedef struct D3D11ConstMesh {
    Rotor3f orientation;
    V3f     pos;
    u8      pad1[4];
} D3D11ConstMesh;

typedef struct D3D11FontConstDims {
    V2f screen;
    V2f tex;
} D3D11FontConstDims;

typedef struct D3D11TriFilledConstDims {
    V2f screen;
    V2f pad;
} D3D11TriFilledConstDims;

typedef struct D3D11TriFilledVertex {
    V2f     pos;
    Color01 color;
} D3D11TriFilledVertex;

typedef struct D3D11FontVertex {
    V2f     scr;
    V2f     tex;
    Color01 color;
} D3D11FontVertex;

typedef struct D3D11Renderer {
    D3D11Common* common;

    ID3D11RasterizerState*   rasterizerState;
    ID3D11RasterizerState*   rasterizerNoCull;
    ID3D11DepthStencilState* depthSpencilState;
    ID3D11DepthStencilView*  depthStencilView;

    struct {
        ID3D11Buffer* vbuffer;
        ID3D11Buffer* ibuffer;
        ID3D11Buffer* colorBuffer;
        VSPS          vsps;
        ID3D11Buffer* constCamera;
        ID3D11Buffer* constMesh;
    } mesh;

    struct {
        isize         vertexCap;
        ID3D11Buffer* vertices;
        VSPS          vsps;
        ID3D11Buffer* constDims;
    } triFilled;

    struct {
        isize                     vertexCap;
        ID3D11Buffer*             vertices;
        VSPS                      vsps;
        ID3D11ShaderResourceView* textureView;
        ID3D11SamplerState*       sampler;
        ID3D11BlendState*         blend;
        ID3D11Buffer*             constDims;
    } font;
} D3D11Renderer;

function D3D11Renderer
initD3D11Renderer(D3D11Common* common, State* state) {
    TempMemory temp = beginTempMemory(&state->scratch);

    D3D11Renderer renderer = {.common = common};

    {
        D3D11_BUFFER_DESC desc = {
            .ByteWidth = (UINT)(state->meshStorage.vertices.len * sizeof(*state->meshStorage.vertices.ptr)),
            .Usage = D3D11_USAGE_IMMUTABLE,
            .BindFlags = D3D11_BIND_VERTEX_BUFFER,
        };
        D3D11_SUBRESOURCE_DATA initial = {.pSysMem = state->meshStorage.vertices.ptr};
        ID3D11Device_CreateBuffer(common->device, &desc, &initial, &renderer.mesh.vbuffer);
    }

    {
        D3D11_BUFFER_DESC desc = {
            .ByteWidth = (UINT)(state->meshStorage.indices.len * sizeof(*state->meshStorage.indices.ptr)),
            .Usage = D3D11_USAGE_IMMUTABLE,
            .BindFlags = D3D11_BIND_INDEX_BUFFER,
        };
        D3D11_SUBRESOURCE_DATA initial = {.pSysMem = state->meshStorage.indices.ptr};
        ID3D11Device_CreateBuffer(common->device, &desc, &initial, &renderer.mesh.ibuffer);
    }

    {
        D3D11_BUFFER_DESC desc = {
            .ByteWidth = (UINT)(state->meshStorage.colors.len * sizeof(*state->meshStorage.colors.ptr)),
            .Usage = D3D11_USAGE_IMMUTABLE,
            .BindFlags = D3D11_BIND_VERTEX_BUFFER,
        };
        D3D11_SUBRESOURCE_DATA initial = {.pSysMem = state->meshStorage.colors.ptr};
        ID3D11Device_CreateBuffer(common->device, &desc, &initial, &renderer.mesh.colorBuffer);
    }

    {
        renderer.triFilled.vertexCap = 1024;
        D3D11_BUFFER_DESC desc = {
            .ByteWidth = (UINT)(renderer.triFilled.vertexCap * sizeof(D3D11TriFilledVertex)),
            .Usage = D3D11_USAGE_DYNAMIC,
            .BindFlags = D3D11_BIND_VERTEX_BUFFER,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        };
        ID3D11Device_CreateBuffer(common->device, &desc, 0, &renderer.triFilled.vertices);
    }

    {
        renderer.font.vertexCap = 1024;
        D3D11_BUFFER_DESC desc = {
            .ByteWidth = (UINT)(renderer.font.vertexCap * sizeof(D3D11FontVertex)),
            .Usage = D3D11_USAGE_DYNAMIC,
            .BindFlags = D3D11_BIND_VERTEX_BUFFER,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        };
        ID3D11Device_CreateBuffer(common->device, &desc, 0, &renderer.font.vertices);
    }

    {
        D3D11_INPUT_ELEMENT_DESC desc[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };

        renderer.mesh.vsps = compileVSPS(desc, arrayCount(desc), L"code/renderer.hlsl", common->device, &state->scratch);
    }

    {
        D3D11_INPUT_ELEMENT_DESC desc[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(D3D11TriFilledVertex, pos), D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(D3D11TriFilledVertex, color), D3D11_INPUT_PER_VERTEX_DATA, 0},
        };

        renderer.triFilled.vsps = compileVSPS(desc, arrayCount(desc), L"code/trifilled.hlsl", common->device, &state->scratch);
    }

    {
        D3D11_INPUT_ELEMENT_DESC desc[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(D3D11FontVertex, scr), D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(D3D11FontVertex, tex), D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(D3D11FontVertex, color), D3D11_INPUT_PER_VERTEX_DATA, 0},
        };

        renderer.font.vsps = compileVSPS(desc, arrayCount(desc), L"code/font.hlsl", common->device, &state->scratch);
    }

    {
        D3D11_TEXTURE2D_DESC desc = {
            .Width = (UINT)state->font.atlasW,
            .Height = (UINT)state->font.atlasH,
            .MipLevels = 1,
            .ArraySize = 1,
            .Format = DXGI_FORMAT_A8_UNORM,
            .SampleDesc = {.Count = 1, .Quality = 0},
            .Usage = D3D11_USAGE_IMMUTABLE,
            .BindFlags = D3D11_BIND_SHADER_RESOURCE,
        };

        D3D11_SUBRESOURCE_DATA initial = {.pSysMem = state->font.atlas, .SysMemPitch = (UINT)state->font.atlasW};

        ID3D11Texture2D* tex = 0;
        ID3D11Device_CreateTexture2D(common->device, &desc, &initial, &tex);
        ID3D11Device_CreateShaderResourceView(common->device, (ID3D11Resource*)tex, 0, &renderer.font.textureView);
        ID3D11Texture2D_Release(tex);
    }

    {
        D3D11_BLEND_DESC desc = {
            .RenderTarget[0] = {
                .BlendEnable = TRUE,
                .SrcBlend = D3D11_BLEND_SRC_ALPHA,
                .DestBlend = D3D11_BLEND_INV_SRC_ALPHA,
                .BlendOp = D3D11_BLEND_OP_ADD,
                .SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA,
                .DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA,
                .BlendOpAlpha = D3D11_BLEND_OP_ADD,
                .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL,
            },
        };
        ID3D11Device_CreateBlendState(common->device, &desc, &renderer.font.blend);
    }

    {
        D3D11_SAMPLER_DESC desc = {
            .Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
            .AddressU = D3D11_TEXTURE_ADDRESS_WRAP,
            .AddressV = D3D11_TEXTURE_ADDRESS_WRAP,
            .AddressW = D3D11_TEXTURE_ADDRESS_WRAP,
        };
        ID3D11Device_CreateSamplerState(common->device, &desc, &renderer.font.sampler);
    }

    {
        D3D11_RASTERIZER_DESC desc = {
            .FillMode = D3D11_FILL_SOLID,
            .CullMode = D3D11_CULL_BACK,
            .DepthClipEnable = true,
        };
        ID3D11Device_CreateRasterizerState(common->device, &desc, &renderer.rasterizerState);
    }

    {
        D3D11_RASTERIZER_DESC desc = {
            .FillMode = D3D11_FILL_SOLID,
            .CullMode = D3D11_CULL_NONE,
        };
        ID3D11Device_CreateRasterizerState(common->device, &desc, &renderer.rasterizerNoCull);
    }

    {
        D3D11_BUFFER_DESC desc = {
            .ByteWidth = sizeof(D3D11ConstCamera),
            .Usage = D3D11_USAGE_DYNAMIC,
            .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        };
        ID3D11Device_CreateBuffer(common->device, &desc, 0, &renderer.mesh.constCamera);

        desc.ByteWidth = sizeof(D3D11ConstMesh);
        ID3D11Device_CreateBuffer(common->device, &desc, 0, &renderer.mesh.constMesh);

        // TODO(khvorov) Resizing
        {
            desc.ByteWidth = sizeof(D3D11FontConstDims);
            D3D11FontConstDims     initDims = {.screen = {(f32)state->windowWidth, (f32)state->windowHeight}, .tex = {(f32)state->font.atlasW, (f32)state->font.atlasH}};
            D3D11_SUBRESOURCE_DATA init = {.pSysMem = &initDims};
            ID3D11Device_CreateBuffer(common->device, &desc, &init, &renderer.font.constDims);
        }

        // TODO(khvorov) Resizing
        {
            desc.ByteWidth = sizeof(D3D11TriFilledConstDims);
            D3D11TriFilledConstDims initDims = {.screen = {(f32)state->windowWidth, (f32)state->windowHeight}};
            D3D11_SUBRESOURCE_DATA  init = {.pSysMem = &initDims};
            ID3D11Device_CreateBuffer(common->device, &desc, &init, &renderer.triFilled.constDims);
        }
    }

    {
        D3D11_DEPTH_STENCIL_DESC desc = {
            .DepthEnable = TRUE,
            .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL,
            .DepthFunc = D3D11_COMPARISON_LESS,
            .StencilEnable = FALSE,
            .StencilReadMask = 0xFF,
            .StencilWriteMask = 0xFF,
            .FrontFace = {
                .StencilFailOp = D3D11_STENCIL_OP_KEEP,
                .StencilDepthFailOp = D3D11_STENCIL_OP_KEEP,
                .StencilPassOp = D3D11_STENCIL_OP_KEEP,
                .StencilFunc = D3D11_COMPARISON_ALWAYS,
            },
            .BackFace = {
                .StencilFailOp = D3D11_STENCIL_OP_KEEP,
                .StencilDepthFailOp = D3D11_STENCIL_OP_KEEP,
                .StencilPassOp = D3D11_STENCIL_OP_KEEP,
                .StencilFunc = D3D11_COMPARISON_ALWAYS,
            },
        };
        ID3D11Device_CreateDepthStencilState(common->device, &desc, &renderer.depthSpencilState);
    }

    {
        // TODO(khvorov) Resizing
        D3D11_TEXTURE2D_DESC desc = {
            .Width = (UINT)state->windowWidth,
            .Height = (UINT)state->windowHeight,
            .MipLevels = 1,
            .ArraySize = 1,
            .Format = DXGI_FORMAT_D32_FLOAT,
            .SampleDesc = {1, 0},
            .Usage = D3D11_USAGE_DEFAULT,
            .BindFlags = D3D11_BIND_DEPTH_STENCIL,
        };

        ID3D11Texture2D* depth = 0;
        ID3D11Device_CreateTexture2D(common->device, &desc, NULL, &depth);
        ID3D11Device_CreateDepthStencilView(common->device, (ID3D11Resource*)depth, NULL, &renderer.depthStencilView);
        ID3D11Texture2D_Release(depth);
    }

    endTempMemory(temp);
    return renderer;
}

// TODO(khvorov) Replace with quadFilled?
typedef struct D3D11TriFilledVertexDynArr {
    D3D11TriFilledVertex* ptr;
    isize                 len;
    isize                 cap;
} D3D11TriFilledVertexDynArr;

function void
d3d11render_pushQuad(D3D11TriFilledVertexDynArr* triFilled, V2f v1, V2f v2, V2f v3, V2f v4, struct nk_color nkcolor) {
    Color01 color = color255to01(nkcolorTo255(nkcolor));

    arrpush(*triFilled, ((D3D11TriFilledVertex) {v1, color}));
    arrpush(*triFilled, ((D3D11TriFilledVertex) {v2, color}));
    arrpush(*triFilled, ((D3D11TriFilledVertex) {v3, color}));

    arrpush(*triFilled, ((D3D11TriFilledVertex) {v2, color}));
    arrpush(*triFilled, ((D3D11TriFilledVertex) {v4, color}));
    arrpush(*triFilled, ((D3D11TriFilledVertex) {v3, color}));
}

function void
d3d11render(D3D11Renderer renderer, State* state) {
    {
        UINT          offsets[] = {0, 0};
        UINT          strides[] = {sizeof(*state->meshStorage.vertices.ptr), sizeof(*state->meshStorage.colors.ptr)};
        ID3D11Buffer* buffers[] = {renderer.mesh.vbuffer, renderer.mesh.colorBuffer};
        ID3D11DeviceContext_IASetVertexBuffers(renderer.common->context, 0, arrayCount(buffers), buffers, strides, offsets);
    }

    ID3D11DeviceContext_IASetIndexBuffer(renderer.common->context, renderer.mesh.ibuffer, DXGI_FORMAT_R32_UINT, 0);
    ID3D11DeviceContext_VSSetShader(renderer.common->context, renderer.mesh.vsps.vs, NULL, 0);
    ID3D11DeviceContext_IASetInputLayout(renderer.common->context, renderer.mesh.vsps.layout);
    ID3D11DeviceContext_IASetPrimitiveTopology(renderer.common->context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ID3D11DeviceContext_PSSetShader(renderer.common->context, renderer.mesh.vsps.ps, NULL, 0);
    ID3D11DeviceContext_RSSetState(renderer.common->context, renderer.rasterizerState);
    ID3D11DeviceContext_OMSetDepthStencilState(renderer.common->context, renderer.depthSpencilState, 0);

    {
        ID3D11Buffer* buffers[] = {renderer.mesh.constCamera, renderer.mesh.constMesh};
        ID3D11DeviceContext_VSSetConstantBuffers(renderer.common->context, 0, arrayCount(buffers), buffers);
    }

    {
        D3D11_MAPPED_SUBRESOURCE mappedCamera = {};
        ID3D11DeviceContext_Map(renderer.common->context, (ID3D11Resource*)renderer.mesh.constCamera, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedCamera);
        D3D11ConstCamera* constCamera = (D3D11ConstCamera*)mappedCamera.pData;
        constCamera->orientation = state->camera.currentOrientation;
        constCamera->pos = state->camera.pos;
        constCamera->tanHalfFovX = state->camera.tanHalfFov.x;
        constCamera->tanHalfFovY = state->camera.tanHalfFov.y;
        constCamera->nearClipZ = state->camera.nearClipZ;
        constCamera->farClipZ = state->camera.farClipZ;
        ID3D11DeviceContext_Unmap(renderer.common->context, (ID3D11Resource*)renderer.mesh.constCamera, 0);
    }

    {
        FLOAT color[] = {0.0f, 0.0, 0.0f, 1.f};
        ID3D11DeviceContext_ClearRenderTargetView(renderer.common->context, renderer.common->rtView, color);
    }

    ID3D11DeviceContext_ClearDepthStencilView(renderer.common->context, renderer.depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    ID3D11DeviceContext_OMSetRenderTargets(renderer.common->context, 1, &renderer.common->rtView, renderer.depthStencilView);

    for (isize meshIndex = 0; meshIndex < state->meshes.len; meshIndex++) {
        Mesh mesh = state->meshes.ptr[meshIndex];

        D3D11_MAPPED_SUBRESOURCE mappedMesh = {};
        ID3D11DeviceContext_Map(renderer.common->context, (ID3D11Resource*)renderer.mesh.constMesh, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedMesh);
        D3D11ConstMesh* constMesh = (D3D11ConstMesh*)mappedMesh.pData;
        constMesh->orientation = mesh.orientation;
        constMesh->pos = mesh.pos;
        ID3D11DeviceContext_Unmap(renderer.common->context, (ID3D11Resource*)renderer.mesh.constMesh, 0);

        i32 baseVertex = mesh.vertices.ptr - state->meshStorage.vertices.ptr;
        i32 baseIndex = (i32*)mesh.indices.ptr - (i32*)state->meshStorage.indices.ptr;
        ID3D11DeviceContext_DrawIndexed(renderer.common->context, mesh.indices.len * 3, baseIndex, baseVertex);
    }

    if (state->showDebugUI) {
        ID3D11DeviceContext_RSSetState(renderer.common->context, renderer.rasterizerNoCull);
        ID3D11DeviceContext_OMSetRenderTargets(renderer.common->context, 1, &renderer.common->rtView, 0);

        D3D11_MAPPED_SUBRESOURCE mappedTriFilledVertices = {};
        ID3D11DeviceContext_Map(renderer.common->context, (ID3D11Resource*)renderer.triFilled.vertices, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedTriFilledVertices);

        D3D11TriFilledVertexDynArr triFilled = {(D3D11TriFilledVertex*)mappedTriFilledVertices.pData, 0, renderer.triFilled.vertexCap};

        D3D11_MAPPED_SUBRESOURCE mappedFontVertices = {};
        ID3D11DeviceContext_Map(renderer.common->context, (ID3D11Resource*)renderer.font.vertices, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedFontVertices);

        struct {
            D3D11FontVertex* ptr;
            isize            len;
            isize            cap;
        } font = {(D3D11FontVertex*)mappedFontVertices.pData, 0, renderer.font.vertexCap};

        const struct nk_command* cmd = 0;
        nk_foreach(cmd, &state->ui) {
            switch (cmd->type) {
                case NK_COMMAND_NOP: break;
                case NK_COMMAND_SCISSOR: break;

                case NK_COMMAND_LINE: {
                    struct nk_command_line* line = (struct nk_command_line*)cmd;

                    i32 x0 = line->begin.x;
                    i32 y0 = line->begin.y;
                    i32 x1 = line->end.x;
                    i32 y1 = line->end.y;

                    V2f begin = {(f32)x0, (f32)y0};
                    V2f end = {(f32)x1, (f32)y1};

                    V2f vec = v2fsub(end, begin);
                    V2f vecPerpendicular = {-vec.y, vec.x};
                    V2f vecPerpN = v2fnormalise(vecPerpendicular);
                    V2f pointMove = v2fscale(vecPerpN, (f32)line->line_thickness);

                    V2f v1 = v2fadd(begin, pointMove);
                    V2f v2 = v2fsub(begin, pointMove);
                    V2f v3 = v2fadd(end, pointMove);
                    V2f v4 = v2fsub(end, pointMove);

                    d3d11render_pushQuad(&triFilled, v1, v2, v3, v4, line->color);
                } break;

                case NK_COMMAND_CURVE: break;
                case NK_COMMAND_RECT: break;

                case NK_COMMAND_RECT_FILLED: {
                    struct nk_command_rect_filled* rect = (struct nk_command_rect_filled*)cmd;

                    i32 x0 = rect->x;
                    i32 x1 = rect->x + rect->w;
                    i32 y0 = rect->y;
                    i32 y1 = rect->y + rect->h;

                    V2f topleft = {(f32)x0, (f32)y0};
                    V2f topright = {(f32)x1, (f32)y0};
                    V2f bottomleft = {(f32)x0, (f32)y1};
                    V2f bottomright = {(f32)x1, (f32)y1};

                    d3d11render_pushQuad(&triFilled, topleft, topright, bottomleft, bottomright, rect->color);
                } break;

                case NK_COMMAND_RECT_MULTI_COLOR: break;
                case NK_COMMAND_CIRCLE: break;
                case NK_COMMAND_CIRCLE_FILLED: break;
                case NK_COMMAND_ARC: break;
                case NK_COMMAND_ARC_FILLED: break;
                case NK_COMMAND_TRIANGLE: break;
                case NK_COMMAND_TRIANGLE_FILLED: break;
                case NK_COMMAND_POLYGON: break;
                case NK_COMMAND_POLYGON_FILLED: break;
                case NK_COMMAND_POLYLINE: break;

                case NK_COMMAND_TEXT: {
                    struct nk_command_text* text = (struct nk_command_text*)cmd;

                    Str     str = {text->string, text->length};
                    Color01 color = color255to01(nkcolorTo255(text->foreground));

                    i32 curx = text->x;
                    for (isize strInd = 0; strInd < str.len; strInd++) {
                        char  ch = str.ptr[strInd];
                        Glyph glyph = state->font.ascii[(i32)ch];

                        i32 texx0 = glyph.x;
                        i32 texx1 = glyph.x + glyph.w;
                        i32 texy0 = glyph.y;
                        i32 texy1 = glyph.y + glyph.h;

                        V2f textopleft = {(f32)texx0, (f32)texy0};
                        V2f textopright = {(f32)texx1, (f32)texy0};
                        V2f texbottomleft = {(f32)texx0, (f32)texy1};
                        V2f texbottomright = {(f32)texx1, (f32)texy1};

                        i32 scrx0 = curx;
                        i32 scrx1 = scrx0 + glyph.w;
                        i32 scry0 = text->y;
                        i32 scry1 = scry0 + glyph.h;

                        V2f scrtopleft = {(f32)scrx0, (f32)scry0};
                        V2f scrtopright = {(f32)scrx1, (f32)scry0};
                        V2f scrbottomleft = {(f32)scrx0, (f32)scry1};
                        V2f scrbottomright = {(f32)scrx1, (f32)scry1};

                        arrpush(font, ((D3D11FontVertex) {scrtopleft, textopleft, color}));
                        arrpush(font, ((D3D11FontVertex) {scrtopright, textopright, color}));
                        arrpush(font, ((D3D11FontVertex) {scrbottomleft, texbottomleft, color}));

                        arrpush(font, ((D3D11FontVertex) {scrtopright, textopright, color}));
                        arrpush(font, ((D3D11FontVertex) {scrbottomright, texbottomright, color}));
                        arrpush(font, ((D3D11FontVertex) {scrbottomleft, texbottomleft, color}));

                        curx += glyph.advance;
                    }

                } break;

                case NK_COMMAND_IMAGE: break;
                case NK_COMMAND_CUSTOM: break;
            }
        }

        ID3D11DeviceContext_Unmap(renderer.common->context, (ID3D11Resource*)renderer.triFilled.vertices, 0);
        ID3D11DeviceContext_Unmap(renderer.common->context, (ID3D11Resource*)renderer.font.vertices, 0);

        {
            UINT          offsets[] = {0};
            UINT          strides[] = {sizeof(D3D11TriFilledVertex)};
            ID3D11Buffer* buffers[] = {renderer.triFilled.vertices};
            ID3D11DeviceContext_IASetVertexBuffers(renderer.common->context, 0, arrayCount(buffers), buffers, strides, offsets);
        }

        {
            ID3D11Buffer* buffers[] = {renderer.triFilled.constDims};
            ID3D11DeviceContext_VSSetConstantBuffers(renderer.common->context, 0, arrayCount(buffers), buffers);
        }

        ID3D11DeviceContext_VSSetShader(renderer.common->context, renderer.triFilled.vsps.vs, NULL, 0);
        ID3D11DeviceContext_IASetInputLayout(renderer.common->context, renderer.triFilled.vsps.layout);
        ID3D11DeviceContext_IASetPrimitiveTopology(renderer.common->context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        ID3D11DeviceContext_PSSetShader(renderer.common->context, renderer.triFilled.vsps.ps, NULL, 0);

        ID3D11DeviceContext_Draw(renderer.common->context, triFilled.len, 0);

        {
            UINT          offsets[] = {0};
            UINT          strides[] = {sizeof(D3D11FontVertex)};
            ID3D11Buffer* buffers[] = {renderer.font.vertices};
            ID3D11DeviceContext_IASetVertexBuffers(renderer.common->context, 0, arrayCount(buffers), buffers, strides, offsets);
        }

        {
            ID3D11Buffer* buffers[] = {renderer.font.constDims};
            ID3D11DeviceContext_VSSetConstantBuffers(renderer.common->context, 0, arrayCount(buffers), buffers);
        }

        ID3D11DeviceContext_PSSetShaderResources(renderer.common->context, 0, 1, &renderer.font.textureView);
        ID3D11DeviceContext_PSSetSamplers(renderer.common->context, 0, 1, &renderer.font.sampler);

        ID3D11DeviceContext_VSSetShader(renderer.common->context, renderer.font.vsps.vs, NULL, 0);
        ID3D11DeviceContext_IASetInputLayout(renderer.common->context, renderer.font.vsps.layout);
        ID3D11DeviceContext_IASetPrimitiveTopology(renderer.common->context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        ID3D11DeviceContext_PSSetShader(renderer.common->context, renderer.font.vsps.ps, NULL, 0);

        ID3D11DeviceContext_OMSetBlendState(renderer.common->context, renderer.font.blend, NULL, ~0U);

        ID3D11DeviceContext_Draw(renderer.common->context, font.len, 0);
    }  // debugui

    HRESULT presentResult = IDXGISwapChain1_Present(renderer.common->swapChain, 1, 0);
    asserthr(presentResult);
    if (presentResult == DXGI_STATUS_OCCLUDED) {
        Sleep(10);
    }
}

//
// SECTION Timing
//

typedef struct Clock {
    LARGE_INTEGER freqPerSecond;
} Clock;

typedef struct ClockMarker {
    LARGE_INTEGER counter;
} ClockMarker;

function Clock
createClock(void) {
    Clock clock = {};
    QueryPerformanceFrequency(&clock.freqPerSecond);
    return clock;
}

function ClockMarker
getClockMarker(void) {
    ClockMarker marker = {};
    QueryPerformanceCounter(&marker.counter);
    return marker;
}

function f32
getMsBetween(Clock clock, ClockMarker marker1, ClockMarker marker2) {
    LONGLONG diff = marker2.counter.QuadPart - marker1.counter.QuadPart;
    f32      result = (f32)diff / (f32)clock.freqPerSecond.QuadPart * 1000.0f;
    return result;
}

typedef struct Timer {
    Clock       clock;
    ClockMarker update;
} Timer;

//
// SECTION Main
//

// NOTE(khvorov) Only to be accessed in windowProc
globalvar State* globalState = 0;

LRESULT CALLBACK
windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    LRESULT result = 0;
    switch (uMsg) {
        case WM_DESTROY: PostQuitMessage(0); break;
        case WM_ERASEBKGND: result = TRUE; break;
        case WM_KILLFOCUS: {
            inputClearKeys(&globalState->input);
        } break;
        case WM_SETFOCUS: {
            RECT rect = {};
            GetClientRect(hwnd, &rect);
            POINT topleft = {rect.left, rect.top};
            ClientToScreen(hwnd, &topleft);
            POINT bottomright = {rect.right, rect.bottom};
            ClientToScreen(hwnd, &bottomright);
            RECT screen = {.left = topleft.x, .right = bottomright.x, .top = topleft.y, .bottom = bottomright.y};
            ClipCursor(&screen);
        } break;
        default: result = DefWindowProcW(hwnd, uMsg, wParam, lParam); break;
    }
    return result;
}

int WINAPI
WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    unused(hPrevInstance);
    unused(lpCmdLine);
    unused(nCmdShow);

    State* state = 0;
    {
        isize memSize = 1 * Gigabyte;
        void* memBase = VirtualAlloc(0, memSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        assert(memBase);
        state = initState(memBase, memSize);
    }

    globalState = state;
    HWND   window = 0;
    HANDLE profileFileHandle = 0;
    {
        TempMemory temp = beginTempMemory(&state->scratch);
        LPWSTR     exename = (LPWSTR)arenaFreePtr(&state->scratch);
        DWORD      exeNameChars = GetModuleFileNameW(hInstance, exename, arenaFreeSize(&state->scratch) / sizeof(u16));

        WNDCLASSEXW windowClass = {
            .cbSize = sizeof(WNDCLASSEXW),
            .lpfnWndProc = windowProc,
            .hInstance = hInstance,
            .hIcon = LoadIconA(NULL, IDI_APPLICATION),
            .hCursor = LoadCursorA(NULL, IDC_ARROW),
            .hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH),
            .lpszClassName = L"triaxisWindowClass",
        };
        assert(RegisterClassExW(&windowClass) != 0);

        window = CreateWindowExW(
            WS_EX_APPWINDOW | WS_EX_NOREDIRECTIONBITMAP,
            windowClass.lpszClassName,
            exename,
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            state->windowWidth,
            state->windowHeight,
            NULL,
            NULL,
            windowClass.hInstance,
            NULL
        );
        assert(window);

#ifdef TRIAXIS_profile
        assert(exeNameChars >= 3);
        exename[exeNameChars - 3] = 's';
        exename[exeNameChars - 2] = 'p';
        exename[exeNameChars - 1] = 'l';

        profileFileHandle = CreateFileW(
            exename,
            GENERIC_WRITE,
            FILE_SHARE_WRITE,
            NULL,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        assert(profileFileHandle != INVALID_HANDLE_VALUE);
#endif

        endTempMemory(temp);
    }

    // NOTE(khvorov) Adjust window size such that it's the client area that's the specified size, not the whole window with decorations
    {
        RECT rect = {};
        GetClientRect(window, &rect);
        isize width = rect.right - rect.left;
        isize height = rect.bottom - rect.top;
        isize dwidth = state->windowWidth - width;
        isize dheight = state->windowHeight - height;
        SetWindowPos(window, 0, rect.left, rect.top, state->windowWidth + dwidth, state->windowHeight + dheight, 0);
    }

    {
        RAWINPUTDEVICE mouse = {
            .usUsagePage = HID_USAGE_PAGE_GENERIC,
            .usUsage = HID_USAGE_GENERIC_MOUSE,
            .hwndTarget = window,
        };

        assert(RegisterRawInputDevices(&mouse, 1, sizeof(RAWINPUTDEVICE)) == TRUE);
    }

#ifdef TRIAXIS_debuginfo
    // NOTE(khvorov) To prevent a white flash
    ShowWindow(window, SW_SHOWMINIMIZED);
#endif
    ShowWindow(window, SW_SHOWNORMAL);

    if (!state->showDebugUI) {
        ShowCursor(FALSE);
    }

    // NOTE(khvorov) Windows will sleep for random amounts of time if we don't do this
    {
        TIMECAPS caps = {};
        timeGetDevCaps(&caps, sizeof(TIMECAPS));
        timeBeginPeriod(caps.wPeriodMin);
    }

    swRendererSetImageSize(&state->swRenderer, state->windowWidth, state->windowHeight);
    D3D11Common   d3d11common = initD3D11Common(window, state->windowWidth, state->windowHeight);
    D3D11Blitter  d3d11blitter = initD3D11Blitter(&d3d11common, state->windowWidth, state->windowHeight, &state->scratch);
    D3D11Renderer d3d11renderer = initD3D11Renderer(&d3d11common, state);

    Timer timer = {.clock = createClock(), .update = getClockMarker()};

    for (bool running = true; running;) {
        timedSectionStart("frame");

        assert(state->scratch.tempCount == 0);
        assert(state->scratch.used == 0);

        // NOTE(khvorov) Input
        {
            timedSectionStart("input");

            inputBeginFrame(&state->input);
            for (MSG msg = {}; PeekMessageA(&msg, 0, 0, 0, PM_REMOVE);) {
                switch (msg.message) {
                    case WM_QUIT: running = false; break;

                    case WM_SYSKEYDOWN:
                    case WM_SYSKEYUP:
                    case WM_KEYDOWN:
                    case WM_KEYUP: {
                        InputKey key = InputKey_Up;
                        bool     keyFound = true;
                        switch (msg.wParam) {
                            case 'W': key = InputKey_Forward; break;
                            case 'S': key = InputKey_Back; break;
                            case 'A': key = InputKey_Left; break;
                            case 'D': key = InputKey_Right; break;
                            case VK_SPACE: key = InputKey_Up; break;
                            case VK_CONTROL: key = InputKey_Down; break;
                            case VK_SHIFT: key = InputKey_MoveFaster; break;
                            case VK_UP: key = InputKey_RotateZY; break;
                            case VK_DOWN: key = InputKey_RotateYZ; break;
                            case VK_LEFT: key = InputKey_RotateXZ; break;
                            case VK_RIGHT: key = InputKey_RotateZX; break;
                            case 'Q': key = InputKey_RotateXY; break;
                            case 'E': key = InputKey_RotateYX; break;
                            case VK_TAB: key = InputKey_ToggleDebugTriangles; break;
                            case 'O': key = InputKey_ToggleOutlines; break;
                            case 'T': key = InputKey_ToggleSW; break;
                            case VK_ESCAPE: key = InputKey_ToggleDebugUI; break;

                            case VK_F4: {
                                keyFound = false;
                                if (msg.message == WM_SYSKEYDOWN) {
                                    running = false;
                                }
                            } break;

                            default: keyFound = false; break;
                        }
                        if (keyFound) {
                            if (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN) {
                                inputKeyDown(&state->input, key);
                            } else {
                                inputKeyUp(&state->input, key);
                            }
                        }
                    } break;

                    case WM_INPUT: {
                        RAWINPUT input = {};
                        UINT     size = sizeof(RAWINPUT);
                        assert(GetRawInputData((HRAWINPUT)msg.lParam, RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER)));
                        if (input.header.dwType == RIM_TYPEMOUSE && input.data.mouse.usFlags == MOUSE_MOVE_RELATIVE) {
                            state->input.mouse.dx = input.data.mouse.lLastX;
                            state->input.mouse.dy = input.data.mouse.lLastY;
                        }
                    } break;

                    case WM_MOUSEMOVE: break;

                    default: {
                        TranslateMessage(&msg);
                        DispatchMessage(&msg);
                    } break;
                }
            }

            {
                POINT point = {};
                GetCursorPos(&point);
                state->input.mouse.x = point.x;
                state->input.mouse.y = point.y;
            }

            timedSectionEnd();
        }

        bool prevShowDebugUI = state->showDebugUI;
        {
            ClockMarker now = getClockMarker();
            f32         ms = getMsBetween(timer.clock, timer.update, now);
            timer.update = now;
            update(state, ms / 1000.0f);
        }
        if (prevShowDebugUI != state->showDebugUI) {
            ShowCursor(state->showDebugUI);
        }

        if (state->useSW) {
            swRender(state);
            d3d11blit(d3d11blitter, state->swRenderer.texture);
        } else {
            d3d11render(d3d11renderer, state);
        }

        nk_clear(&state->ui);

        timedSectionEnd();
    }

    // NOTE(khvorov) Had 2 bluescreens on Win10 before adding this
    {
        ID3D11Buffer_Release(d3d11renderer.mesh.vbuffer);
        ID3D11Buffer_Release(d3d11renderer.mesh.ibuffer);
        ID3D11Buffer_Release(d3d11renderer.mesh.colorBuffer);
        ID3D11VertexShader_Release(d3d11renderer.mesh.vsps.vs);
        ID3D11InputLayout_Release(d3d11renderer.mesh.vsps.layout);
        ID3D11PixelShader_Release(d3d11renderer.mesh.vsps.ps);
        ID3D11Buffer_Release(d3d11renderer.mesh.constCamera);
        ID3D11Buffer_Release(d3d11renderer.mesh.constMesh);

        ID3D11RasterizerState_Release(d3d11renderer.rasterizerState);
        ID3D11RasterizerState_Release(d3d11renderer.rasterizerNoCull);
        ID3D11DepthStencilState_Release(d3d11renderer.depthSpencilState);
        ID3D11DepthStencilView_Release(d3d11renderer.depthStencilView);

        ID3D11Buffer_Release(d3d11renderer.triFilled.vertices);
        ID3D11VertexShader_Release(d3d11renderer.triFilled.vsps.vs);
        ID3D11InputLayout_Release(d3d11renderer.triFilled.vsps.layout);
        ID3D11PixelShader_Release(d3d11renderer.triFilled.vsps.ps);
        ID3D11Buffer_Release(d3d11renderer.triFilled.constDims);

        ID3D11Buffer_Release(d3d11renderer.font.vertices);
        ID3D11VertexShader_Release(d3d11renderer.font.vsps.vs);
        ID3D11InputLayout_Release(d3d11renderer.font.vsps.layout);
        ID3D11PixelShader_Release(d3d11renderer.font.vsps.ps);
        ID3D11ShaderResourceView_Release(d3d11renderer.font.textureView);
        ID3D11SamplerState_Release(d3d11renderer.font.sampler);
        ID3D11BlendState_Release(d3d11renderer.font.blend);
        ID3D11Buffer_Release(d3d11renderer.font.constDims);

        ID3D11Buffer_Release(d3d11blitter.vbuffer);
        ID3D11VertexShader_Release(d3d11blitter.vsps.vs);
        ID3D11PixelShader_Release(d3d11blitter.vsps.ps);
        ID3D11InputLayout_Release(d3d11blitter.vsps.layout);
        ID3D11Texture1D_Release(d3d11blitter.tex.tex2d);
        ID3D11ShaderResourceView_Release(d3d11blitter.tex.view);
        ID3D11SamplerState_Release(d3d11blitter.sampler);
        ID3D11RasterizerState_Release(d3d11blitter.rasterizerState);

        IDXGISwapChain1_Release(d3d11common.swapChain);
        ID3D11RenderTargetView_Release(d3d11common.rtView);
        ID3D11Device_Release(d3d11common.device);
        ID3D11DeviceContext_Release(d3d11common.context);
    }

#ifdef TRIAXIS_profile
    {
        ClockMarker beforeSleep = getClockMarker();
        u64         begin = __rdtsc();
        Sleep(100);
        u64         end = __rdtsc();
        ClockMarker afterSleep = getClockMarker();
        f64         waited = getMsBetween(timer.clock, beforeSleep, afterSleep);
        u64         ticksu = end - begin;
        f64         ticksf = (f64)ticksu;
        f64         microseconds = waited * 1000.0f;
        f64         rdtscFreqPerMicrosecond = ticksf / microseconds;

        SpallHeader header = {
            .magic_header = 0x0BADF00D,
            .version = 1,
            .timestamp_unit = 1.0 / rdtscFreqPerMicrosecond,
            .must_be_0 = 0,
        };
        DWORD written = 0;
        WriteFile(
            profileFileHandle,
            &header,
            sizeof(header),
            &written,
            NULL
        );
        assert(written == sizeof(header));

        isize toWrite = globalSpallBuffer.head;
        assert(toWrite < 2ll * Gigabyte);
        written = 0;
        WriteFile(
            profileFileHandle,
            globalSpallBuffer.data,
            toWrite,
            &written,
            NULL
        );
        assert(written == toWrite);
        CloseHandle(profileFileHandle);
    }
#endif

    return 0;
}

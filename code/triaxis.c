#pragma clang diagnostic ignored "-Wunused-function"

#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>
#include <immintrin.h>

#include "TracyC.h"

// TODO(khvorov) Remove
#include <math.h>

// clang-format off
#define PI 3.14159
#define function static
#define arrayCount(x) (int)(sizeof(x) / sizeof(x[0]))
#define unused(x) (x) = (x)
#define min(x, y) (((x) < (y)) ? (x) : (y))
#define max(x, y) (((x) > (y)) ? (x) : (y))
#define absval(x) ((x) >= 0 ? (x) : -(x))
#define arenaAllocArray(arena, type, count) (type*)arenaAlloc(arena, sizeof(type)*count, alignof(type))
#define arenaAllocCap(arena, type, maxbytes, arr) arr.cap = maxbytes / sizeof(type); arr.ptr = arenaAllocArray(arena, type, arr.cap)
#define arrpush(arr, val) (((arr).len < (arr).cap) ? ((arr).ptr[(arr).len] = val, (arr).len++) : (__debugbreak(), 0))
#define arrget(arr, i) (arr.ptr[((i) < (arr).len ? (i) : (__debugbreak(), 0))])

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
typedef intptr_t isize;
typedef float    f32;

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
memeq(void* ptr1, void* ptr2, isize len) {
    bool result = true;
    for (isize ind = 0; ind < len; ind++) {
        if (((u8*)ptr1)[ind] != ((u8*)ptr2)[ind]) {
            result = false;
            break;
        }
    }
    return result;
}

const __mmask64 globalTailByteMasks512[64] = {
    0x0000000000000000,
    0x0000000000000001,
    0x0000000000000003,
    0x0000000000000007,
    0x000000000000000f,
    0x000000000000001f,
    0x000000000000003f,
    0x000000000000007f,
    0x00000000000000ff,
    0x00000000000001ff,
    0x00000000000003ff,
    0x00000000000007ff,
    0x0000000000000fff,
    0x0000000000001fff,
    0x0000000000003fff,
    0x0000000000007fff,
    0x000000000000ffff,
    0x000000000001ffff,
    0x000000000003ffff,
    0x000000000007ffff,
    0x00000000000fffff,
    0x00000000001fffff,
    0x00000000003fffff,
    0x00000000007fffff,
    0x0000000000ffffff,
    0x0000000001ffffff,
    0x0000000003ffffff,
    0x0000000007ffffff,
    0x000000000fffffff,
    0x000000001fffffff,
    0x000000003fffffff,
    0x000000007fffffff,
    0x00000000ffffffff,
    0x00000001ffffffff,
    0x00000003ffffffff,
    0x00000007ffffffff,
    0x0000000fffffffff,
    0x0000001fffffffff,
    0x0000003fffffffff,
    0x0000007fffffffff,
    0x000000ffffffffff,
    0x000001ffffffffff,
    0x000003ffffffffff,
    0x000007ffffffffff,
    0x00000fffffffffff,
    0x00001fffffffffff,
    0x00003fffffffffff,
    0x00007fffffffffff,
    0x0000ffffffffffff,
    0x0001ffffffffffff,
    0x0003ffffffffffff,
    0x0007ffffffffffff,
    0x000fffffffffffff,
    0x001fffffffffffff,
    0x003fffffffffffff,
    0x007fffffffffffff,
    0x00ffffffffffffff,
    0x01ffffffffffffff,
    0x03ffffffffffffff,
    0x07ffffffffffffff,
    0x0fffffffffffffff,
    0x1fffffffffffffff,
    0x3fffffffffffffff,
    0x7fffffffffffffff,
};

function void
zeromem(void* ptr, isize len) {
    isize    wholeCount = len / sizeof(__m512i);
    __m512i* ptr512 = (__m512i*)ptr;

    {
        __m512i zero512 = _mm512_setzero_si512();
        for (isize ind = 0; ind < wholeCount; ind++) {
            _mm512_storeu_si512(ptr512 + ind, zero512);
        }
    }

    {
        __m512i   zeros512 = _mm512_set1_epi8(0);
        isize     remaining = len - wholeCount * sizeof(__m512i);
        __mmask64 tailMask = globalTailByteMasks512[remaining];
        _mm512_mask_storeu_epi8(ptr512 + wholeCount, tailMask, zeros512);
    }
}

function void
copymem(void* dest, void* src, isize len) {
    assert((src < dest && src + len <= dest) || (dest < src && dest + len <= src));

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

#define STR(x) ((Str) {.ptr = x, .len = sizeof(x) - 1})
typedef struct Str {
    char* ptr;
    isize len;
} Str;

function bool
streq(Str s1, Str s2) {
    bool result = false;
    if (s1.len == s2.len) {
        result = memeq(s1.ptr, s2.ptr, s1.len);
    }
    return result;
}

typedef struct StrBuilder {
    char* ptr;
    isize len;
    isize cap;
} StrBuilder;

function void
fmtStr(StrBuilder* builder, Str str) {
    assert(builder->len + str.len <= builder->cap);
    copymem(builder->ptr + builder->len, str.ptr, str.len);
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
    assert(val >= 0);

    if (spec.padChar == '\0') {
        spec.padChar = ' ';
    }

    isize pow10 = 1;
    isize digitCount = 1;
    for (isize valCopy = val / 10; valCopy / pow10 > 0; pow10 *= 10, digitCount++) {}

    if (spec.disallowOverflow && spec.chars < digitCount) {
        assert(!"overflow");
    }

    if (spec.align == FmtAlign_Right) {
        while (spec.chars > digitCount) {
            arrpush(*builder, spec.padChar);
            spec.chars -= 1;
        }
    }

    for (isize curVal = val; pow10 > 0 && spec.chars > 0; pow10 /= 10, spec.chars--) {
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
    assert(val >= 0);

    isize maxValLeft = 1;
    for (isize ind = 0; ind < spec.charsLeft; ind++) {
        maxValLeft *= 10;
    }

    isize maxValRight = 1;
    for (isize ind = 0; ind < spec.charsRight; ind++) {
        maxValRight *= 10;
    }

    bool  overflow = false;
    isize whole = (isize)val;
    if (whole > maxValLeft - 1) {
        overflow = true;
        f32 epsilon = 1.0f / (f32)maxValRight;
        val = maxValLeft - epsilon;
        whole = (isize)val;
    }
    assert(whole <= maxValLeft - 1);

    f32   frac = val - (f32)whole;
    isize fracInt = (isize)((frac * (f32)(maxValRight)) + 0.5f);

    fmtInt(builder, whole, (FmtInt) {.chars = spec.charsLeft, .align = FmtAlign_Right, .disallowOverflow = true});
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

function f32
squaref(f32 val) {
    f32 result = val * val;
    return result;
}

function f32
lerp(f32 start, f32 end, f32 by) {
    f32 result = start + (end - start) * by;
    return result;
}

function f32
degreesToRadians(f32 degrees) {
    f32 result = degrees / 180 * PI;
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
createRotor3fAnglePlane(f32 angleDegrees, f32 xy, f32 xz, f32 yz) {
    f32 area = sqrtf(squaref(xy) + squaref(xz) + squaref(yz));
    f32 angleRadians = degreesToRadians(angleDegrees);
    f32 halfa = angleRadians / 2;
    f32 sina = sinf(halfa);
    f32 cosa = cosf(halfa);

    Rotor3f result = {
        .dt = cosa,
        .xy = -xy / area * sina,
        .xz = -xz / area * sina,
        .yz = -yz / area * sina,
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
    f32 l = sqrtf(squaref(r.dt) + squaref(r.xy) + squaref(r.xz) + squaref(r.yz));
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
    Color255 result = {.r = (color >> 16) & 0xff, .g = (color >> 8) & 0xff, .b = color & 0xff, .a = color >> 24};
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
        .r = lerp(c1.r, c2.r, by),
        .g = lerp(c1.g, c2.g, by),
        .b = lerp(c1.b, c2.b, by),
        .a = lerp(c1.a, c2.a, by),
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
    f32     fovDegreesX;
    Rotor3f orientation;
    f32     moveWUPerSec;
    f32     rotDegreesPerSec;
} Camera;

function Camera
createCamera(V3f pos) {
    Camera camera = {.fovDegreesX = 90, .pos = pos, .orientation = createRotor3f(), .moveWUPerSec = 1, .rotDegreesPerSec = 70};
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
    InputKey_RotateXY,
    InputKey_RotateYX,
    InputKey_RotateXZ,
    InputKey_RotateZX,
    InputKey_RotateYZ,
    InputKey_RotateZY,
    InputKey_ToggleDebugTriangles,
    InputKey_Count,
} InputKey;

typedef struct KeyState {
    bool down;
    i32  halfTransitionCount;
} KeyState;

typedef struct Input {
    KeyState keys[InputKey_Count];
} Input;

function void
inputBeginFrame(Input* input) {
    for (i32 keyIndex = 0; keyIndex < InputKey_Count; keyIndex++) {
        KeyState* state = input->keys + keyIndex;
        state->halfTransitionCount = 0;
    }
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
// SECTION SWRenderer
//

typedef struct Texture {
    u32*  ptr;
    isize width;
    isize height;
} Texture;

typedef struct SWRenderer {
    struct {
        u32*  ptr;
        isize width;
        isize height;
        isize cap;
    } image;

    MeshStorage triangles;
} SWRenderer;

function SWRenderer
createSWRenderer(Arena* arena, isize bytes) {
    SWRenderer renderer = {};

    isize forImage = bytes / 4 * 3;
    isize forTriangles = bytes - forImage;

    arenaAllocCap(arena, u32, forImage, renderer.image);
    renderer.triangles = createMeshStorage(arena, forTriangles);

    return renderer;
}

function void
swRendererSetImageSize(SWRenderer* renderer, isize width, isize height) {
    isize pixelCount = width * height;
    assert(pixelCount <= renderer->image.cap);
    renderer->image.width = width;
    renderer->image.height = height;
}

function void
swRendererClearImage(SWRenderer* renderer) {
    zeromem(renderer->image.ptr, renderer->image.width * renderer->image.height * sizeof(u32));
}

typedef struct Triangle {
    V2f  v1, v2, v3;
    f32  area;
    bool isBehind;
} Triangle;

function Triangle
swRendererPullTriangle(SWRenderer* renderer, TriangleIndices trig) {
    V3f v1og = arrget(renderer->triangles.vertices, trig.i1);
    V3f v2og = arrget(renderer->triangles.vertices, trig.i2);
    V3f v3og = arrget(renderer->triangles.vertices, trig.i3);

    V2f imageDim = {(f32)renderer->image.width, (f32)renderer->image.height};

    V2f v1 = v2fhadamard(v1og.xy, imageDim);
    V2f v2 = v2fhadamard(v2og.xy, imageDim);
    V2f v3 = v2fhadamard(v3og.xy, imageDim);

    f32 area = edgeWedge(v1, v2, v3);

    Triangle result = {v1, v2, v3, area, v1og.z < 0 || v2og.z < 0 || v3og.z < 0};
    return result;
}

function void
swRendererFillTriangle(SWRenderer* renderer, TriangleIndices trig) {
    Triangle tr = swRendererPullTriangle(renderer, trig);

    V2f v1 = tr.v1;
    V2f v2 = tr.v2;
    V2f v3 = tr.v3;

    if (tr.area > 0 && !tr.isBehind) {
        Color01 c1 = arrget(renderer->triangles.colors, trig.i1);
        Color01 c2 = arrget(renderer->triangles.colors, trig.i2);
        Color01 c3 = arrget(renderer->triangles.colors, trig.i3);

        f32 xmin = min(v1.x, min(v2.x, v3.x));
        f32 ymin = min(v1.y, min(v2.y, v3.y));
        f32 xmax = max(v1.x, max(v2.x, v3.x));
        f32 ymax = max(v1.y, max(v2.y, v3.y));

        bool allowZero1 = isTopLeft(v1, v2);
        bool allowZero2 = isTopLeft(v2, v3);
        bool allowZero3 = isTopLeft(v3, v1);

        f32 dcross1x = v1.y - v2.y;
        f32 dcross2x = v2.y - v3.y;
        f32 dcross3x = v3.y - v1.y;

        f32 dcross1y = v2.x - v1.x;
        f32 dcross2y = v3.x - v2.x;
        f32 dcross3y = v1.x - v3.x;

        i32 ystart = max((i32)ymin, 0);
        i32 xstart = max((i32)xmin, 0);
        i32 yend = min((i32)ymax, renderer->image.height - 1);
        i32 xend = min((i32)xmax, renderer->image.width - 1);

        // TODO(khvorov) Are constant increments actually faster than just computing the edge cross every time?
        V2f topleft = {(f32)(xstart), (f32)(ystart)};
        f32 cross1topleft = edgeWedge(v1, v2, topleft);
        f32 cross2topleft = edgeWedge(v2, v3, topleft);
        f32 cross3topleft = edgeWedge(v3, v1, topleft);

        for (i32 ycoord = ystart; ycoord <= yend; ycoord++) {
            f32 yinc = (f32)(ycoord - ystart);
            f32 cross1row = cross1topleft + yinc * dcross1y;
            f32 cross2row = cross2topleft + yinc * dcross2y;
            f32 cross3row = cross3topleft + yinc * dcross3y;

            for (i32 xcoord = xstart; xcoord <= xend; xcoord++) {
                f32 xinc = (f32)(xcoord - xstart);
                f32 cross1 = cross1row + xinc * dcross1x;
                f32 cross2 = cross2row + xinc * dcross2x;
                f32 cross3 = cross3row + xinc * dcross3x;

                bool pass1 = cross1 > 0 || (cross1 == 0 && allowZero1);
                bool pass2 = cross2 > 0 || (cross2 == 0 && allowZero2);
                bool pass3 = cross3 > 0 || (cross3 == 0 && allowZero3);

                if (pass1 && pass2 && pass3) {
                    f32 cross1scaled = cross1 / tr.area;
                    f32 cross2scaled = cross2 / tr.area;
                    f32 cross3scaled = cross3 / tr.area;

                    Color01 color01 = color01add(color01add(color01scale(c1, cross2scaled), color01scale(c2, cross3scaled)), color01scale(c3, cross1scaled));

                    i32 index = ycoord * renderer->image.width + xcoord;
                    assert(index < renderer->image.width * renderer->image.height);

                    u32      existingColoru32 = renderer->image.ptr[index];
                    Color255 existingColor255 = coloru32to255(existingColoru32);
                    Color01  existingColor01 = color255to01(existingColor255);

                    Color01 blended01 = {
                        .r = lerp(existingColor01.r, color01.r, color01.a),
                        .g = lerp(existingColor01.g, color01.g, color01.a),
                        .b = lerp(existingColor01.b, color01.b, color01.a),
                        .a = 1,
                    };

                    Color255 blended255 = color01to255(blended01);
                    u32      blendedu32 = color255tou32(blended255);
                    renderer->image.ptr[index] = blendedu32;
                }
            }
        }
    }
}

function void
swRendererContrast(SWRenderer* renderer, isize row, isize col) {
    assert(row >= 0 && col >= 0 && row < renderer->image.height && col < renderer->image.width);
    isize index = row * renderer->image.width + col;
    u32   oldVal = renderer->image.ptr[index];
    u32   inverted = coloru32GetContrast(oldVal);
    renderer->image.ptr[index] = inverted;
}

function void
swRendererDrawContrastLine(SWRenderer* renderer, V2f v1, V2f v2) {
    i32 x0 = (i32)(v1.x + 0.5);
    i32 y0 = (i32)(v1.y + 0.5);
    i32 x1 = (i32)(v2.x + 0.5);
    i32 y1 = (i32)(v2.y + 0.5);

    i32 dx = absval(x1 - x0);
    i32 sx = x0 < x1 ? 1 : -1;
    i32 dy = -absval(y1 - y0);
    i32 sy = y0 < y1 ? 1 : -1;
    i32 error = dx + dy;

    for (;;) {
        if (y0 >= 0 && x0 >= 0 && y0 < renderer->image.height && x0 < renderer->image.width) {
            swRendererContrast(renderer, y0, x0);
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
swRendererDrawContrastRect(SWRenderer* renderer, Rect2f rect) {
    Rect2f rectClipped = rect2fClip(rect, (Rect2f) {{-0.5, -0.5}, {renderer->image.width, renderer->image.height}});
    i32    x0 = (i32)(rectClipped.topleft.x + 0.5);
    i32    x1 = (i32)(rectClipped.topleft.x + rectClipped.dim.x + 0.5);
    i32    y0 = (i32)(rectClipped.topleft.y + 0.5);
    i32    y1 = (i32)(rectClipped.topleft.y + rectClipped.dim.y + 0.5);

    for (i32 ycoord = y0; ycoord < y1; ycoord++) {
        for (i32 xcoord = x0; xcoord < x1; xcoord++) {
            swRendererContrast(renderer, ycoord, xcoord);
        }
    }
}

function void
swRendererOutlineTriangle(SWRenderer* renderer, TriangleIndices trig) {
    Triangle tr = swRendererPullTriangle(renderer, trig);

    V2f v1 = tr.v1;
    V2f v2 = tr.v2;
    V2f v3 = tr.v3;

    if (tr.area > 0 && !tr.isBehind) {
        swRendererDrawContrastLine(renderer, v1, v2);
        swRendererDrawContrastLine(renderer, v2, v3);
        swRendererDrawContrastLine(renderer, v3, v1);

        V2f vertexRectDim = {10, 10};
        swRendererDrawContrastRect(renderer, rect2fCenterDim(v1, vertexRectDim));
        swRendererDrawContrastRect(renderer, rect2fCenterDim(v2, vertexRectDim));
        swRendererDrawContrastRect(renderer, rect2fCenterDim(v3, vertexRectDim));
    }
}

function void
swRendererFillTriangles(SWRenderer* renderer) {
    for (i32 ind = 0; ind < renderer->triangles.indices.len; ind++) {
        TriangleIndices trig = renderer->triangles.indices.ptr[ind];
        swRendererFillTriangle(renderer, trig);
    }
}

function void
swRendererOutlineTriangles(SWRenderer* renderer) {
    for (i32 ind = 0; ind < renderer->triangles.indices.len; ind++) {
        TriangleIndices trig = renderer->triangles.indices.ptr[ind];
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
    SWRendererMeshBuilder builder = {renderer, renderer->triangles.vertices.len, renderer->triangles.indices.len};
    return builder;
}

function void
swRendererEndMesh(SWRendererMeshBuilder builder) {
    for (i32 indexIndex = builder.firstIndexIndex; indexIndex < builder.renderer->triangles.indices.len; indexIndex++) {
        TriangleIndices* trig = builder.renderer->triangles.indices.ptr + indexIndex;
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
            Rotor3f cameraRotationRev = rotor3fReverse(camera.orientation);
            V3f     rot = rotor3fRotateV3f(cameraRotationRev, trans);
            vtxCamera = rot;
        }

        V3f vtxScreen = {};
        {
            V2f plane = {vtxCamera.x / vtxCamera.z, vtxCamera.y / vtxCamera.z};

            f32 fovRadiansX = degreesToRadians(camera.fovDegreesX);

            f32 planeRight = tan(0.5f * fovRadiansX);
            f32 planeLeft = -planeRight;
            f32 planeTop = ((f32)renderer->image.height / (f32)renderer->image.width) * planeRight;
            f32 planeBottom = -planeTop;

            V2f screen = {
                (plane.x - planeLeft) / (planeRight - planeLeft),
                (plane.y - planeTop) / (planeBottom - planeTop),
            };

            vtxScreen = (V3f) {.xy = screen, .z_ = vtxCamera.z};
        }

        arrpush(renderer->triangles.vertices, vtxScreen);
        arrpush(renderer->triangles.colors, arrget(mesh.colors, ind));
    }

    for (i32 ind = 0; ind < mesh.indices.len; ind++) {
        TriangleIndices trig = mesh.indices.ptr[ind];
        meshStorageAddTriangle(&renderer->triangles, trig);
    }

    swRendererEndMesh(cubeInRendererBuilder);
}

function void
swRendererScaleOntoAPixelGrid(SWRenderer* renderer, isize width, isize height, Arena* scratch) {
    TempMemory temp = beginTempMemory(scratch);

    u32* currentImageCopy = arenaAllocArray(scratch, u32, renderer->image.width * renderer->image.height);

    for (isize ind = 0; ind < renderer->image.width * renderer->image.height; ind++) {
        currentImageCopy[ind] = renderer->image.ptr[ind];
    }

    isize scaleX = width / renderer->image.width;
    isize scaleY = height / renderer->image.height;

    isize oldWidth = renderer->image.width;
    isize oldHeight = renderer->image.height;
    swRendererSetImageSize(renderer, width, height);

    // NOTE(khvorov) Copy the scaled up image
    for (isize oldRow = 0; oldRow < oldHeight; oldRow++) {
        isize newRowStart = oldRow * scaleY;
        isize newRowEnd = newRowStart + scaleY;
        for (isize newRow = newRowStart; newRow < newRowEnd; newRow++) {
            for (isize oldColumn = 0; oldColumn < oldWidth; oldColumn++) {
                isize newColumnStart = oldColumn * scaleX;
                isize newColumnEnd = newColumnStart + scaleX;
                for (isize newColumn = newColumnStart; newColumn < newColumnEnd; newColumn++) {
                    isize oldIndex = oldRow * oldWidth + oldColumn;
                    u32   oldVal = currentImageCopy[oldIndex];
                    isize newIndex = newRow * width + newColumn;
                    renderer->image.ptr[newIndex] = oldVal;
                }
            }
        }
    }

    // NOTE(khvorov) Grid line
    for (isize oldRow = 0; oldRow < oldHeight; oldRow++) {
        isize topleftY = oldRow * scaleY;
        for (isize oldCol = 0; oldCol < oldWidth; oldCol++) {
            isize topleftX = oldCol * scaleX;
            for (isize toplineX = topleftX; toplineX < topleftX + scaleX; toplineX++) {
                swRendererContrast(renderer, topleftY, toplineX);
            }
            for (isize leftlineY = topleftY + 1; leftlineY < topleftY + scaleY; leftlineY++) {
                swRendererContrast(renderer, leftlineY, topleftX);
            }

            {
                isize centerY = topleftY + scaleY / 2;
                isize centerX = topleftX + scaleX / 2;
                swRendererContrast(renderer, centerY, centerX);
            }
        }
    }

    endTempMemory(temp);
}

function void
swRendererDrawDebugTriangles(SWRenderer* renderer, isize finalImageWidth, isize finalImageHeight, Arena* scratch) {
    // NOTE(khvorov) Debug triangles from
    // https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-rasterizer-stage-rules
    arrpush(renderer->triangles.vertices, ((V3f) {.x = 0.5, .y = 0.5, .z = 1}));
    arrpush(renderer->triangles.vertices, ((V3f) {.x = 5.5, .y = 1.5, .z = 1}));
    arrpush(renderer->triangles.vertices, ((V3f) {.x = 1.5, .y = 3.5, .z = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->triangles.vertices, ((V3f) {.x = 4, .y = 0, .z = 1}));
    arrpush(renderer->triangles.vertices, ((V3f) {.x = 4, .y = 0, .z = 1}));
    arrpush(renderer->triangles.vertices, ((V3f) {.x = 4, .y = 0, .z = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->triangles.vertices, ((V3f) {.x = 5.75, .y = -0.25, .z = 1}));
    arrpush(renderer->triangles.vertices, ((V3f) {.x = 5.75, .y = 0.75, .z = 1}));
    arrpush(renderer->triangles.vertices, ((V3f) {.x = 4.75, .y = 0.75, .z = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->triangles.vertices, ((V3f) {.x = 7, .y = 0, .z = 1}));
    arrpush(renderer->triangles.vertices, ((V3f) {.x = 7, .y = 1, .z = 1}));
    arrpush(renderer->triangles.vertices, ((V3f) {.x = 6, .y = 1, .z = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->triangles.vertices, ((V3f) {.x = 7.25, .y = 2, .z = 1}));
    arrpush(renderer->triangles.vertices, ((V3f) {.x = 9.25, .y = 0.25, .z = 1}));
    arrpush(renderer->triangles.vertices, ((V3f) {.x = 11.25, .y = 2, .z = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->triangles.vertices, ((V3f) {.x = 7.25, .y = 2, .z = 1}));
    arrpush(renderer->triangles.vertices, ((V3f) {.x = 11.25, .y = 2, .z = 1}));
    arrpush(renderer->triangles.vertices, ((V3f) {.x = 9, .y = 4.75, .z = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .g = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .g = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .g = 1}));

    arrpush(renderer->triangles.vertices, ((V3f) {.x = 13, .y = 1, .z = 1}));
    arrpush(renderer->triangles.vertices, ((V3f) {.x = 14.5, .y = -0.5, .z = 1}));
    arrpush(renderer->triangles.vertices, ((V3f) {.x = 14, .y = 2, .z = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->triangles.vertices, ((V3f) {.x = 13, .y = 1, .z = 1}));
    arrpush(renderer->triangles.vertices, ((V3f) {.x = 14, .y = 2, .z = 1}));
    arrpush(renderer->triangles.vertices, ((V3f) {.x = 14, .y = 4, .z = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->triangles.vertices, ((V3f) {.x = 0.5, .y = 5.5, .z = 1}));
    arrpush(renderer->triangles.vertices, ((V3f) {.x = 6.5, .y = 3.5, .z = 1}));
    arrpush(renderer->triangles.vertices, ((V3f) {.x = 4.5, .y = 5.5, .z = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->triangles.vertices, ((V3f) {.x = 4.5, .y = 5.5, .z = 1}));
    arrpush(renderer->triangles.vertices, ((V3f) {.x = 6.5, .y = 3.5, .z = 1}));
    arrpush(renderer->triangles.vertices, ((V3f) {.x = 7.5, .y = 6.5, .z = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .g = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .g = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .g = 1}));

    arrpush(renderer->triangles.vertices, ((V3f) {.x = 6.5, .y = 3.5, .z = 1}));
    arrpush(renderer->triangles.vertices, ((V3f) {.x = 9, .y = 5, .z = 1}));
    arrpush(renderer->triangles.vertices, ((V3f) {.x = 7.5, .y = 6.5, .z = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->triangles.vertices, ((V3f) {.x = 9, .y = 7, .z = 1}));
    arrpush(renderer->triangles.vertices, ((V3f) {.x = 10, .y = 7, .z = 1}));
    arrpush(renderer->triangles.vertices, ((V3f) {.x = 9, .y = 9, .z = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->triangles.vertices, ((V3f) {.x = 11, .y = 4, .z = 1}));
    arrpush(renderer->triangles.vertices, ((V3f) {.x = 12, .y = 5, .z = 1}));
    arrpush(renderer->triangles.vertices, ((V3f) {.x = 11, .y = 6, .z = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->triangles.vertices, ((V3f) {.x = 13, .y = 5, .z = 1}));
    arrpush(renderer->triangles.vertices, ((V3f) {.x = 15, .y = 5, .z = 1}));
    arrpush(renderer->triangles.vertices, ((V3f) {.x = 13, .y = 7, .z = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->triangles.vertices, ((V3f) {.x = 15, .y = 5, .z = 1}));
    arrpush(renderer->triangles.vertices, ((V3f) {.x = 15, .y = 7, .z = 1}));
    arrpush(renderer->triangles.vertices, ((V3f) {.x = 13, .y = 7, .z = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .g = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .g = 1}));
    arrpush(renderer->triangles.colors, ((Color01) {.a = 0.5, .g = 1}));

    assert(renderer->triangles.colors.len == renderer->triangles.vertices.len);
    assert(renderer->triangles.vertices.len % 3 == 0);

    isize imageWidth = 16;
    isize imageHeight = 8;
    for (isize ind = 0; ind < renderer->triangles.vertices.len; ind++) {
        renderer->triangles.vertices.ptr[ind].xy = v2fhadamard(renderer->triangles.vertices.ptr[ind].xy, (V2f) {1.0f / (f32)imageWidth, 1.0f / (f32)imageHeight});
    }

    for (isize ind = 0; ind < renderer->triangles.vertices.len; ind += 3) {
        TriangleIndices trig = {ind, ind + 1, ind + 2};
        meshStorageAddTriangle(&renderer->triangles, trig);
    }

    swRendererSetImageSize(renderer, imageWidth, imageHeight);
    swRendererClearImage(renderer);
    swRendererFillTriangles(renderer);
    swRendererScaleOntoAPixelGrid(renderer, finalImageWidth, finalImageHeight, scratch);

    // NOTE(khvorov) Fill triangles on the pixel grid - vertices have to be shifted to correspond
    // to their positions in the smaller image
    {
        isize imageScaleX = finalImageWidth / imageWidth;
        isize imageScaleY = finalImageHeight / imageHeight;

        V2f offset = v2fhadamard(v2fscale((V2f) {(f32)imageScaleX, (f32)imageScaleY}, 0.5), (V2f) {1.0f / (f32)finalImageWidth, 1.0f / (f32)finalImageHeight});
        for (isize ind = 0; ind < renderer->triangles.vertices.len; ind++) {
            renderer->triangles.vertices.ptr[ind].xy = v2fadd(renderer->triangles.vertices.ptr[ind].xy, offset);
        }

        swRendererOutlineTriangles(renderer);

        for (isize ind = 0; ind < renderer->triangles.vertices.len; ind++) {
            renderer->triangles.vertices.ptr[ind].xy = v2fsub(renderer->triangles.vertices.ptr[ind].xy, offset);
        }
    }
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
    u8* ptr;
    i32 width;
    i32 height;
    i32 advance;
} Glyph;

typedef struct Font {
    Glyph ascii[128];
    i32   lineAdvance;
} Font;

function void
initFont(Font* font, Arena* arena) {
    font->lineAdvance = 16;
    for (u8 ch = 0; ch < 128; ch++) {
        Glyph* glyph = font->ascii + ch;
        glyph->width = 8;
        glyph->height = 16;
        glyph->advance = 8;
        glyph->ptr = arenaAllocArray(arena, u8, glyph->width * glyph->height);

        u8* glyphBitmap = (u8*)globalFontData + ch * glyph->height;
        for (isize row = 0; row < glyph->height; row++) {
            u8 glyphRowBitmap = glyphBitmap[row];
            for (isize col = 0; col < glyph->width; col++) {
                u8    mask = 1 << col;
                u8    glyphRowMasked = glyphRowBitmap & mask;
                isize glyphIndex = row * glyph->width + col;
                glyph->ptr[glyphIndex] = 0;
                if (glyphRowMasked) {
                    glyph->ptr[glyphIndex] = 0xFF;
                }
            }
        }
    }
}

function void
drawGlyph(Glyph glyph, Texture dest, i32 x, i32 y, Color01 color) {
    for (i32 row = 0; row < glyph.height; row++) {
        i32 destRow = y + row;
        assert(destRow < dest.height);
        for (i32 col = 0; col < glyph.width; col++) {
            i32 destCol = x + col;
            assert(destCol < dest.width);

            i32      index = row * glyph.width + col;
            u8       alpha = glyph.ptr[index];
            Color255 og255 = {.r = alpha, .g = alpha, .b = alpha, .a = 255};
            Color01  og01 = color255to01(og255);
            Color01  dest01 = color01mul(og01, color);
            Color255 dest255 = color01to255(dest01);
            u32      dest32 = color255tou32(dest255);

            i32 destIndex = destRow * dest.width + destCol;
            dest.ptr[destIndex] = dest32;
        }
    }
}

function void
drawStr(Font* font, Str str, Texture dest, i32 top, Color01 color) {
    i32 curX = 0;
    for (isize ind = 0; ind < str.len; ind++) {
        char  ch = str.ptr[ind];
        Glyph glyph = font->ascii[(u8)ch];
        drawGlyph(glyph, dest, curX, top, color);
        curX += glyph.advance;
    }
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
            copymem(thisDest, thisSrc, thisBufBytes);
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
            copymem(thisDest, thisSrc, thisBufBytes);
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
            copymem(thisDest, thisSrc, thisBufBytes);
            assert(memeq(thisSrc, thisDest, thisBufBytes));

            assert(guardBefore[0] == guardBeforeValue);
            assert(guardBetween[0] == guardBetweenValue);
            assert(guardAfter[0] == guardAfterValue);
        }
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
        assert(lerp(5, 15, 0.3) == 8);

        assert(degreesToRadians(0) == 0);
        assert(degreesToRadians(30) < degreesToRadians(60));

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
        assert(rotor3fEq(rotor3fNormalise((Rotor3f) {4, 4, 4, 4}), (Rotor3f) {0.5, 0.5, 0.5, 0.5}));
        assert(rotor3fEq(rotor3fReverse((Rotor3f) {1, 2, 3, 4}), (Rotor3f) {1, -2, -3, -4}));

        assert(rotor3fRotateV3f(createRotor3fAnglePlane(90, 1, 0, 0), (V3f) {.x = 1, 0, 0}).y == 1);
        assert(rotor3fRotateV3f(createRotor3fAnglePlane(90, -1, 0, 0), (V3f) {.x = 1, 0, 0}).y == -1);

        {
            Rotor3f r1 = createRotor3fAnglePlane(30, 1, 0, 0);
            Rotor3f r2 = createRotor3fAnglePlane(60, 1, 0, 0);
            Rotor3f rmul = rotor3fMulRotor3f(r1, r2);
            V3f     vrot = rotor3fRotateV3f(rmul, (V3f) {.x = 1, 0, 0});
            assert(absval(vrot.y - 1) < 0.001);
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
        MeshStorage store = createMeshStorage(arena, 1024 * 1024);

        Mesh cube1 = createCubeMesh(&store, 2, (V3f) {}, createRotor3f());
        assert(store.vertices.len == cube1.vertices.len);
        assert(store.indices.len == cube1.indices.len);
        assert(v3feq(cube1.vertices.ptr[0], (V3f) {.x = -1, 1, -1}));
        assert(cube1.indices.ptr[0].i1 == 0);

        Mesh cube2 = createCubeMesh(&store, 4, (V3f) {}, createRotor3f());
        assert(store.vertices.len == cube1.vertices.len + cube2.vertices.len);
        assert(store.indices.len == cube1.indices.len + cube2.indices.len);
        assert(cube2.vertices.ptr == cube1.vertices.ptr + cube1.vertices.len);
        assert(cube2.indices.ptr == cube1.indices.ptr + cube1.indices.len);
        assert(v3feq(cube2.vertices.ptr[0], (V3f) {.x = -2, 2, -2}));
        assert(cube2.indices.ptr[0].i1 == 0);
    }

    {
        StrBuilder builder = {};
        arenaAllocCap(arena, char, 1000, builder);

        fmtStr(&builder, STR("test"));
        assert(streq((Str) {builder.ptr, builder.len}, STR("test")));

        fmtStr(&builder, STR(" and 2 "));
        assert(streq((Str) {builder.ptr, builder.len}, STR("test and 2 ")));

        fmtInt(&builder, 123, (FmtInt) {.chars = 3});
        assert(streq((Str) {builder.ptr, builder.len}, STR("test and 2 123")));
        fmtStr(&builder, STR(" "));

        fmtInt(&builder, 0, (FmtInt) {.chars = 1});
        assert(streq((Str) {builder.ptr, builder.len}, STR("test and 2 123 0")));
        fmtStr(&builder, STR(" "));

        fmtF32(&builder, 123.4567, (FmtF32) {.charsLeft = 3, .charsRight = 2});
        assert(streq((Str) {builder.ptr, builder.len}, STR("test and 2 123 0 123.46")));

        fmtNull(&builder);
        assert(builder.ptr[builder.len - 1] == '\0');

        builder.len = 0;
        fmtInt(&builder, 123, (FmtInt) {.chars = 5, .align = FmtAlign_Left});
        assert(streq((Str) {builder.ptr, builder.len}, STR("123  ")));

        builder.len = 0;
        fmtF32(&builder, 123.0f, (FmtF32) {.charsLeft = 2, .charsRight = 2});
        assert(streq((Str) {builder.ptr, builder.len}, STR("99.9+")));
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
#if TRACY_ENABLE
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

            TracyCZoneN(tracyCtx, "bench copymem", true);
            copymem(arr1, arr2, toCopy);
            TracyCZoneEnd(tracyCtx);

            endTempMemory(temp);
        }
    }

    {
        isize toZero = arenaFreeSize(arena);

        for (isize ind = 0; ind < samples; ind++) {
            TempMemory temp = beginTempMemory(arena);

            TracyCZoneN(tracyCtx, "bench zeromem", true);
            zeromem(arenaFreePtr(arena), toZero);
            TracyCZoneEnd(tracyCtx);

            endTempMemory(temp);
        }
    }

    endTempMemory(runBenchTemp);
}

//
// SECTION App
//

typedef struct State {
    SWRenderer  renderer;
    MeshStorage meshStorage;
    Font        font;
    Arena       scratch;

    isize windowWidth;
    isize windowHeight;

    Camera camera;
    Input  input;
    Mesh   cube1;
    Mesh   cube2;
    bool   showDebugTriangles;
} State;

function State*
initState(void* mem, isize bytes) {
    assert(mem);
    assert(bytes > 0);

    Arena arena = {.base = mem, .size = bytes};
#ifdef TRIAXIS_tests
    runTests(&arena);
#endif
#ifdef TRIAXIS_bench
    runBench(&arena);
#endif

    State* state = arenaAllocArray(&arena, State, 1);

    initFont(&state->font, &arena);
    state->scratch = createArenaFromArena(&arena, 10 * 1024 * 1024);

    isize perSystem = arenaFreeSize(&arena) / 3;
    state->renderer = createSWRenderer(&arena, perSystem);
    state->meshStorage = createMeshStorage(&arena, perSystem);

    state->camera = createCamera((V3f) {.x = 0, 0, -3});
    state->input = (Input) {};

    state->cube1 = createCubeMesh(&state->meshStorage, 1, (V3f) {.x = 1, .y = 0, .z = 0}, createRotor3fAnglePlane(0, 1, 0, 0));
    state->cube2 = createCubeMesh(&state->meshStorage, 1, (V3f) {.x = -1, .y = 0, .z = 0}, createRotor3fAnglePlane(0, 0, 1, 0));

    state->showDebugTriangles = false;

    state->windowWidth = 1600;
    state->windowHeight = 800;

    return state;
}

function void
update(State* state, f32 deltaSec) {
    {
        f32 moveInc = state->camera.moveWUPerSec * deltaSec;

        if (state->input.keys[InputKey_Forward].down) {
            state->camera.pos = v3fadd(v3fscale(rotor3fRotateV3f(state->camera.orientation, (V3f) {.x = 0, 0, 1}), moveInc), state->camera.pos);
        }
        if (state->input.keys[InputKey_Back].down) {
            state->camera.pos = v3fadd(v3fscale(rotor3fRotateV3f(state->camera.orientation, (V3f) {.x = 0, 0, 1}), -moveInc), state->camera.pos);
        }
        if (state->input.keys[InputKey_Right].down) {
            state->camera.pos = v3fadd(v3fscale(rotor3fRotateV3f(state->camera.orientation, (V3f) {.x = 1, 0, 0}), moveInc), state->camera.pos);
        }
        if (state->input.keys[InputKey_Left].down) {
            state->camera.pos = v3fadd(v3fscale(rotor3fRotateV3f(state->camera.orientation, (V3f) {.x = 1, 0, 0}), -moveInc), state->camera.pos);
        }
        if (state->input.keys[InputKey_Up].down) {
            state->camera.pos = v3fadd(v3fscale(rotor3fRotateV3f(state->camera.orientation, (V3f) {.x = 0, 1, 0}), moveInc), state->camera.pos);
        }
        if (state->input.keys[InputKey_Down].down) {
            state->camera.pos = v3fadd(v3fscale(rotor3fRotateV3f(state->camera.orientation, (V3f) {.x = 0, 1, 0}), -moveInc), state->camera.pos);
        }
    }

    {
        f32 rotInc = state->camera.rotDegreesPerSec * deltaSec;

        if (state->input.keys[InputKey_RotateXY].down) {
            state->camera.orientation = rotor3fMulRotor3f(state->camera.orientation, createRotor3fAnglePlane(rotInc, 1, 0, 0));
        }
        if (state->input.keys[InputKey_RotateYX].down) {
            state->camera.orientation = rotor3fMulRotor3f(state->camera.orientation, createRotor3fAnglePlane(rotInc, -1, 0, 0));
        }
        if (state->input.keys[InputKey_RotateXZ].down) {
            state->camera.orientation = rotor3fMulRotor3f(state->camera.orientation, createRotor3fAnglePlane(rotInc, 0, 1, 0));
        }
        if (state->input.keys[InputKey_RotateZX].down) {
            state->camera.orientation = rotor3fMulRotor3f(state->camera.orientation, createRotor3fAnglePlane(rotInc, 0, -1, 0));
        }
        if (state->input.keys[InputKey_RotateYZ].down) {
            state->camera.orientation = rotor3fMulRotor3f(state->camera.orientation, createRotor3fAnglePlane(rotInc, 0, 0, 1));
        }
        if (state->input.keys[InputKey_RotateZY].down) {
            state->camera.orientation = rotor3fMulRotor3f(state->camera.orientation, createRotor3fAnglePlane(rotInc, 0, 0, -1));
        }
    }

    Rotor3f cubeRotation1 = createRotor3fAnglePlane(40 * deltaSec, 1, 1, 1);
    state->cube1.orientation = rotor3fMulRotor3f(state->cube1.orientation, cubeRotation1);
    Rotor3f cubeRotation2 = rotor3fReverse(cubeRotation1);
    state->cube2.orientation = rotor3fMulRotor3f(state->cube2.orientation, cubeRotation2);

    if (inputKeyWasPressed(&state->input, InputKey_ToggleDebugTriangles)) {
        state->showDebugTriangles = !state->showDebugTriangles;
    }
}

function void
render(State* state) {
    meshStorageClearBuffers(&state->renderer.triangles);
    if (state->showDebugTriangles) {
        swRendererDrawDebugTriangles(&state->renderer, state->windowWidth, state->windowHeight, &state->scratch);
    } else {
        swRendererPushMesh(&state->renderer, state->cube1, state->camera);
        swRendererPushMesh(&state->renderer, state->cube2, state->camera);
        swRendererSetImageSize(&state->renderer, state->windowWidth, state->windowHeight);
        swRendererClearImage(&state->renderer);
        swRendererFillTriangles(&state->renderer);
        swRendererOutlineTriangles(&state->renderer);
    }
}

//
// SECTION Platform
//

#undef function
#define WIN32_LEAN_AND_MEAN 1
#define VC_EXTRALEAN 1
#include <Windows.h>
#include <timeapi.h>

#define COBJMACROS
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>

#pragma comment(lib, "gdi32")
#pragma comment(lib, "user32")
#pragma comment(lib, "Winmm")
#pragma comment(lib, "d3d11")
#pragma comment(lib, "dxguid")
#pragma comment(lib, "d3dcompiler")

#define asserthr(x) assert(SUCCEEDED(x))

typedef struct Clock {
    LARGE_INTEGER freqPerSecond;
} Clock;

typedef struct ClockMarker {
    LARGE_INTEGER counter;
} ClockMarker;

static Clock
createClock(void) {
    Clock clock = {};
    QueryPerformanceFrequency(&clock.freqPerSecond);
    return clock;
}

static ClockMarker
getClockMarker(void) {
    ClockMarker marker = {};
    QueryPerformanceCounter(&marker.counter);
    return marker;
}

static f32
getMsFromMarker(Clock clock, ClockMarker marker) {
    ClockMarker now = getClockMarker();
    LONGLONG    diff = now.counter.QuadPart - marker.counter.QuadPart;
    f32         result = (f32)diff / (f32)clock.freqPerSecond.QuadPart * 1000.0f;
    return result;
}

typedef struct Timer {
    Clock       clock;
    ClockMarker update;
} Timer;

static f32
msSinceLastUpdate(Timer* timer) {
    f32 result = getMsFromMarker(timer->clock, timer->update);
    timer->update = getClockMarker();
    return result;
}

LRESULT CALLBACK
windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    LRESULT result = 0;
    switch (uMsg) {
        case WM_DESTROY: PostQuitMessage(0); break;
        case WM_ERASEBKGND: result = TRUE; break;  // NOTE(khvorov) Do nothing
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
        isize memSize = 1 * 1024 * 1024 * 1024;
        void* memBase = VirtualAlloc(0, memSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        assert(memBase);
        state = initState(memBase, memSize);
    }

    WNDCLASSEXW windowClass = {
        .cbSize = sizeof(WNDCLASSEXW),
        .lpfnWndProc = windowProc,
        .hInstance = hInstance,
        .lpszClassName = L"Triaxis",
        .hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH),
        .hCursor = LoadCursorA(NULL, IDC_ARROW),
        .hIcon = LoadIconA(NULL, IDI_APPLICATION),
    };
    assert(RegisterClassExW(&windowClass) != 0);

    // TODO(khvorov) Put exe path in the name
    HWND window = CreateWindowExW(
        WS_EX_APPWINDOW | WS_EX_NOREDIRECTIONBITMAP,
        windowClass.lpszClassName,
        L"Triaxis",
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

    // TODO(khvorov) Handle resizing

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

    ID3D11Device*        device = 0;
    ID3D11DeviceContext* context = 0;
    {
        UINT flags = 0;
        flags |= D3D11_CREATE_DEVICE_DEBUG;
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

    {
        ID3D11InfoQueue* info = 0;
        ID3D11Device_QueryInterface(device, &IID_ID3D11InfoQueue, (void**)&info);
        ID3D11InfoQueue_SetBreakOnSeverity(info, D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        ID3D11InfoQueue_SetBreakOnSeverity(info, D3D11_MESSAGE_SEVERITY_ERROR, TRUE);
        ID3D11InfoQueue_Release(info);
    }

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

        IDXGIFactory_MakeWindowAssociation(factory, window, DXGI_MWA_NO_ALT_ENTER);

        IDXGIFactory2_Release(factory);
        IDXGIAdapter_Release(dxgiAdapter);
        IDXGIDevice_Release(dxgiDevice);
    }

    struct Vertex {
        float position[2];
        float uv[2];
    };

    ID3D11Buffer* vbuffer = 0;
    {
        struct Vertex data[] = {
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
        ID3D11Device_CreateBuffer(device, &desc, &initial, &vbuffer);
    }

    ID3D11InputLayout*  layout = 0;
    ID3D11VertexShader* vshader = 0;
    ID3D11PixelShader*  pshader = 0;
    {
        D3D11_INPUT_ELEMENT_DESC desc[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(struct Vertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(struct Vertex, uv), D3D11_INPUT_PER_VERTEX_DATA, 0},
        };

        UINT flags = D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR | D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS;
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;

        ID3DBlob* verror = 0;
        ID3DBlob* vblob = 0;

        ID3DBlob* perror = 0;
        ID3DBlob* pblob = 0;
        {
            TempMemory temp = beginTempMemory(&state->scratch);

            Str hlsl = {};
            {
                void*         buf = arenaFreePtr(&state->scratch);
                HANDLE        handle = CreateFileW(L"code/shader.hlsl", GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
                DWORD         bytesRead = 0;
                LARGE_INTEGER filesize = {};
                GetFileSizeEx(handle, &filesize);
                ReadFile(handle, buf, filesize.QuadPart, &bytesRead, 0);
                assert(bytesRead == filesize.QuadPart);
                CloseHandle(handle);
                arenaChangeUsed(&state->scratch, bytesRead);
                hlsl = (Str) {(char*)buf, (isize)bytesRead};
            }

            HRESULT vresult = D3DCompile(hlsl.ptr, hlsl.len, NULL, NULL, NULL, "vs", "vs_5_0", flags, 0, &vblob, &verror);
            if (FAILED(vresult)) {
                char* msg = ID3D10Blob_GetBufferPointer(verror);
                OutputDebugStringA(msg);
                assert(!"failed to compile");
            }

            HRESULT presult = D3DCompile(hlsl.ptr, hlsl.len, NULL, NULL, NULL, "ps", "ps_5_0", flags, 0, &pblob, &perror);
            if (FAILED(presult)) {
                char* msg = ID3D10Blob_GetBufferPointer(perror);
                OutputDebugStringA(msg);
                assert(!"failed to compile");
            }
            endTempMemory(temp);
        }

        ID3D11Device_CreateVertexShader(device, ID3D10Blob_GetBufferPointer(vblob), ID3D10Blob_GetBufferSize(vblob), NULL, &vshader);
        ID3D11Device_CreatePixelShader(device, ID3D10Blob_GetBufferPointer(pblob), ID3D10Blob_GetBufferSize(pblob), NULL, &pshader);
        ID3D11Device_CreateInputLayout(device, desc, ARRAYSIZE(desc), ID3D10Blob_GetBufferPointer(vblob), ID3D10Blob_GetBufferSize(vblob), &layout);

        ID3D10Blob_Release(pblob);
        ID3D10Blob_Release(vblob);
    }

    ID3D11ShaderResourceView* textureView = 0;
    ID3D11Texture2D*          texture = 0;
    {
        // TODO(khvorov) Resizing
        swRendererSetImageSize(&state->renderer, state->windowWidth, state->windowHeight);
        D3D11_TEXTURE2D_DESC desc = {
            .Width = state->renderer.image.width,
            .Height = state->renderer.image.height,
            .MipLevels = 1,
            .ArraySize = 1,
            .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
            .SampleDesc = {.Count = 1, .Quality = 0},
            .Usage = D3D11_USAGE_DYNAMIC,
            .BindFlags = D3D11_BIND_SHADER_RESOURCE,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        };

        D3D11_SUBRESOURCE_DATA data = {
            .pSysMem = state->renderer.image.ptr,
            .SysMemPitch = state->renderer.image.width * sizeof(u32),
        };

        ID3D11Device_CreateTexture2D(device, &desc, &data, &texture);
        ID3D11Device_CreateShaderResourceView(device, (ID3D11Resource*)texture, NULL, &textureView);
    }

    ID3D11SamplerState* sampler = 0;
    {
        D3D11_SAMPLER_DESC desc = {
            .Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
            .AddressU = D3D11_TEXTURE_ADDRESS_WRAP,
            .AddressV = D3D11_TEXTURE_ADDRESS_WRAP,
            .AddressW = D3D11_TEXTURE_ADDRESS_WRAP,
        };

        ID3D11Device_CreateSamplerState(device, &desc, &sampler);
    }

    ID3D11RasterizerState* rasterizerState = 0;
    {
        D3D11_RASTERIZER_DESC desc = {
            .FillMode = D3D11_FILL_SOLID,
            .CullMode = D3D11_CULL_NONE,
        };
        ID3D11Device_CreateRasterizerState(device, &desc, &rasterizerState);
    }

    ID3D11DepthStencilState* depthState = 0;
    {
        D3D11_DEPTH_STENCIL_DESC desc = {
            .DepthEnable = FALSE,
            .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL,
            .DepthFunc = D3D11_COMPARISON_LESS,
            .StencilEnable = FALSE,
            .StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK,
            .StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK,
        };
        ID3D11Device_CreateDepthStencilState(device, &desc, &depthState);
    }

    ID3D11RenderTargetView* rtView = 0;
    ID3D11DepthStencilView* dsView = 0;
    {
        asserthr(IDXGISwapChain1_ResizeBuffers(swapChain, 0, state->windowWidth, state->windowHeight, DXGI_FORMAT_UNKNOWN, 0));

        // create RenderTarget view for new backbuffer texture
        ID3D11Texture2D* backbuffer;
        IDXGISwapChain1_GetBuffer(swapChain, 0, &IID_ID3D11Texture2D, (void**)&backbuffer);
        ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource*)backbuffer, NULL, &rtView);
        assert(rtView);
        ID3D11Texture2D_Release(backbuffer);

        D3D11_TEXTURE2D_DESC depthDesc = {
            .Width = state->windowWidth,
            .Height = state->windowHeight,
            .MipLevels = 1,
            .ArraySize = 1,
            .Format = DXGI_FORMAT_D32_FLOAT,
            .SampleDesc = {.Count = 1, .Quality = 0},
            .Usage = D3D11_USAGE_DEFAULT,
            .BindFlags = D3D11_BIND_DEPTH_STENCIL,
        };

        ID3D11Texture2D* depth = 0;
        ID3D11Device_CreateTexture2D(device, &depthDesc, NULL, &depth);
        ID3D11Device_CreateDepthStencilView(device, (ID3D11Resource*)depth, NULL, &dsView);
        ID3D11Texture2D_Release(depth);
    }

    D3D11_VIEWPORT viewport = {
        .TopLeftX = 0,
        .TopLeftY = 0,
        .Width = (FLOAT)state->windowWidth,
        .Height = (FLOAT)state->windowHeight,
        .MinDepth = 0,
        .MaxDepth = 1,
    };

#ifdef TRIAXIS_debuginfo
    // NOTE(khvorov) To prevent a white flash
    ShowWindow(window, SW_SHOWMINIMIZED);
#endif
    ShowWindow(window, SW_SHOWNORMAL);

    // NOTE(khvorov) Windows will sleep for random amounts of time if we don't do this
    {
        TIMECAPS caps = {};
        timeGetDevCaps(&caps, sizeof(TIMECAPS));
        timeBeginPeriod(caps.wPeriodMin);
    }

    Timer timer = {.clock = createClock(), .update = getClockMarker()};
    for (bool running = true; running;) {
        TracyCFrameMark;
        TracyCZoneN(tracyFrameCtx, "frame", true);

        assert(state->scratch.tempCount == 0);
        assert(state->scratch.used == 0);

        // NOTE(khvorov) Input
        {
            TracyCZoneN(tracyCtx, "input", true);

            inputBeginFrame(&state->input);
            for (MSG msg = {}; PeekMessageA(&msg, 0, 0, 0, PM_REMOVE);) {
                switch (msg.message) {
                    case WM_QUIT: running = false; break;

                    case WM_KEYDOWN:
                    case WM_KEYUP: {
                        InputKey key = 0;
                        bool     keyFound = true;
                        switch (msg.wParam) {
                            case 'W': key = InputKey_Forward; break;
                            case 'S': key = InputKey_Back; break;
                            case 'A': key = InputKey_Left; break;
                            case 'D': key = InputKey_Right; break;
                            case VK_SHIFT: key = InputKey_Up; break;
                            case VK_CONTROL: key = InputKey_Down; break;
                            case VK_UP: key = InputKey_RotateZY; break;
                            case VK_DOWN: key = InputKey_RotateYZ; break;
                            case VK_LEFT: key = InputKey_RotateXZ; break;
                            case VK_RIGHT: key = InputKey_RotateZX; break;
                            case 'Q': key = InputKey_RotateXY; break;
                            case 'E': key = InputKey_RotateYX; break;
                            case VK_TAB: key = InputKey_ToggleDebugTriangles; break;
                            default: keyFound = false; break;
                        }
                        if (keyFound) {
                            if (msg.message == WM_KEYDOWN) {
                                inputKeyDown(&state->input, key);
                            } else {
                                inputKeyUp(&state->input, key);
                            }
                        }
                    } break;

                    default: {
                        TranslateMessage(&msg);
                        DispatchMessage(&msg);
                    } break;
                }
            }

            TracyCZoneEnd(tracyCtx);
        }

        {
            TracyCZoneN(tracyCtx, "update", true);
            f32 ms = msSinceLastUpdate(&timer);
            update(state, ms / 1000.0f);
            TracyCZoneEnd(tracyCtx);
        }

        {
            TracyCZoneN(tracyCtx, "render", true);
            render(state);
            TracyCZoneEnd(tracyCtx);
        }

        // NOTE(khvorov) Present
        {
            {
                FLOAT color[] = {0.0f, 0.0, 0.0f, 1.f};
                ID3D11DeviceContext_ClearRenderTargetView(context, rtView, color);
                ID3D11DeviceContext_ClearDepthStencilView(context, dsView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.f, 0);
            }

            {
                ID3D11DeviceContext_IASetInputLayout(context, layout);
                ID3D11DeviceContext_IASetPrimitiveTopology(context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                UINT stride = sizeof(struct Vertex);
                UINT offset = 0;
                ID3D11DeviceContext_IASetVertexBuffers(context, 0, 1, &vbuffer, &stride, &offset);
            }

            ID3D11DeviceContext_VSSetShader(context, vshader, NULL, 0);

            ID3D11DeviceContext_RSSetViewports(context, 1, &viewport);
            ID3D11DeviceContext_RSSetState(context, rasterizerState);

            {
                D3D11_MAPPED_SUBRESOURCE mappedTexture = {};
                ID3D11DeviceContext_Map(context, (ID3D11Resource*)texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedTexture);
                u32* pixels = (u32*)mappedTexture.pData;
                TracyCZoneN(tracyCtx, "present copymem", true);
                copymem(pixels, state->renderer.image.ptr, state->renderer.image.width * state->renderer.image.height * sizeof(u32));
                TracyCZoneEnd(tracyCtx);

                ID3D11DeviceContext_Unmap(context, (ID3D11Resource*)texture, 0);

                ID3D11DeviceContext_PSSetSamplers(context, 0, 1, &sampler);
                ID3D11DeviceContext_PSSetShaderResources(context, 0, 1, &textureView);
                ID3D11DeviceContext_PSSetShader(context, pshader, NULL, 0);
            }

            ID3D11DeviceContext_OMSetDepthStencilState(context, depthState, 0);
            ID3D11DeviceContext_OMSetRenderTargets(context, 1, &rtView, dsView);

            ID3D11DeviceContext_IASetPrimitiveTopology(context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            ID3D11DeviceContext_Draw(context, 4, 0);

            HRESULT presentResult = IDXGISwapChain1_Present(swapChain, 1, 0);

            asserthr(presentResult);
            if (presentResult == DXGI_STATUS_OCCLUDED) {
                Sleep(10);
            }
        }

        TracyCZoneEnd(tracyFrameCtx);
    }

    return 0;
}
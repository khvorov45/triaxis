#ifndef TRIAXIS_H
#define TRIAXIS_H

#pragma clang diagnostic ignored "-Wunused-function"

#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>
#include <immintrin.h>

#include "TracyC.h"

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

#include "generated.c"

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

//
// SECTION Misc
//

typedef struct Texture {
    u32*  ptr;
    isize width;
    isize height;
} Texture;

#endif  // TRIAXIS_H

#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>

// TODO(khvorov) Remove
#include <math.h>

// clang-format off
#define PI 3.14159
#define function static
#define assert(cond) do { if (cond) {} else { __debugbreak(); }} while (0)
#define arrayCount(x) (int)(sizeof(x) / sizeof(x[0]))
#define unused(x) (x) = (x)
#define min(x, y) (((x) < (y)) ? (x) : (y))
#define max(x, y) (((x) > (y)) ? (x) : (y))
#define absval(x) ((x) >= 0 ? (x) : -(x))
#define arenaAllocArray(arena, type, count) (type*)arenaAlloc(arena, sizeof(type)*count, alignof(type))
#define arenaAllocCap(arena, type, maxbytes, arr) arr.cap = maxbytes / sizeof(type); arr.ptr = arenaAllocArray(arena, type, arr.cap)
#define arrpush(arr, val) (((arr).len < (arr).cap) ? ((arr).ptr[(arr).len] = val, (arr).len++) : (__debugbreak(), 0))
#define arrget(arr, i) (arr.ptr[((i) < (arr).len ? (i) : (__debugbreak(), 0))])
// clang-format on

typedef uint8_t  u8;
typedef int32_t  i32;
typedef uint32_t u32;
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
// SECTION Math
//

function f32
lerp(f32 start, f32 end, f32 by) {
    f32 result = start + (end - start) * by;
    return result;
}

typedef struct V2f {
    f32 x, y;
} V2f;

typedef struct V3f {
    f32 x, y, z;
} V3f;

function V2f
v2fsub(V2f v1, V2f v2) {
    V2f result = {v1.x - v2.x, v1.y - v2.y};
    return result;
}

function V3f
v3fadd(V3f v1, V3f v2) {
    V3f result = {v1.x + v2.x, v1.y + v2.y, v1.z + v2.z};
    return result;
}

function V3f
v3fsub(V3f v1, V3f v2) {
    V3f result = {v1.x - v2.x, v1.y - v2.y, v1.z - v2.z};
    return result;
}

function V3f
v3fmul(V3f v1, f32 by) {
    V3f result = {v1.x * by, v1.y * by, v1.z * by};
    return result;
}

function f32
v3fdot(V3f v1, V3f v2) {
    f32 result = v1.x * v2.x + v1.y * v2.y + v1.z * v2.z;
    return result;
}

function V2f
v2fadd(V2f v1, V2f v2) {
    V2f result = {v1.x + v2.x, v1.y + v2.y};
    return result;
}

function V2f
v2fmulv2f(V2f v1, V2f v2) {
    V2f result = {v1.x * v2.x, v1.y * v2.y};
    return result;
}

function V2f
v2fmulf32(V2f v1, f32 s1) {
    V2f result = {v1.x * s1, v1.y * s1};
    return result;
}

function V2f
v2fdiv(V2f v1, V2f v2) {
    V2f result = {v1.x / v2.x, v1.y / v2.y};
    return result;
}

function V2f
v2fhadamard(V2f v1, V2f v2) {
    V2f result = {v1.x * v2.x, v1.y * v2.y};
    return result;
}

// clang-format off
typedef struct M3x3f {
    f32 m11, m12, m13;
    f32 m21, m22, m23;
    f32 m31, m32, m33;
} M3x3f;
// clang-format on

function M3x3f
getRotationMat(f32 by, V3f axis) {
    f32 s = sin(by);
    f32 c = cos(by);
    f32 x = axis.x;
    f32 y = axis.y;
    f32 z = axis.z;

    // clang-format off
    M3x3f mat = {
        c + x * x * (1 - c),     x * y * (1 - c) - z * s, x * z * (1 - c) + y * s,
        y * x * (1 - c) + z * s, c + y * y * (1 - c),     y * z * (1 - c) - x * s,
        z * x * (1 - c) - y * s, z * y * (1 - c) + x * s, c + z * z * (1 - c),
    };
    // clang-format on

    return mat;
}

function V3f
m3x3fmulv3f(M3x3f m, V3f v) {
    V3f result = {
        .x = m.m11 * v.x + m.m12 * v.y + m.m13 * v.z,
        .y = m.m21 * v.x + m.m22 * v.y + m.m23 * v.z,
        .z = m.m31 * v.x + m.m32 * v.y + m.m33 * v.z,
    };
    return result;
}

typedef struct Rect2f {
    V2f topleft;
    V2f dim;
} Rect2f;

function Rect2f
rect2fCenterDim(V2f center, V2f dim) {
    Rect2f result = {v2fsub(center, v2fmulv2f(dim, (V2f) {0.5, 0.5})), dim};
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
        .a = (u8)(color.a * 255.0f + 0.5f)};
    return result;
}

function Color01
color01add(Color01 c1, Color01 c2) {
    Color01 result = {.r = c1.r + c2.r, .g = c1.g + c2.g, .b = c1.b + c2.b, .a = c1.a + c2.a};
    return result;
}

function Color01
color01mul(Color01 c1, f32 by) {
    Color01 result = {.r = c1.r * by, .g = c1.g * by, .b = c1.b * by, .a = c1.a * by};
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
edgeCrossMag(V2f v1, V2f v2, V2f pt) {
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

typedef struct IndexArrDyn {
    i32*  ptr;
    isize len;
    isize cap;
} IndexArrDyn;

typedef struct Mesh {
    struct {
        V3f*  ptr;
        isize len;
    } vertices;
    struct {
        i32*  ptr;
        isize len;
    } indices;
} Mesh;

typedef struct MeshStorage {
    struct {
        V3f*  ptr;
        isize len;
        isize cap;
    } vertices;

    IndexArrDyn indices;
} MeshStorage;

typedef struct MeshBuilder {
    MeshStorage* storage;
    isize        vertexLenBefore;
    isize        indexLenBefore;
} MeshBuilder;

function MeshStorage
createMeshStorage(Arena* arena, isize bytes) {
    MeshStorage storage = {};
    isize       bytesPerBuffer = bytes / 2;
    arenaAllocCap(arena, V3f, bytesPerBuffer, storage.vertices);
    arenaAllocCap(arena, i32, bytesPerBuffer, storage.indices);
    return storage;
}

function MeshBuilder
beginMesh(MeshStorage* storage) {
    MeshBuilder builder = {storage, storage->vertices.len, storage->indices.len};
    return builder;
}

function Mesh
endMesh(MeshBuilder builder) {
    Mesh mesh = {
        .vertices.ptr = builder.storage->vertices.ptr + builder.vertexLenBefore,
        .vertices.len = builder.storage->vertices.len - builder.vertexLenBefore,
        .indices.ptr = builder.storage->indices.ptr + builder.indexLenBefore,
        .indices.len = builder.storage->indices.len - builder.indexLenBefore,
    };
    assert(mesh.vertices.len >= 0);
    assert(mesh.indices.len >= 0);
    for (i32 ind = 0; ind < mesh.indices.len; ind++) {
        mesh.indices.ptr[ind] -= builder.indexLenBefore;
    }
    return mesh;
}

function void
meshStorageAddTriangle(MeshStorage* storage, i32 i1, i32 i2, i32 i3) {
    assert(i1 < storage->vertices.len && i2 < storage->vertices.len && i3 < storage->vertices.len);
    arrpush(storage->indices, i1);
    arrpush(storage->indices, i2);
    arrpush(storage->indices, i3);
}

function void
meshStorageAddQuad(MeshStorage* storage, i32 i1, i32 i2, i32 i3, i32 i4) {
    meshStorageAddTriangle(storage, i1, i2, i3);
    meshStorageAddTriangle(storage, i1, i3, i4);
}

function Mesh
cubeCenterDim(MeshStorage* storage, V3f center, f32 dim) {
    f32         halfdim = dim / 2;
    MeshBuilder cubeBuilder = beginMesh(storage);

    V3f frontTopLeft = v3fadd(center, (V3f) {-halfdim, halfdim, -halfdim});
    V3f frontTopRight = v3fadd(center, (V3f) {halfdim, halfdim, -halfdim});
    V3f frontBottomLeft = v3fadd(center, (V3f) {-halfdim, -halfdim, -halfdim});
    V3f frontBottomRight = v3fadd(center, (V3f) {halfdim, -halfdim, -halfdim});

    V3f backTopLeft = v3fadd(center, (V3f) {-halfdim, halfdim, halfdim});
    V3f backTopRight = v3fadd(center, (V3f) {halfdim, halfdim, halfdim});
    V3f backBottomLeft = v3fadd(center, (V3f) {-halfdim, -halfdim, halfdim});
    V3f backBottomRight = v3fadd(center, (V3f) {halfdim, -halfdim, halfdim});

    i32 frontTopLeftIndex = arrpush(storage->vertices, frontTopLeft);
    i32 frontTopRightIndex = arrpush(storage->vertices, frontTopRight);
    i32 frontBottomLeftIndex = arrpush(storage->vertices, frontBottomLeft);
    i32 frontBottomRightIndex = arrpush(storage->vertices, frontBottomRight);
    i32 backTopLeftIndex = arrpush(storage->vertices, backTopLeft);
    i32 backTopRightIndex = arrpush(storage->vertices, backTopRight);
    i32 backBottomLeftIndex = arrpush(storage->vertices, backBottomLeft);
    i32 backBottomRightIndex = arrpush(storage->vertices, backBottomRight);

    meshStorageAddQuad(storage, frontTopLeftIndex, frontTopRightIndex, frontBottomRightIndex, frontBottomLeftIndex);
    meshStorageAddQuad(storage, frontTopRightIndex, backTopRightIndex, backBottomRightIndex, frontBottomRightIndex);
    meshStorageAddQuad(storage, frontTopLeftIndex, backTopLeftIndex, backTopRightIndex, frontTopRightIndex);
    meshStorageAddQuad(storage, frontTopLeftIndex, frontBottomLeftIndex, backBottomLeftIndex, backTopLeftIndex);
    meshStorageAddQuad(storage, frontBottomLeftIndex, frontBottomRightIndex, backBottomRightIndex, backBottomLeftIndex);
    meshStorageAddQuad(storage, backTopRightIndex, backTopLeftIndex, backBottomLeftIndex, backBottomRightIndex);

    Mesh cube = endMesh(cubeBuilder);
    return cube;
}

//
// SECTION Renderer
//

typedef struct Renderer {
    struct {
        u32*  ptr;
        isize width;
        isize height;
        isize cap;
    } image;

    struct {
        V2f*  ptr;
        isize len;
        isize cap;
    } vertices;

    struct {
        Color01* ptr;
        isize    len;
        isize    cap;
    } colors;

    IndexArrDyn indices;
    Arena       scratch;
} Renderer;

function Renderer
createRenderer(Arena* arena, isize bytes) {
    Renderer renderer = {};

    isize bytesForBuffers = bytes / 2;
    isize bufferSize = bytesForBuffers / 4;

    arenaAllocCap(arena, u32, bufferSize, renderer.image);
    arenaAllocCap(arena, V2f, bufferSize, renderer.vertices);
    arenaAllocCap(arena, Color01, bufferSize, renderer.colors);
    arenaAllocCap(arena, i32, bufferSize, renderer.indices);

    renderer.scratch = createArenaFromArena(arena, bytes - bytesForBuffers);

    return renderer;
}

function void
rendererClearBuffers(Renderer* renderer) {
    renderer->scratch.used = 0;
    renderer->vertices.len = 0;
    renderer->colors.len = 0;
    renderer->indices.len = 0;
}

function void
setImageSize(Renderer* renderer, isize width, isize height) {
    isize pixelCount = width * height;
    assert(pixelCount <= renderer->image.cap);
    renderer->image.width = width;
    renderer->image.height = height;
}

function void
clearImage(Renderer* renderer) {
    for (isize ind = 0; ind < renderer->image.width * renderer->image.height; ind++) {
        renderer->image.ptr[ind] = 0;
    }
}

function void
rendererPushTriangle(Renderer* renderer, i32 i1, i32 i2, i32 i3) {
    assert(i1 < renderer->vertices.len && i2 < renderer->vertices.len && i3 < renderer->vertices.len);
    assert(i1 < renderer->colors.len && i2 < renderer->colors.len && i3 < renderer->colors.len);
    arrpush(renderer->indices, i1);
    arrpush(renderer->indices, i2);
    arrpush(renderer->indices, i3);
}

function void
fillTriangle(Renderer* renderer, i32 i1, i32 i2, i32 i3) {
    V2f imageDim = {(f32)renderer->image.width, (f32)renderer->image.height};
    V2f v1 = v2fhadamard(arrget(renderer->vertices, i1), imageDim);
    V2f v2 = v2fhadamard(arrget(renderer->vertices, i2), imageDim);
    V2f v3 = v2fhadamard(arrget(renderer->vertices, i3), imageDim);

    Color01 c1 = arrget(renderer->colors, i1);
    Color01 c2 = arrget(renderer->colors, i2);
    Color01 c3 = arrget(renderer->colors, i3);

    f32 xmin = min(v1.x, min(v2.x, v3.x));
    f32 ymin = min(v1.y, min(v2.y, v3.y));
    f32 xmax = max(v1.x, max(v2.x, v3.x));
    f32 ymax = max(v1.y, max(v2.y, v3.y));

    bool allowZero1 = isTopLeft(v1, v2);
    bool allowZero2 = isTopLeft(v2, v3);
    bool allowZero3 = isTopLeft(v3, v1);

    f32 area = edgeCrossMag(v1, v2, v3);

    f32 dcross1x = v1.y - v2.y;
    f32 dcross2x = v2.y - v3.y;
    f32 dcross3x = v3.y - v1.y;

    f32 dcross1y = v2.x - v1.x;
    f32 dcross2y = v3.x - v2.x;
    f32 dcross3y = v1.x - v3.x;

    i32 ystart = (i32)ymin;
    i32 xstart = (i32)xmin;

    // TODO(khvorov) Are constant increments actually faster than just computing the edge cross every time?
    V2f topleft = {(f32)(xstart), (f32)(ystart)};
    f32 cross1topleft = edgeCrossMag(v1, v2, topleft);
    f32 cross2topleft = edgeCrossMag(v2, v3, topleft);
    f32 cross3topleft = edgeCrossMag(v3, v1, topleft);

    for (i32 ycoord = ystart; ycoord <= (i32)ymax; ycoord++) {
        if (ycoord >= 0 && ycoord < renderer->image.height) {
            f32 yinc = (f32)(ycoord - ystart);
            f32 cross1row = cross1topleft + yinc * dcross1y;
            f32 cross2row = cross2topleft + yinc * dcross2y;
            f32 cross3row = cross3topleft + yinc * dcross3y;

            for (i32 xcoord = xstart; xcoord <= (i32)xmax; xcoord++) {
                if (xcoord >= 0 && xcoord < renderer->image.width) {
                    f32 xinc = (f32)(xcoord - xstart);
                    f32 cross1 = cross1row + xinc * dcross1x;
                    f32 cross2 = cross2row + xinc * dcross2x;
                    f32 cross3 = cross3row + xinc * dcross3x;

                    bool pass1 = cross1 > 0 || (cross1 == 0 && allowZero1);
                    bool pass2 = cross2 > 0 || (cross2 == 0 && allowZero2);
                    bool pass3 = cross3 > 0 || (cross3 == 0 && allowZero3);

                    if (pass1 && pass2 && pass3) {
                        f32 cross1scaled = cross1 / area;
                        f32 cross2scaled = cross2 / area;
                        f32 cross3scaled = cross3 / area;

                        Color01 color01 = color01add(color01add(color01mul(c1, cross2scaled), color01mul(c2, cross3scaled)), color01mul(c3, cross1scaled));

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
}

function void
rendererContrast(Renderer* renderer, isize row, isize col) {
    assert(row >= 0 && col >= 0 && row < renderer->image.height && col < renderer->image.width);
    isize index = row * renderer->image.width + col;
    u32   oldVal = renderer->image.ptr[index];
    u32   inverted = coloru32GetContrast(oldVal);
    renderer->image.ptr[index] = inverted;
}

function void
drawContrastLine(Renderer* renderer, V2f v1, V2f v2) {
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
            rendererContrast(renderer, y0, x0);
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
drawContrastRect(Renderer* renderer, Rect2f rect) {
    Rect2f rectClipped = rect2fClip(rect, (Rect2f) {{-0.5, -0.5}, {renderer->image.width, renderer->image.height}});
    i32    x0 = (i32)(rectClipped.topleft.x + 0.5);
    i32    x1 = (i32)(rectClipped.topleft.x + rectClipped.dim.x + 0.5);
    i32    y0 = (i32)(rectClipped.topleft.y + 0.5);
    i32    y1 = (i32)(rectClipped.topleft.y + rectClipped.dim.y + 0.5);

    for (i32 ycoord = y0; ycoord < y1; ycoord++) {
        for (i32 xcoord = x0; xcoord < x1; xcoord++) {
            rendererContrast(renderer, ycoord, xcoord);
        }
    }
}

function void
outlineTriangle(Renderer* renderer, i32 i1, i32 i2, i32 i3) {
    V2f imageDim = {(f32)renderer->image.width, (f32)renderer->image.height};
    V2f v1 = v2fhadamard(arrget(renderer->vertices, i1), imageDim);
    V2f v2 = v2fhadamard(arrget(renderer->vertices, i2), imageDim);
    V2f v3 = v2fhadamard(arrget(renderer->vertices, i3), imageDim);

    drawContrastLine(renderer, v1, v2);
    drawContrastLine(renderer, v2, v3);
    drawContrastLine(renderer, v3, v1);

    V2f vertexRectDim = {10, 10};
    drawContrastRect(renderer, rect2fCenterDim(v1, vertexRectDim));
    drawContrastRect(renderer, rect2fCenterDim(v2, vertexRectDim));
    drawContrastRect(renderer, rect2fCenterDim(v3, vertexRectDim));
}

function void
rendererFillTriangles(Renderer* renderer) {
    for (i32 start = 0; start < renderer->indices.len; start += 3) {
        fillTriangle(renderer, arrget(renderer->indices, start), arrget(renderer->indices, start + 1), arrget(renderer->indices, start + 2));
    }
}

function void
rendererOutlineTriangles(Renderer* renderer) {
    for (i32 start = 0; start < renderer->indices.len; start += 3) {
        outlineTriangle(renderer, arrget(renderer->indices, start), arrget(renderer->indices, start + 1), arrget(renderer->indices, start + 2));
    }
}

typedef struct RendererMeshBuilder {
    Renderer* renderer;
    i32       firstVertexIndex;
    i32       firstIndexIndex;
} RendererMeshBuilder;

function RendererMeshBuilder
rendererBeginMesh(Renderer* renderer) {
    RendererMeshBuilder builder = {renderer, renderer->vertices.len, renderer->indices.len};
    return builder;
}

function void
rendererEndMesh(RendererMeshBuilder builder) {
    for (i32 indexIndex = builder.firstIndexIndex; indexIndex < builder.renderer->indices.len; indexIndex++) {
        builder.renderer->indices.ptr[indexIndex] += builder.firstVertexIndex;
    }
}

function void
scaleOntoAPixelGrid(Renderer* renderer, isize width, isize height) {
    TempMemory temp = beginTempMemory(&renderer->scratch);

    u32* currentImageCopy = arenaAllocArray(&renderer->scratch, u32, renderer->image.width * renderer->image.height);

    for (isize ind = 0; ind < renderer->image.width * renderer->image.height; ind++) {
        currentImageCopy[ind] = renderer->image.ptr[ind];
    }

    isize scaleX = width / renderer->image.width;
    isize scaleY = height / renderer->image.height;

    isize oldWidth = renderer->image.width;
    isize oldHeight = renderer->image.height;
    setImageSize(renderer, width, height);

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
                rendererContrast(renderer, topleftY, toplineX);
            }
            for (isize leftlineY = topleftY + 1; leftlineY < topleftY + scaleY; leftlineY++) {
                rendererContrast(renderer, leftlineY, topleftX);
            }

            {
                isize centerY = topleftY + scaleY / 2;
                isize centerX = topleftX + scaleX / 2;
                rendererContrast(renderer, centerY, centerX);
            }
        }
    }

    endTempMemory(temp);
}

function void
drawDebugTriangles(Renderer* renderer, isize finalImageWidth, isize finalImageHeight) {
    // NOTE(khvorov) Debug triangles from
    // https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-rasterizer-stage-rules
    arrpush(renderer->vertices, ((V2f) {0.5, 0.5}));
    arrpush(renderer->vertices, ((V2f) {5.5, 1.5}));
    arrpush(renderer->vertices, ((V2f) {1.5, 3.5}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->vertices, ((V2f) {4, 0}));
    arrpush(renderer->vertices, ((V2f) {4, 0}));
    arrpush(renderer->vertices, ((V2f) {4, 0}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->vertices, ((V2f) {5.75, -0.25}));
    arrpush(renderer->vertices, ((V2f) {5.75, 0.75}));
    arrpush(renderer->vertices, ((V2f) {4.75, 0.75}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->vertices, ((V2f) {7, 0}));
    arrpush(renderer->vertices, ((V2f) {7, 1}));
    arrpush(renderer->vertices, ((V2f) {6, 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->vertices, ((V2f) {7.25, 2}));
    arrpush(renderer->vertices, ((V2f) {9.25, 0.25}));
    arrpush(renderer->vertices, ((V2f) {11.25, 2}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->vertices, ((V2f) {7.25, 2}));
    arrpush(renderer->vertices, ((V2f) {11.25, 2}));
    arrpush(renderer->vertices, ((V2f) {9, 4.75}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .g = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .g = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .g = 1}));

    arrpush(renderer->vertices, ((V2f) {13, 1}));
    arrpush(renderer->vertices, ((V2f) {14.5, -0.5}));
    arrpush(renderer->vertices, ((V2f) {14, 2}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->vertices, ((V2f) {13, 1}));
    arrpush(renderer->vertices, ((V2f) {14, 2}));
    arrpush(renderer->vertices, ((V2f) {14, 4}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->vertices, ((V2f) {0.5, 5.5}));
    arrpush(renderer->vertices, ((V2f) {6.5, 3.5}));
    arrpush(renderer->vertices, ((V2f) {4.5, 5.5}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->vertices, ((V2f) {4.5, 5.5}));
    arrpush(renderer->vertices, ((V2f) {6.5, 3.5}));
    arrpush(renderer->vertices, ((V2f) {7.5, 6.5}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .g = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .g = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .g = 1}));

    arrpush(renderer->vertices, ((V2f) {6.5, 3.5}));
    arrpush(renderer->vertices, ((V2f) {9, 5}));
    arrpush(renderer->vertices, ((V2f) {7.5, 6.5}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->vertices, ((V2f) {9, 7}));
    arrpush(renderer->vertices, ((V2f) {10, 7}));
    arrpush(renderer->vertices, ((V2f) {9, 9}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->vertices, ((V2f) {11, 4}));
    arrpush(renderer->vertices, ((V2f) {12, 5}));
    arrpush(renderer->vertices, ((V2f) {11, 6}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->vertices, ((V2f) {13, 5}));
    arrpush(renderer->vertices, ((V2f) {15, 5}));
    arrpush(renderer->vertices, ((V2f) {13, 7}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->vertices, ((V2f) {15, 5}));
    arrpush(renderer->vertices, ((V2f) {15, 7}));
    arrpush(renderer->vertices, ((V2f) {13, 7}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .g = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .g = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .g = 1}));

    assert(renderer->colors.len == renderer->vertices.len);

    isize imageWidth = 16;
    isize imageHeight = 8;
    for (isize ind = 0; ind < renderer->vertices.len; ind++) {
        renderer->vertices.ptr[ind] = v2fdiv(renderer->vertices.ptr[ind], (V2f) {(f32)imageWidth, (f32)imageHeight});
    }

    for (isize ind = 0; ind < renderer->vertices.len; ind += 3) {
        rendererPushTriangle(renderer, ind, ind + 1, ind + 2);
    }

    setImageSize(renderer, imageWidth, imageHeight);
    clearImage(renderer);
    rendererFillTriangles(renderer);
    scaleOntoAPixelGrid(renderer, finalImageWidth, finalImageHeight);

    // NOTE(khvorov) Fill triangles on the pixel grid - vertices have to be shifted to correspond
    // to their positions in the smaller image
    {
        isize imageScaleX = finalImageWidth / imageWidth;
        isize imageScaleY = finalImageHeight / imageHeight;

        V2f offset = v2fdiv(v2fmulf32((V2f) {(f32)imageScaleX, (f32)imageScaleY}, 0.5), (V2f) {(f32)finalImageWidth, (f32)finalImageHeight});
        for (isize ind = 0; ind < renderer->vertices.len; ind++) {
            renderer->vertices.ptr[ind] = v2fadd(renderer->vertices.ptr[ind], offset);
        }

        rendererOutlineTriangles(renderer);

        for (isize ind = 0; ind < renderer->vertices.len; ind++) {
            renderer->vertices.ptr[ind] = v2fsub(renderer->vertices.ptr[ind], offset);
        }
    }
}

//
// SECTION Camera
//

typedef struct Camera {
    V3f pos;
    f32 fovDegreesX;
    V3f axisX;
    V3f axisY;
    V3f axisZ;
} Camera;

function Camera
createCamera(void) {
    Camera camera = {.fovDegreesX = 90, .axisX = {.x = 1}, .axisY = {.y = 1}, .axisZ = {.z = 1}};
    return camera;
}

function void
cameraRotate(Camera* camera, f32 by, V3f axis) {
    M3x3f mat = getRotationMat(by, axis);
    camera->axisX = m3x3fmulv3f(mat, camera->axisX);
    camera->axisY = m3x3fmulv3f(mat, camera->axisY);
    camera->axisZ = m3x3fmulv3f(mat, camera->axisZ);
}

typedef enum InputKey {
    InputKey_Forward,
    InputKey_Back,
    InputKey_Left,
    InputKey_Right,
    InputKey_Up,
    InputKey_Down,
    InputKey_RotateXLeft,
    InputKey_RotateXRight,
    InputKey_RotateYLeft,
    InputKey_RotateYRight,
    InputKey_RotateZLeft,
    InputKey_RotateZRight,
    InputKey_Count,
} InputKey;

typedef struct Input {
    bool keysDown[InputKey_Count];
} Input;

//
// SECTION Platform
//

#undef function
#define WIN32_LEAN_AND_MEAN 1
#define VC_EXTRALEAN 1
#include <Windows.h>

LRESULT CALLBACK
windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    LRESULT result = 0;
    switch (uMsg) {
        case WM_DESTROY: PostQuitMessage(0); break;
        case WM_ERASEBKGND: result = TRUE; break;  // NOTE(khvorov) Do nothing
        default: result = DefWindowProc(hwnd, uMsg, wParam, lParam); break;
    }
    return result;
}

int WINAPI
WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    unused(hPrevInstance);
    unused(lpCmdLine);
    unused(nCmdShow);

    Renderer    renderer = {};
    MeshStorage meshStorage = {};
    {
        isize memSize = 1 * 1024 * 1024 * 1024;
        void* memBase = VirtualAlloc(0, memSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        assert(memBase);

        Arena arena = {.base = memBase, .size = memSize};

        renderer = createRenderer(&arena, arenaFreeSize(&arena) / 2);
        meshStorage = createMeshStorage(&arena, arenaFreeSize(&arena));
    }

    WNDCLASSEXA windowClass = {
        .cbSize = sizeof(WNDCLASSEXA),
        .lpfnWndProc = windowProc,
        .hInstance = hInstance,
        .lpszClassName = "Triaxis",
        .hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH),
        .hCursor = LoadCursorA(NULL, IDC_ARROW),
    };
    assert(RegisterClassExA(&windowClass) != 0);

    isize windowWidth = 1600;
    isize windowHeight = 800;
    HWND  window = CreateWindowExA(
        0,
        windowClass.lpszClassName,
        "Triaxis",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowWidth,
        windowHeight,
        NULL,
        NULL,
        hInstance,
        NULL
    );
    assert(window);
    HDC hdc = GetDC(window);

    // NOTE(khvorov) Adjust window size such that it's the client area that's the specified size, not the whole window with decorations
    {
        RECT rect = {};
        GetClientRect(window, &rect);
        isize width = rect.right - rect.left;
        isize height = rect.bottom - rect.top;
        isize dwidth = windowWidth - width;
        isize dheight = windowHeight - height;
        SetWindowPos(window, 0, rect.left, rect.top, windowWidth + dwidth, windowHeight + dheight, 0);
    }

    // TODO(khvorov) Hack is debug only
    ShowWindow(window, SW_SHOWMINIMIZED);
    ShowWindow(window, SW_SHOWNORMAL);

    Camera camera = createCamera();
    Input  input = {};
    Mesh   cube = cubeCenterDim(&meshStorage, (V3f) {0, 0, 3}, 1.5);

    for (;;) {
        for (MSG msg = {}; PeekMessageA(&msg, 0, 0, 0, PM_REMOVE);) {
            switch (msg.message) {
                case WM_QUIT: ExitProcess(0); break;

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
                        case VK_UP: key = InputKey_RotateXLeft; break;
                        case VK_DOWN: key = InputKey_RotateXRight; break;
                        case VK_LEFT: key = InputKey_RotateYLeft; break;
                        case VK_RIGHT: key = InputKey_RotateYRight; break;
                        case 'Q': key = InputKey_RotateZLeft; break;
                        case 'E': key = InputKey_RotateZRight; break;
                        default: keyFound = false; break;
                    }
                    if (keyFound) {
                        bool state = msg.message == WM_KEYDOWN;
                        input.keysDown[key] = state;
                    }
                } break;

                default: {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                } break;
            }
        }

        {
            f32 cameraMovementInc = 0.1;
            f32 cameraRotationInc = 0.01;
            if (input.keysDown[InputKey_Forward]) {
                camera.pos = v3fadd(v3fmul(camera.axisZ, cameraMovementInc), camera.pos);
            }
            if (input.keysDown[InputKey_Back]) {
                camera.pos = v3fadd(v3fmul(camera.axisZ, -cameraMovementInc), camera.pos);
            }
            if (input.keysDown[InputKey_Left]) {
                camera.pos = v3fadd(v3fmul(camera.axisX, -cameraMovementInc), camera.pos);
            }
            if (input.keysDown[InputKey_Right]) {
                camera.pos = v3fadd(v3fmul(camera.axisX, cameraMovementInc), camera.pos);
            }
            if (input.keysDown[InputKey_Up]) {
                camera.pos = v3fadd(v3fmul(camera.axisY, cameraMovementInc), camera.pos);
            }
            if (input.keysDown[InputKey_Down]) {
                camera.pos = v3fadd(v3fmul(camera.axisY, -cameraMovementInc), camera.pos);
            }
            if (input.keysDown[InputKey_RotateXLeft]) {
                cameraRotate(&camera, cameraRotationInc, camera.axisX);
            }
            if (input.keysDown[InputKey_RotateXRight]) {
                cameraRotate(&camera, -cameraRotationInc, camera.axisX);
            }
            if (input.keysDown[InputKey_RotateYLeft]) {
                cameraRotate(&camera, -cameraRotationInc, camera.axisY);
            }
            if (input.keysDown[InputKey_RotateYRight]) {
                cameraRotate(&camera, cameraRotationInc, camera.axisY);
            }
            if (input.keysDown[InputKey_RotateZLeft]) {
                cameraRotate(&camera, cameraRotationInc, camera.axisZ);
            }
            if (input.keysDown[InputKey_RotateZRight]) {
                cameraRotate(&camera, -cameraRotationInc, camera.axisZ);
            }
        }

        rendererClearBuffers(&renderer);

        bool showDebugTriangles = false;
        if (showDebugTriangles) {
            drawDebugTriangles(&renderer, windowWidth, windowHeight);
        } else {
            RendererMeshBuilder cubeInRendererBuilder = rendererBeginMesh(&renderer);

            Color01 colors[] = {
                {.r = 1, .a = 1},
                {.g = 1, .a = 1},
                {.b = 1, .a = 1},
                {.r = 1, .g = 1, .a = 1},
                {.r = 1, .b = 1, .a = 1},
                {.g = 1, .b = 1, .a = 1},
                {.r = 1, .g = 1, .b = 1, .a = 1},
                {.r = 0.5, .g = 1, .a = 1},
            };
            for (i32 ind = 0; ind < cube.vertices.len; ind++) {
                V3f cubeVertex = cube.vertices.ptr[ind];

                V3f cubeVertexCameraTranslated = v3fsub(cubeVertex, camera.pos);

                V3f cubeVertexInCameraSpace = {
                    .x = v3fdot(camera.axisX, cubeVertexCameraTranslated),
                    .y = v3fdot(camera.axisY, cubeVertexCameraTranslated),
                    .z = v3fdot(camera.axisZ, cubeVertexCameraTranslated),
                };

                V2f cubeVertexOnPlane = {cubeVertexInCameraSpace.x / cubeVertexInCameraSpace.z, cubeVertexInCameraSpace.y / cubeVertexInCameraSpace.z};

                f32 fovRadiansX = camera.fovDegreesX / 180 * PI;

                f32 planeRight = tan(0.5f * fovRadiansX);
                f32 planeLeft = -planeRight;
                f32 planeTop = ((f32)windowHeight / (f32)windowWidth) * planeRight;
                f32 planeBottom = -planeTop;

                V2f cubeVertexOnScreen = {
                    (cubeVertexOnPlane.x - planeLeft) / (planeRight - planeLeft),
                    (cubeVertexOnPlane.y - planeTop) / (planeBottom - planeTop),
                };

                arrpush(renderer.vertices, cubeVertexOnScreen);
                assert(ind < arrayCount(colors));
                arrpush(renderer.colors, colors[ind]);
            }

            for (i32 ind = 0; ind < cube.indices.len; ind += 3) {
                assert(ind + 2 < cube.indices.len);
                i32 cubeIndex1 = cube.indices.ptr[ind];
                i32 cubeIndex2 = cube.indices.ptr[ind + 1];
                i32 cubeIndex3 = cube.indices.ptr[ind + 2];

                V2f cubeVertex1 = arrget(renderer.vertices, cubeIndex1 + cubeInRendererBuilder.firstVertexIndex);
                V2f cubeVertex2 = arrget(renderer.vertices, cubeIndex2 + cubeInRendererBuilder.firstVertexIndex);
                V2f cubeVertex3 = arrget(renderer.vertices, cubeIndex3 + cubeInRendererBuilder.firstVertexIndex);

                f32 area = edgeCrossMag(cubeVertex1, cubeVertex2, cubeVertex3);
                if (area > 0) {
                    rendererPushTriangle(&renderer, cubeIndex1, cubeIndex2, cubeIndex3);
                }
            }

            rendererEndMesh(cubeInRendererBuilder);

            setImageSize(&renderer, windowWidth, windowHeight);
            clearImage(&renderer);
            rendererFillTriangles(&renderer);
            rendererOutlineTriangles(&renderer);
        }

        // NOTE(khvorov) Present the bitmap
        {
            BITMAPINFO bmi = {
                .bmiHeader.biSize = sizeof(BITMAPINFOHEADER),
                .bmiHeader.biWidth = renderer.image.width,
                .bmiHeader.biHeight = -renderer.image.height,  // NOTE(khvorov) Top-down
                .bmiHeader.biPlanes = 1,
                .bmiHeader.biBitCount = 32,
                .bmiHeader.biCompression = BI_RGB,
            };
            StretchDIBits(
                hdc,
                0,
                0,
                windowWidth,
                windowHeight,
                0,
                0,
                renderer.image.width,
                renderer.image.height,
                renderer.image.ptr,
                &bmi,
                DIB_RGB_COLORS,
                SRCCOPY
            );
        }

        // TODO(khvorov) Better timing
        Sleep(10);

        // NOTE(khvorov) Move the shape to the cursor
        if (false) {
            V2f   refVertex = renderer.vertices.ptr[1];
            POINT cursor = {};
            GetCursorPos(&cursor);
            ScreenToClient(window, &cursor);
            f32 cursorImageX01 = ((f32)cursor.x / (f32)windowWidth);
            f32 dref = cursorImageX01 - refVertex.x;
            for (isize ind = 0; ind < renderer.vertices.len; ind++) {
                renderer.vertices.ptr[ind].x += dref;
            }
        }
    }

    return 0;
}

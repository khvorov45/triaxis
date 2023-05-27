#pragma clang diagnostic ignored "-Wunused-function"

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

typedef struct IndexArrDyn {
    i32*  ptr;
    isize len;
    isize cap;
} IndexArrDyn;

typedef struct V3fArrDyn {
    V3f*  ptr;
    isize len;
    isize cap;
} V3fArrDyn;

typedef struct Mesh {
    struct {
        V3f*  ptr;
        isize len;
    } vertices;

    struct {
        i32*  ptr;
        isize len;
    } indices;

    V3f     pos;
    Rotor3f orientation;
} Mesh;

typedef struct MeshStorage {
    IndexArrDyn indices;
    V3fArrDyn   vertices;
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
endMesh(MeshBuilder builder, V3f pos, Rotor3f orientation) {
    Mesh mesh = {
        .vertices.ptr = builder.storage->vertices.ptr + builder.vertexLenBefore,
        .vertices.len = builder.storage->vertices.len - builder.vertexLenBefore,
        .indices.ptr = builder.storage->indices.ptr + builder.indexLenBefore,
        .indices.len = builder.storage->indices.len - builder.indexLenBefore,
        .pos = pos,
        .orientation = orientation,
    };
    assert(mesh.vertices.len >= 0);
    assert(mesh.indices.len >= 0);
    for (i32 ind = 0; ind < mesh.indices.len; ind++) {
        mesh.indices.ptr[ind] -= builder.vertexLenBefore;
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
} Camera;

function Camera
createCamera(V3f pos) {
    Camera camera = {.fovDegreesX = 90, .pos = pos, .orientation = createRotor3f()};
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
    InputKey_Count,
} InputKey;

typedef struct Input {
    bool keysDown[InputKey_Count];
} Input;

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
        Color01* ptr;
        isize    len;
        isize    cap;
    } colors;

    V3fArrDyn   vertices;
    IndexArrDyn indices;
    Arena       scratch;
} Renderer;

function Renderer
createRenderer(Arena* arena, isize bytes) {
    Renderer renderer = {};

    isize bytesForBuffers = bytes / 2;
    isize bufferSize = bytesForBuffers / 4;

    arenaAllocCap(arena, u32, bufferSize, renderer.image);
    arenaAllocCap(arena, V3f, bufferSize, renderer.vertices);
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

typedef struct Triangle {
    V2f  v1, v2, v3;
    f32  area;
    bool isBehind;
} Triangle;

function Triangle
rendererPullTriangle(Renderer* renderer, i32 i1, i32 i2, i32 i3) {
    V3f v1og = arrget(renderer->vertices, i1);
    V3f v2og = arrget(renderer->vertices, i2);
    V3f v3og = arrget(renderer->vertices, i3);

    V2f imageDim = {(f32)renderer->image.width, (f32)renderer->image.height};

    V2f v1 = v2fhadamard(v1og.xy, imageDim);
    V2f v2 = v2fhadamard(v2og.xy, imageDim);
    V2f v3 = v2fhadamard(v3og.xy, imageDim);

    f32 area = edgeWedge(v1, v2, v3);

    Triangle result = {v1, v2, v3, area, v1og.z < 0 || v2og.z < 0 || v3og.z < 0};
    return result;
}

function void
rendererFillTriangle(Renderer* renderer, i32 i1, i32 i2, i32 i3) {
    Triangle tr = rendererPullTriangle(renderer, i1, i2, i3);

    V2f v1 = tr.v1;
    V2f v2 = tr.v2;
    V2f v3 = tr.v3;

    if (tr.area > 0 && !tr.isBehind) {
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
    Triangle tr = rendererPullTriangle(renderer, i1, i2, i3);

    V2f v1 = tr.v1;
    V2f v2 = tr.v2;
    V2f v3 = tr.v3;

    if (tr.area > 0 && !tr.isBehind) {
        drawContrastLine(renderer, v1, v2);
        drawContrastLine(renderer, v2, v3);
        drawContrastLine(renderer, v3, v1);

        V2f vertexRectDim = {10, 10};
        drawContrastRect(renderer, rect2fCenterDim(v1, vertexRectDim));
        drawContrastRect(renderer, rect2fCenterDim(v2, vertexRectDim));
        drawContrastRect(renderer, rect2fCenterDim(v3, vertexRectDim));
    }
}

function void
rendererFillTriangles(Renderer* renderer) {
    for (i32 start = 0; start < renderer->indices.len; start += 3) {
        assert(start + 2 < renderer->indices.len);
        rendererFillTriangle(renderer, renderer->indices.ptr[start], renderer->indices.ptr[start + 1], renderer->indices.ptr[start + 2]);
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
rendererPushMesh(Renderer* renderer, Mesh mesh, Camera camera) {
    RendererMeshBuilder cubeInRendererBuilder = rendererBeginMesh(renderer);

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

        arrpush(renderer->vertices, vtxScreen);
        assert(ind < arrayCount(colors));
        arrpush(renderer->colors, colors[ind]);
    }

    for (i32 ind = 0; ind < mesh.indices.len; ind += 3) {
        assert(ind + 2 < mesh.indices.len);
        i32 cubeIndex1 = mesh.indices.ptr[ind];
        i32 cubeIndex2 = mesh.indices.ptr[ind + 1];
        i32 cubeIndex3 = mesh.indices.ptr[ind + 2];
        rendererPushTriangle(renderer, cubeIndex1, cubeIndex2, cubeIndex3);
    }

    rendererEndMesh(cubeInRendererBuilder);
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
    arrpush(renderer->vertices, ((V3f) {.x = 0.5, .y = 0.5, .z = 1}));
    arrpush(renderer->vertices, ((V3f) {.x = 5.5, .y = 1.5, .z = 1}));
    arrpush(renderer->vertices, ((V3f) {.x = 1.5, .y = 3.5, .z = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->vertices, ((V3f) {.x = 4, .y = 0, .z = 1}));
    arrpush(renderer->vertices, ((V3f) {.x = 4, .y = 0, .z = 1}));
    arrpush(renderer->vertices, ((V3f) {.x = 4, .y = 0, .z = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->vertices, ((V3f) {.x = 5.75, .y = -0.25, .z = 1}));
    arrpush(renderer->vertices, ((V3f) {.x = 5.75, .y = 0.75, .z = 1}));
    arrpush(renderer->vertices, ((V3f) {.x = 4.75, .y = 0.75, .z = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->vertices, ((V3f) {.x = 7, .y = 0, .z = 1}));
    arrpush(renderer->vertices, ((V3f) {.x = 7, .y = 1, .z = 1}));
    arrpush(renderer->vertices, ((V3f) {.x = 6, .y = 1, .z = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->vertices, ((V3f) {.x = 7.25, .y = 2, .z = 1}));
    arrpush(renderer->vertices, ((V3f) {.x = 9.25, .y = 0.25, .z = 1}));
    arrpush(renderer->vertices, ((V3f) {.x = 11.25, .y = 2, .z = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->vertices, ((V3f) {.x = 7.25, .y = 2, .z = 1}));
    arrpush(renderer->vertices, ((V3f) {.x = 11.25, .y = 2, .z = 1}));
    arrpush(renderer->vertices, ((V3f) {.x = 9, .y = 4.75, .z = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .g = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .g = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .g = 1}));

    arrpush(renderer->vertices, ((V3f) {.x = 13, .y = 1, .z = 1}));
    arrpush(renderer->vertices, ((V3f) {.x = 14.5, .y = -0.5, .z = 1}));
    arrpush(renderer->vertices, ((V3f) {.x = 14, .y = 2, .z = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->vertices, ((V3f) {.x = 13, .y = 1, .z = 1}));
    arrpush(renderer->vertices, ((V3f) {.x = 14, .y = 2, .z = 1}));
    arrpush(renderer->vertices, ((V3f) {.x = 14, .y = 4, .z = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->vertices, ((V3f) {.x = 0.5, .y = 5.5, .z = 1}));
    arrpush(renderer->vertices, ((V3f) {.x = 6.5, .y = 3.5, .z = 1}));
    arrpush(renderer->vertices, ((V3f) {.x = 4.5, .y = 5.5, .z = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->vertices, ((V3f) {.x = 4.5, .y = 5.5, .z = 1}));
    arrpush(renderer->vertices, ((V3f) {.x = 6.5, .y = 3.5, .z = 1}));
    arrpush(renderer->vertices, ((V3f) {.x = 7.5, .y = 6.5, .z = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .g = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .g = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .g = 1}));

    arrpush(renderer->vertices, ((V3f) {.x = 6.5, .y = 3.5, .z = 1}));
    arrpush(renderer->vertices, ((V3f) {.x = 9, .y = 5, .z = 1}));
    arrpush(renderer->vertices, ((V3f) {.x = 7.5, .y = 6.5, .z = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->vertices, ((V3f) {.x = 9, .y = 7, .z = 1}));
    arrpush(renderer->vertices, ((V3f) {.x = 10, .y = 7, .z = 1}));
    arrpush(renderer->vertices, ((V3f) {.x = 9, .y = 9, .z = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->vertices, ((V3f) {.x = 11, .y = 4, .z = 1}));
    arrpush(renderer->vertices, ((V3f) {.x = 12, .y = 5, .z = 1}));
    arrpush(renderer->vertices, ((V3f) {.x = 11, .y = 6, .z = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->vertices, ((V3f) {.x = 13, .y = 5, .z = 1}));
    arrpush(renderer->vertices, ((V3f) {.x = 15, .y = 5, .z = 1}));
    arrpush(renderer->vertices, ((V3f) {.x = 13, .y = 7, .z = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .r = 1}));

    arrpush(renderer->vertices, ((V3f) {.x = 15, .y = 5, .z = 1}));
    arrpush(renderer->vertices, ((V3f) {.x = 15, .y = 7, .z = 1}));
    arrpush(renderer->vertices, ((V3f) {.x = 13, .y = 7, .z = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .g = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .g = 1}));
    arrpush(renderer->colors, ((Color01) {.a = 0.5, .g = 1}));

    assert(renderer->colors.len == renderer->vertices.len);

    isize imageWidth = 16;
    isize imageHeight = 8;
    for (isize ind = 0; ind < renderer->vertices.len; ind++) {
        renderer->vertices.ptr[ind].xy = v2fhadamard(renderer->vertices.ptr[ind].xy, (V2f) {1.0f / (f32)imageWidth, 1.0f / (f32)imageHeight});
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

        V2f offset = v2fhadamard(v2fscale((V2f) {(f32)imageScaleX, (f32)imageScaleY}, 0.5), (V2f) {1.0f / (f32)finalImageWidth, 1.0f / (f32)finalImageHeight});
        for (isize ind = 0; ind < renderer->vertices.len; ind++) {
            renderer->vertices.ptr[ind].xy = v2fadd(renderer->vertices.ptr[ind].xy, offset);
        }

        rendererOutlineTriangles(renderer);

        for (isize ind = 0; ind < renderer->vertices.len; ind++) {
            renderer->vertices.ptr[ind].xy = v2fsub(renderer->vertices.ptr[ind].xy, offset);
        }
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
        assert(v3feq(v3fadd((V3f) {.x = 1, 2, 3}, (V3f) {.x = 3, -5, 10}), (V3f) {.x = 4, -3, 13}));

        assert(v2feq(v2fsub((V2f) {1, 2}, (V2f) {3, -5}), (V2f) {-2, 7}));
        assert(v3feq(v3fsub((V3f) {.x = 1, 2, 3}, (V3f) {.x = 3, -5, 10}), (V3f) {.x = -2, 7, -7}));

        assert(v2feq(v2fhadamard((V2f) {1, 2}, (V2f) {3, 4}), (V2f) {3, 8}));
        assert(v3feq(v3fhadamard((V3f) {.x = 1, 2, 3}, (V3f) {.x = 3, 4, 5}), (V3f) {.x = 3, 8, 15}));

        assert(v2feq(v2fscale((V2f) {1, 2}, 5), (V2f) {5, 10}));
        assert(v3feq(v3fscale((V3f) {.x = 1, 2, 3}, 5), (V3f) {.x = 5, 10, 15}));

        assert(v2fdot((V2f) {1, 2}, (V2f) {4, 5}) == 14);
        assert(v3fdot((V3f) {.x = 1, 2, 3}, (V3f) {.x = 4, 5, 6}) == 32);
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
        assert(color01eq(color01mul((Color01) {1, 2, 3, 4}, 2), (Color01) {2, 4, 6, 8}));
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
        assert(cube1.indices.ptr[0] == 0);

        Mesh cube2 = createCubeMesh(&store, 4, (V3f) {}, createRotor3f());
        assert(store.vertices.len == cube1.vertices.len + cube2.vertices.len);
        assert(store.indices.len == cube1.indices.len + cube2.indices.len);
        assert(cube2.vertices.ptr == cube1.vertices.ptr + cube1.vertices.len);
        assert(cube2.indices.ptr == cube1.indices.ptr + cube1.indices.len);
        assert(v3feq(cube2.vertices.ptr[0], (V3f) {.x = -2, 2, -2}));
        assert(cube2.indices.ptr[0] == 0);
    }

    endTempMemory(temp);
}

//
// SECTION Platform
//

#undef function
#define WIN32_LEAN_AND_MEAN 1
#define VC_EXTRALEAN 1
#include <Windows.h>

typedef struct Clock {
    LARGE_INTEGER freqPerSecond;
} Clock;

typedef struct ClockMarker {
    LARGE_INTEGER counter;
} ClockMarker;

static Clock
getClock(void) {
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
        runTests(&arena);

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

    Camera camera = createCamera((V3f) {.x = 0, 0, -3});
    Input  input = {};

    Mesh cube1 = createCubeMesh(&meshStorage, 1, (V3f) {.x = 1, 0, 0}, createRotor3fAnglePlane(0, 1, 0, 0));
    Mesh cube2 = createCubeMesh(&meshStorage, 1, (V3f) {.x = -1, 0, 0}, createRotor3fAnglePlane(0, 0, 1, 0));

    f32         msPerFrameTarget = 1.0f / 60.0f * 1000.0f;
    Clock       clock = getClock();
    ClockMarker frameStart = getClockMarker();

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
                        case VK_UP: key = InputKey_RotateZY; break;
                        case VK_DOWN: key = InputKey_RotateYZ; break;
                        case VK_LEFT: key = InputKey_RotateXZ; break;
                        case VK_RIGHT: key = InputKey_RotateZX; break;
                        case 'Q': key = InputKey_RotateXY; break;
                        case 'E': key = InputKey_RotateYX; break;
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
            f32 cameraRotationInc = 1;

            if (input.keysDown[InputKey_Forward]) {
                camera.pos = v3fadd(v3fscale(rotor3fRotateV3f(camera.orientation, (V3f) {.x = 0, 0, 1}), cameraMovementInc), camera.pos);
            }
            if (input.keysDown[InputKey_Back]) {
                camera.pos = v3fadd(v3fscale(rotor3fRotateV3f(camera.orientation, (V3f) {.x = 0, 0, 1}), -cameraMovementInc), camera.pos);
            }
            if (input.keysDown[InputKey_Right]) {
                camera.pos = v3fadd(v3fscale(rotor3fRotateV3f(camera.orientation, (V3f) {.x = 1, 0, 0}), cameraMovementInc), camera.pos);
            }
            if (input.keysDown[InputKey_Left]) {
                camera.pos = v3fadd(v3fscale(rotor3fRotateV3f(camera.orientation, (V3f) {.x = 1, 0, 0}), -cameraMovementInc), camera.pos);
            }
            if (input.keysDown[InputKey_Up]) {
                camera.pos = v3fadd(v3fscale(rotor3fRotateV3f(camera.orientation, (V3f) {.x = 0, 1, 0}), cameraMovementInc), camera.pos);
            }
            if (input.keysDown[InputKey_Down]) {
                camera.pos = v3fadd(v3fscale(rotor3fRotateV3f(camera.orientation, (V3f) {.x = 0, 1, 0}), -cameraMovementInc), camera.pos);
            }

            if (input.keysDown[InputKey_RotateXY]) {
                camera.orientation = rotor3fMulRotor3f(camera.orientation, createRotor3fAnglePlane(cameraRotationInc, 1, 0, 0));
            }
            if (input.keysDown[InputKey_RotateYX]) {
                camera.orientation = rotor3fMulRotor3f(camera.orientation, createRotor3fAnglePlane(cameraRotationInc, -1, 0, 0));
            }
            if (input.keysDown[InputKey_RotateXZ]) {
                camera.orientation = rotor3fMulRotor3f(camera.orientation, createRotor3fAnglePlane(cameraRotationInc, 0, 1, 0));
            }
            if (input.keysDown[InputKey_RotateZX]) {
                camera.orientation = rotor3fMulRotor3f(camera.orientation, createRotor3fAnglePlane(cameraRotationInc, 0, -1, 0));
            }
            if (input.keysDown[InputKey_RotateYZ]) {
                camera.orientation = rotor3fMulRotor3f(camera.orientation, createRotor3fAnglePlane(cameraRotationInc, 0, 0, 1));
            }
            if (input.keysDown[InputKey_RotateZY]) {
                camera.orientation = rotor3fMulRotor3f(camera.orientation, createRotor3fAnglePlane(cameraRotationInc, 0, 0, -1));
            }

            Rotor3f cubeRotation1 = createRotor3fAnglePlane(0, 1, 1, 1);
            cube1.orientation = rotor3fMulRotor3f(cube1.orientation, cubeRotation1);
            Rotor3f cubeRotation2 = rotor3fReverse(cubeRotation1);
            cube2.orientation = rotor3fMulRotor3f(cube2.orientation, cubeRotation2);
        }

        rendererClearBuffers(&renderer);

        bool showDebugTriangles = false;
        if (showDebugTriangles) {
            drawDebugTriangles(&renderer, windowWidth, windowHeight);
        } else {
            rendererPushMesh(&renderer, cube1, camera);
            rendererPushMesh(&renderer, cube2, camera);

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

        // NOTE(khvorov) Frame timing
        {
            f32 msFromStart = getMsFromMarker(clock, frameStart);
            f32 msToSleep = msPerFrameTarget - msFromStart;
            if (msToSleep >= 1) {
                DWORD msToSleepFloor = (DWORD)msToSleep;
                Sleep(msToSleepFloor);
            }
            for (; msFromStart < msPerFrameTarget; msFromStart = getMsFromMarker(clock, frameStart)) {}
            frameStart = getClockMarker();
        }
    }

    return 0;
}

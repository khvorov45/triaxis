#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>

//
// SECTION Renderer
//

// clang-format off
#define function static
#define assert(cond) do { if (cond) {} else { __debugbreak(); }} while (0)
#define unused(x) (x) = (x)
#define min(x, y) (((x) < (y)) ? (x) : (y))
#define max(x, y) (((x) > (y)) ? (x) : (y))
// clang-format on

typedef uint8_t  u8;
typedef int32_t  i32;
typedef uint32_t u32;
typedef intptr_t isize;
typedef float    f32;

function i32
ceilf32toi32(f32 val) {
    i32 valint = (i32)val;
    i32 result = valint + (isize)(val > (f32)valint);
    return result;
}

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
} Arena;

function void*
arenaFreePtr(Arena* arena) {
    assert(arena->used <= arena->size);
    void* result = (u8*)arena->base + arena->used;
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

function Arena
createArenaFromArena(Arena* parent, isize size) {
    Arena arena = {.base = arenaFreePtr(parent), .size = size};
    arenaChangeUsed(parent, size);
    return arena;
}

typedef struct Texture {
    u32*  pixels;
    isize width;
    isize height;
} Texture;

typedef struct Renderer {
    Arena   arena;
    Arena   imageArena;
    Texture image;
} Renderer;

function Renderer
createRenderer(void* base, isize size) {
    Renderer renderer = {.arena.base = base, .arena.size = size};
    renderer.imageArena = createArenaFromArena(&renderer.arena, size / 2);
    arenaAlign(&renderer.imageArena, alignof(u32));
    renderer.image.pixels = arenaFreePtr(&renderer.imageArena);
    return renderer;
}

function void
setImageSize(Renderer* renderer, isize width, isize height) {
    renderer->imageArena.used = 0;
    arenaChangeUsed(&renderer->imageArena, width * height * sizeof(renderer->image.pixels[0]));
    renderer->image.width = width;
    renderer->image.height = height;
}

typedef struct V2f {
    f32 x, y;
} V2f;

function V2f
v2fminus(V2f v1, V2f v2) {
    V2f result = {v2.x - v1.x, v2.y - v1.y};
    return result;
}

function f32
edgeCrossMag(V2f v1, V2f v2, V2f pt) {
    V2f v1v2 = v2fminus(v2, v1);
    V2f v1pt = v2fminus(pt, v1);
    f32 result = v1v2.x * v1pt.y - v1v2.y * v1pt.x;
    return result;
}

typedef struct Rect2i {
    isize x, y, width, height;
} Rect2i;

typedef struct Rect2f {
    f32 x, y, width, height;
} Rect2f;

typedef struct Color255 {
    u8 r, g, b, a;
} Color255;

function u32
color255tou32(Color255 color) {
    u32 coloru32 = (color.a << 24) | (color.r << 16) | (color.g << 8) | (color.b << 0);
    return coloru32;
}

function void
fillRect2i(Renderer* renderer, Rect2i rect, Color255 color) {
    assert(rect.x > 0 && rect.x < renderer->image.width);
    assert(rect.y > 0 && rect.y < renderer->image.height);
    isize right = rect.x + rect.width;
    isize bottom = rect.y + rect.height;
    assert(right > 0 && right < renderer->image.width);
    assert(bottom > 0 && bottom < renderer->image.height);

    u32 coloru32 = color255tou32(color);
    for (isize row = rect.y; row < bottom; row++) {
        for (isize column = rect.x; column < right; column++) {
            isize index = row * renderer->image.width + column;
            renderer->image.pixels[index] = coloru32;
        }
    }
}

function bool
v2fInRect2i(V2f point, Rect2i rectf) {
    Rect2f rect = {(f32)rectf.x, (f32)rectf.y, (f32)rectf.width, (f32)rectf.height};
    bool   hor = point.x >= rect.x && point.x < rect.x + rect.width;
    bool   ver = point.y >= rect.y && point.y < rect.y + rect.height;
    bool   result = hor && ver;
    return result;
}

function bool
v2fInBounds(Renderer* renderer, V2f point) {
    bool result = v2fInRect2i(point, (Rect2i) {0, 0, renderer->image.width, renderer->image.height});
    return result;
}

typedef struct TriangleF {
    V2f v1, v2, v3;
} TriangleF;

function void
fillTriangleF(Renderer* renderer, TriangleF triangle, Color255 color) {
    assert(v2fInBounds(renderer, triangle.v1));
    assert(v2fInBounds(renderer, triangle.v2));
    assert(v2fInBounds(renderer, triangle.v3));

    f32 xmin = min(triangle.v1.x, min(triangle.v2.x, triangle.v3.x));
    f32 ymin = min(triangle.v1.y, min(triangle.v2.y, triangle.v3.y));
    f32 xmax = max(triangle.v1.x, max(triangle.v2.x, triangle.v3.x));
    f32 ymax = max(triangle.v1.y, max(triangle.v2.y, triangle.v3.y));

    i32 xstart = ceilf32toi32(xmin);
    i32 xend = ceilf32toi32(xmax);
    i32 ystart = ceilf32toi32(ymin);
    i32 yend = ceilf32toi32(ymax);

    u32 coloru32 = color255tou32(color);
    for (i32 ycoord = ystart; ycoord < yend; ycoord++) {
        for (i32 xcoord = xstart; xcoord < xend; xcoord++) {

            V2f point = {(float)xcoord, (float)ycoord};
            f32 cross1 = edgeCrossMag(triangle.v1, triangle.v2, point);
            f32 cross2 = edgeCrossMag(triangle.v2, triangle.v3, point);
            f32 cross3 = edgeCrossMag(triangle.v3, triangle.v1, point);

            if (cross1 > 0 && cross2 > 0 && cross3 > 0) {
                renderer->image.pixels[ycoord * renderer->image.width + xcoord] = coloru32;
            }
        }
    }
}

//
// SECTION Platform
//

#undef function
#define WIN32_LEAN_AND_MEAN 1
#define VC_EXTRALEAN 1
#include <Windows.h>

typedef struct Win32Renderer {
    Renderer renderer;
    HDC      hdc;
    isize    windowWidth;
    isize    windowHeight;
} Win32Renderer;

LRESULT CALLBACK
windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    LRESULT result = 0;
    switch (uMsg) {
        case WM_DESTROY: PostQuitMessage(0); break;
        case WM_ERASEBKGND: result = TRUE; break;  // NOTE(khvorov) Do nothinga

        case WM_PAINT: {
            Win32Renderer* win32Rend = (Win32Renderer*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (win32Rend) {
                BITMAPINFO bmi = {
                    .bmiHeader.biSize = sizeof(BITMAPINFOHEADER),
                    .bmiHeader.biWidth = win32Rend->renderer.image.width,
                    .bmiHeader.biHeight = -win32Rend->renderer.image.height,  // NOTE(khvorov) Top-down
                    .bmiHeader.biPlanes = 1,
                    .bmiHeader.biBitCount = 32,
                    .bmiHeader.biCompression = BI_RGB,
                };
                StretchDIBits(win32Rend->hdc, 0, 0, win32Rend->windowWidth, win32Rend->windowHeight, 0, 0, win32Rend->renderer.image.width, win32Rend->renderer.image.height, win32Rend->renderer.image.pixels, &bmi, DIB_RGB_COLORS, SRCCOPY);
            }
        } break;

        default: result = DefWindowProc(hwnd, uMsg, wParam, lParam); break;
    }
    return result;
}

int WINAPI
WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    unused(hPrevInstance);
    unused(lpCmdLine);
    unused(nCmdShow);

    isize memSize = 1 * 1024 * 1024 * 1024;
    void* memBase = VirtualAlloc(0, memSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(memBase);
    Win32Renderer win32Rend = {.renderer = createRenderer(memBase, memSize), .windowWidth = 1000, .windowHeight = 1000};
    setImageSize(&win32Rend.renderer, win32Rend.windowWidth, win32Rend.windowHeight);
    fillRect2i(&win32Rend.renderer, (Rect2i) {100, 100, 100, 100}, (Color255) {255, 0, 0, 255});
    fillTriangleF(&win32Rend.renderer, (TriangleF) {{10, 2}, {200, 280}, {30, 210}}, (Color255) {0, 0, 255, 255});

    WNDCLASSEXW windowClass = {
        .cbSize = sizeof(WNDCLASSEXW),
        .lpfnWndProc = windowProc,
        .hInstance = hInstance,
        .lpszClassName = L"Triaxis",
        .hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH),
        .hCursor = LoadCursorA(NULL, IDC_ARROW),
    };
    assert(RegisterClassExW(&windowClass) != 0);

    HWND window = CreateWindowExW(0, windowClass.lpszClassName, L"Triaxis", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, win32Rend.windowWidth, win32Rend.windowHeight, NULL, NULL, hInstance, NULL);
    assert(window);
    win32Rend.hdc = GetDC(window);

    // TODO(khvorov) Find out if there is a better way
    ShowWindow(window, SW_SHOWMINIMIZED);
    ShowWindow(window, SW_SHOWNORMAL);

    SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)&win32Rend);

    for (MSG msg = {}; GetMessageW(&msg, NULL, 0, 0);) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}

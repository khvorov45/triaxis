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

// function i32
// ceilf32toi32(f32 val) {
//     i32 valint = (i32)val;
//     i32 result = valint + (isize)(val > (f32)valint);
//     return result;
// }

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

function f32
lerp(f32 start, f32 end, f32 by) {
    f32 result = start + (end - start) * by;
    return result;
}

typedef struct V2f {
    f32 x, y;
} V2f;

function V2f
v2fminus(V2f v1, V2f v2) {
    V2f result = {v1.x - v2.x, v1.y - v2.y};
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
    Color255 result = {.r = (u8)(color.r * 255.0f), .g = (u8)(color.g * 255.0f), .b = (u8)(color.b * 255.0f), .a = (u8)(color.a * 255.0f)};
    return result;
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

// function bool
// v2fInRect2f(V2f point, Rect2f rect) {
//     bool hor = point.x >= rect.x && point.x < rect.x + rect.width;
//     bool ver = point.y >= rect.y && point.y < rect.y + rect.height;
//     bool result = hor && ver;
//     return result;
// }

// function bool
// v2fInRect2i(V2f point, Rect2i recti) {
//     Rect2f rect = {(f32)recti.x, (f32)recti.y, (f32)recti.width, (f32)recti.height};
//     bool   result = v2fInRect2f(point, rect);
//     return result;
// }

// function bool
// v2fInBounds(Renderer* renderer, V2f point) {
//     Rect2f rect = (Rect2f) {-0.5f, -0.5f, ((f32)renderer->image.width), ((f32)renderer->image.height)};
//     bool   result = v2fInRect2f(point, rect);
//     return result;
// }

typedef struct TriangleF {
    V2f v1, v2, v3;
} TriangleF;

function f32
edgeCrossMag(V2f v1, V2f v2, V2f pt) {
    V2f v1v2 = v2fminus(v2, v1);
    V2f v1pt = v2fminus(pt, v1);
    f32 result = v1v2.x * v1pt.y - v1v2.y * v1pt.x;
    return result;
}

function bool
isTopLeft(V2f v1, V2f v2) {
    V2f  v1v2 = v2fminus(v2, v1);
    bool isFlatTop = v1v2.y == 0 && v1v2.x > 0;
    bool isLeft = v1v2.y < 0;
    bool result = isFlatTop || isLeft;
    return result;
}

function void
fillTriangleF(Renderer* renderer, TriangleF triangle, Color255 color) {
    f32 xmin = min(triangle.v1.x, min(triangle.v2.x, triangle.v3.x));
    f32 ymin = min(triangle.v1.y, min(triangle.v2.y, triangle.v3.y));
    f32 xmax = max(triangle.v1.x, max(triangle.v2.x, triangle.v3.x));
    f32 ymax = max(triangle.v1.y, max(triangle.v2.y, triangle.v3.y));

    bool allowZero1 = isTopLeft(triangle.v1, triangle.v2);
    bool allowZero2 = isTopLeft(triangle.v2, triangle.v3);
    bool allowZero3 = isTopLeft(triangle.v3, triangle.v1);

    Color01 color01 = color255to01(color);
    for (i32 ycoord = (i32)ymin; ycoord <= (i32)ymax; ycoord++) {
        for (i32 xcoord = (i32)xmin; xcoord <= (i32)xmax; xcoord++) {
            V2f point = {(f32)xcoord, (f32)ycoord};
            f32 cross1 = edgeCrossMag(triangle.v1, triangle.v2, point);
            f32 cross2 = edgeCrossMag(triangle.v2, triangle.v3, point);
            f32 cross3 = edgeCrossMag(triangle.v3, triangle.v1, point);

            bool pass1 = cross1 > 0 || (cross1 == 0 && allowZero1);
            bool pass2 = cross2 > 0 || (cross2 == 0 && allowZero2);
            bool pass3 = cross3 > 0 || (cross3 == 0 && allowZero3);

            if (pass1 && pass2 && pass3) {
                i32 index = ycoord * renderer->image.width + xcoord;
                if (index < renderer->image.width * renderer->image.height) {
                    u32      existingColoru32 = renderer->image.pixels[index];
                    Color255 existingColor255 = coloru32to255(existingColoru32);
                    Color01  existingColor01 = color255to01(existingColor255);
                    Color01  blended01 = {
                        .r = lerp(existingColor01.r, color01.r, color01.a),
                        .g = lerp(existingColor01.g, color01.g, color01.a),
                        .b = lerp(existingColor01.b, color01.b, color01.a),
                        .a = 1,
                    };
                    Color255 blended255 = color01to255(blended01);
                    u32      blendedu32 = color255tou32(blended255);
                    renderer->image.pixels[index] = blendedu32;
                }
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
    Win32Renderer win32Rend = {.renderer = createRenderer(memBase, memSize), .windowWidth = 1600, .windowHeight = 800};
    setImageSize(&win32Rend.renderer, 16, 8);
    if (false)
        fillRect2i(&win32Rend.renderer, (Rect2i) {100, 100, 100, 100}, (Color255) {255, 0, 0, 255});

    // NOTE(khvorov) Triangles taken from https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-rasterizer-stage-rules
    fillTriangleF(&win32Rend.renderer, (TriangleF) {{0.5, 0.5}, {5.5, 1.5}, {1.5, 3.5}}, (Color255) {0, 0, 255, 255});
    fillTriangleF(&win32Rend.renderer, (TriangleF) {{4, 0}, {4, 0}, {4, 0}}, (Color255) {0, 0, 255, 255});
    fillTriangleF(&win32Rend.renderer, (TriangleF) {{5.75, -0.25}, {5.75, 0.75}, {4.75, 0.75}}, (Color255) {0, 0, 255, 255});
    fillTriangleF(&win32Rend.renderer, (TriangleF) {{7, 0}, {7, 1}, {6, 1}}, (Color255) {0, 0, 255, 255});
    fillTriangleF(&win32Rend.renderer, (TriangleF) {{7.25, 2}, {9.25, 0.25}, {11.25, 2}}, (Color255) {0, 0, 255, 128});
    fillTriangleF(&win32Rend.renderer, (TriangleF) {{7.25, 2}, {11.25, 2}, {9, 4.75}}, (Color255) {0, 255, 255, 128});
    fillTriangleF(&win32Rend.renderer, (TriangleF) {{13, 1}, {14.5, -0.5}, {14, 2}}, (Color255) {0, 0, 255, 255});
    fillTriangleF(&win32Rend.renderer, (TriangleF) {{13, 1}, {14, 2}, {14, 4}}, (Color255) {0, 0, 255, 255});
    fillTriangleF(&win32Rend.renderer, (TriangleF) {{0.5, 5.5}, {6.5, 3.5}, {4.5, 5.5}}, (Color255) {0, 0, 255, 128});
    fillTriangleF(&win32Rend.renderer, (TriangleF) {{4.5, 5.5}, {6.5, 3.5}, {7.5, 6.5}}, (Color255) {0, 255, 0, 128});
    fillTriangleF(&win32Rend.renderer, (TriangleF) {{6.5, 3.5}, {9, 5}, {7.5, 6.5}}, (Color255) {255, 0, 0, 128});
    fillTriangleF(&win32Rend.renderer, (TriangleF) {{9, 7}, {10, 7}, {9, 9}}, (Color255) {0, 0, 255, 255});
    fillTriangleF(&win32Rend.renderer, (TriangleF) {{11, 4}, {12, 5}, {11, 6}}, (Color255) {0, 0, 255, 255});
    fillTriangleF(&win32Rend.renderer, (TriangleF) {{13, 5}, {15, 5}, {13, 7}}, (Color255) {0, 0, 255, 128});
    fillTriangleF(&win32Rend.renderer, (TriangleF) {{15, 5}, {15, 7}, {13, 7}}, (Color255) {0, 255, 0, 128});

    WNDCLASSEXW windowClass = {
        .cbSize = sizeof(WNDCLASSEXW),
        .lpfnWndProc = windowProc,
        .hInstance = hInstance,
        .lpszClassName = L"Triaxis",
        .hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH),
        .hCursor = LoadCursorA(NULL, IDC_ARROW),
    };
    assert(RegisterClassExW(&windowClass) != 0);

    // TODO(khvorov) Adjust window size such that it's the client area that's the specified width/height
    HWND window = CreateWindowExW(0, windowClass.lpszClassName, L"Triaxis", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, win32Rend.windowWidth, win32Rend.windowHeight, NULL, NULL, hInstance, NULL);
    assert(window);
    win32Rend.hdc = GetDC(window);

    // NOTE(khvorov) Adjust window size such that it's the client area that's the specified size, not the whole window with decorations
    {
        RECT rect = {};
        GetClientRect(window, &rect);
        isize width = rect.right - rect.left;
        isize height = rect.bottom - rect.top;
        isize dwidth = win32Rend.windowWidth - width;
        isize dheight = win32Rend.windowHeight - height;
        SetWindowPos(window, 0, rect.left, rect.top, win32Rend.windowWidth + dwidth, win32Rend.windowHeight + dheight, 0);
    }

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

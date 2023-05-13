#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>

//
// SECTION Renderer
//

// clang-format off
#define function static
#define assert(cond) do { if (cond) {} else { __debugbreak(); }} while (0)
#define arrayCount(x) (sizeof(x) / sizeof(x[0]))
#define unused(x) (x) = (x)
#define min(x, y) (((x) < (y)) ? (x) : (y))
#define max(x, y) (((x) > (y)) ? (x) : (y))
// clang-format on

typedef uint8_t  u8;
typedef int32_t  i32;
typedef uint32_t u32;
typedef intptr_t isize;
typedef float    f32;

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

function void
clearImage(Renderer* renderer) {
    for (isize ind = 0; ind < renderer->image.width * renderer->image.height; ind++) {
        renderer->image.pixels[ind] = 0;
    }
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
v2fsub(V2f v1, V2f v2) {
    V2f result = {v1.x - v2.x, v1.y - v2.y};
    return result;
}

function V2f
v2fhadamard(V2f v1, V2f v2) {
    V2f result = {v1.x * v2.x, v1.y * v2.y};
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

typedef struct TriangleF {
    V2f     v1, v2, v3;
    Color01 c1, c2, c3;
} TriangleF;

function TriangleF
triangleSameColor(V2f v1, V2f v2, V2f v3, Color255 color) {
    Color01   c01 = color255to01(color);
    TriangleF result = {v1, v2, v3, c01, c01, c01};
    return result;
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

function void
fillTriangleF(Renderer* renderer, TriangleF triangle) {
    f32 xmin = min(triangle.v1.x, min(triangle.v2.x, triangle.v3.x));
    f32 ymin = min(triangle.v1.y, min(triangle.v2.y, triangle.v3.y));
    f32 xmax = max(triangle.v1.x, max(triangle.v2.x, triangle.v3.x));
    f32 ymax = max(triangle.v1.y, max(triangle.v2.y, triangle.v3.y));

    bool allowZero1 = isTopLeft(triangle.v1, triangle.v2);
    bool allowZero2 = isTopLeft(triangle.v2, triangle.v3);
    bool allowZero3 = isTopLeft(triangle.v3, triangle.v1);

    f32 area = edgeCrossMag(triangle.v1, triangle.v2, triangle.v3);

    f32 dcross1x = triangle.v1.y - triangle.v2.y;
    f32 dcross2x = triangle.v2.y - triangle.v3.y;
    f32 dcross3x = triangle.v3.y - triangle.v1.y;

    f32 dcross1y = triangle.v2.x - triangle.v1.x;
    f32 dcross2y = triangle.v3.x - triangle.v2.x;
    f32 dcross3y = triangle.v1.x - triangle.v3.x;

    i32 ystart = (i32)ymin;
    i32 xstart = (i32)xmin;

    // TODO(khvorov) Are constant increments actually faster than just computing the edge cross every time?
    V2f topleft = {(f32)(xstart), (f32)(ystart)};
    f32 cross1topleft = edgeCrossMag(triangle.v1, triangle.v2, topleft);
    f32 cross2topleft = edgeCrossMag(triangle.v2, triangle.v3, topleft);
    f32 cross3topleft = edgeCrossMag(triangle.v3, triangle.v1, topleft);

    for (i32 ycoord = ystart; ycoord <= (i32)ymax; ycoord++) {
        if (ycoord < renderer->image.height) {
            f32 yinc = (f32)(ycoord - ystart);
            f32 cross1row = cross1topleft + yinc * dcross1y;
            f32 cross2row = cross2topleft + yinc * dcross2y;
            f32 cross3row = cross3topleft + yinc * dcross3y;

            for (i32 xcoord = xstart; xcoord <= (i32)xmax; xcoord++) {
                if (xcoord < renderer->image.width) {
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

                        Color01 color01 = color01add(color01add(color01mul(triangle.c1, cross2scaled), color01mul(triangle.c2, cross3scaled)), color01mul(triangle.c3, cross1scaled));

                        i32 index = ycoord * renderer->image.width + xcoord;
                        assert(index < renderer->image.width * renderer->image.height);

                        u32      existingColoru32 = renderer->image.pixels[index];
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
                        renderer->image.pixels[index] = blendedu32;
                    }
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

    isize memSize = 1 * 1024 * 1024 * 1024;
    void* memBase = VirtualAlloc(0, memSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(memBase);
    Win32Renderer win32Rend = {.renderer = createRenderer(memBase, memSize), .windowWidth = 1600, .windowHeight = 800};

    // NOTE(khvorov) Run some tests
    {
        setImageSize(&win32Rend.renderer, 16, 8);

        // NOTE(khvorov) Triangles taken from https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-rasterizer-stage-rules
        fillTriangleF(&win32Rend.renderer, triangleSameColor((V2f) {0.5, 0.5}, (V2f) {5.5, 1.5}, (V2f) {1.5, 3.5}, (Color255) {0, 0, 255, 255}));
        fillTriangleF(&win32Rend.renderer, triangleSameColor((V2f) {4, 0}, (V2f) {4, 0}, (V2f) {4, 0}, (Color255) {0, 0, 255, 255}));
        fillTriangleF(&win32Rend.renderer, triangleSameColor((V2f) {5.75, -0.25}, (V2f) {5.75, 0.75}, (V2f) {4.75, 0.75}, (Color255) {0, 0, 255, 255}));
        fillTriangleF(&win32Rend.renderer, triangleSameColor((V2f) {7, 0}, (V2f) {7, 1}, (V2f) {6, 1}, (Color255) {0, 0, 255, 255}));
        fillTriangleF(&win32Rend.renderer, triangleSameColor((V2f) {7.25, 2}, (V2f) {9.25, 0.25}, (V2f) {11.25, 2}, (Color255) {0, 0, 255, 128}));
        fillTriangleF(&win32Rend.renderer, triangleSameColor((V2f) {7.25, 2}, (V2f) {11.25, 2}, (V2f) {9, 4.75}, (Color255) {0, 255, 255, 128}));
        fillTriangleF(&win32Rend.renderer, triangleSameColor((V2f) {13, 1}, (V2f) {14.5, -0.5}, (V2f) {14, 2}, (Color255) {0, 0, 255, 255}));
        fillTriangleF(&win32Rend.renderer, triangleSameColor((V2f) {13, 1}, (V2f) {14, 2}, (V2f) {14, 4}, (Color255) {0, 0, 255, 255}));
        fillTriangleF(&win32Rend.renderer, triangleSameColor((V2f) {0.5, 5.5}, (V2f) {6.5, 3.5}, (V2f) {4.5, 5.5}, (Color255) {0, 0, 255, 128}));
        fillTriangleF(&win32Rend.renderer, triangleSameColor((V2f) {4.5, 5.5}, (V2f) {6.5, 3.5}, (V2f) {7.5, 6.5}, (Color255) {0, 255, 0, 128}));
        fillTriangleF(&win32Rend.renderer, triangleSameColor((V2f) {6.5, 3.5}, (V2f) {9, 5}, (V2f) {7.5, 6.5}, (Color255) {255, 0, 0, 128}));
        fillTriangleF(&win32Rend.renderer, triangleSameColor((V2f) {9, 7}, (V2f) {10, 7}, (V2f) {9, 9}, (Color255) {0, 0, 255, 255}));
        fillTriangleF(&win32Rend.renderer, triangleSameColor((V2f) {11, 4}, (V2f) {12, 5}, (V2f) {11, 6}, (Color255) {0, 0, 255, 255}));
        fillTriangleF(&win32Rend.renderer, triangleSameColor((V2f) {13, 5}, (V2f) {15, 5}, (V2f) {13, 7}, (Color255) {0, 0, 255, 128}));
        fillTriangleF(&win32Rend.renderer, triangleSameColor((V2f) {15, 5}, (V2f) {15, 7}, (V2f) {13, 7}, (Color255) {0, 255, 0, 128}));

        // clang-format off
        u32 expected[] = {
            0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xff0000ff, 0x00000000, 
            0x00000000, 0xff0000ff, 0xff0000ff, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xff000080, 0xff000080, 0x00000000, 0x00000000, 0xff0000ff, 0xff0000ff, 0x00000000, 
            0x00000000, 0xff0000ff, 0xff0000ff, 0xff0000ff, 0xff0000ff, 0x00000000, 0x00000000, 0x00000000, 0xff008080, 0xff008080, 0xff008080, 0xff008080, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 
            0x00000000, 0x00000000, 0xff0000ff, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xff008080, 0xff008080, 0xff008080, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 
            0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xff000080, 0xff008000, 0xff800000, 0x00000000, 0xff008080, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 
            0x00000000, 0x00000000, 0xff000080, 0xff000080, 0xff000080, 0xff008000, 0xff008000, 0xff800000, 0xff800000, 0x00000000, 0x00000000, 0xff0000ff, 0x00000000, 0xff000080, 0xff000080, 0x00000000, 
            0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xff008000, 0xff008000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xff000080, 0xff008000, 0x00000000, 
            0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xff0000ff, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 
        };
        // clang-format on

        assert(arrayCount(expected) == win32Rend.renderer.image.height * win32Rend.renderer.image.width);
        for (isize row = 0; row < win32Rend.renderer.image.height; row++) {
            for (isize column = 0; column < win32Rend.renderer.image.width; column++) {
                isize index = row * win32Rend.renderer.image.width + column;
                u32   expectedPx = expected[index];
                u32   actualPx = win32Rend.renderer.image.pixels[index];
                assert(expectedPx == actualPx);
            }
        }
    }

    clearImage(&win32Rend.renderer);

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

    // TODO(khvorov) Hack is debug only
    ShowWindow(window, SW_SHOWMINIMIZED);
    ShowWindow(window, SW_SHOWNORMAL);

    SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)&win32Rend);

    setImageSize(&win32Rend.renderer, win32Rend.windowWidth / 50, win32Rend.windowHeight / 50);
    V2f vertices[] = {
        v2fhadamard((V2f) {0.04, 0.01}, (V2f) {(f32)win32Rend.renderer.image.width, (f32)win32Rend.renderer.image.height}),
        v2fhadamard((V2f) {0.5, 0.5}, (V2f) {(f32)win32Rend.renderer.image.width, (f32)win32Rend.renderer.image.height}),
        v2fhadamard((V2f) {0.01, 0.3}, (V2f) {(f32)win32Rend.renderer.image.width, (f32)win32Rend.renderer.image.height}),
        v2fhadamard((V2f) {0.4, 0.03}, (V2f) {(f32)win32Rend.renderer.image.width, (f32)win32Rend.renderer.image.height}),
    };

    Color01 colors[] = {
        {.a = 1, .r = 1},
        {.a = 1, .g = 1},
        {.a = 1, .b = 1},
        {.a = 1, .r = 1, .g = 1},
    };

    assert(arrayCount(vertices) == arrayCount(colors));

    for (MSG msg = {}; GetMessageW(&msg, 0, 0, 0);) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);

        clearImage(&win32Rend.renderer);
        fillTriangleF(&win32Rend.renderer, (TriangleF) {vertices[0], vertices[1], vertices[2], colors[0], colors[1], colors[2]});
        fillTriangleF(&win32Rend.renderer, (TriangleF) {vertices[0], vertices[3], vertices[1], colors[0], colors[3], colors[1]});

        // NOTE(khvorov) Present the bitmap
        {
            BITMAPINFO bmi = {
                .bmiHeader.biSize = sizeof(BITMAPINFOHEADER),
                .bmiHeader.biWidth = win32Rend.renderer.image.width,
                .bmiHeader.biHeight = -win32Rend.renderer.image.height,  // NOTE(khvorov) Top-down
                .bmiHeader.biPlanes = 1,
                .bmiHeader.biBitCount = 32,
                .bmiHeader.biCompression = BI_RGB,
            };
            StretchDIBits(win32Rend.hdc, 0, 0, win32Rend.windowWidth, win32Rend.windowHeight, 0, 0, win32Rend.renderer.image.width, win32Rend.renderer.image.height, win32Rend.renderer.image.pixels, &bmi, DIB_RGB_COLORS, SRCCOPY);
        }

        // NOTE(khvorov) Move the shape to the cursor
        {
            V2f   refVertex = vertices[1];
            POINT cursor = {};
            GetCursorPos(&cursor);
            ScreenToClient(window, &cursor);
            f32 cursorImageX = ((f32)cursor.x / (f32)win32Rend.windowWidth * (f32)win32Rend.renderer.image.width);
            f32 dref = cursorImageX - refVertex.x;
            for (isize ind = 0; ind < (isize)arrayCount(vertices); ind++) {
                vertices[ind].x += dref;
            }
        }
    }

    return 0;
}

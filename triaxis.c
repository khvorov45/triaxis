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
// clang-format on

typedef uint8_t  u8;
typedef int32_t  i32;
typedef uint32_t u32;
typedef intptr_t isize;

bool
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

typedef struct Rect2i {
    isize x, y, width, height;
} Rect2i;

typedef struct Color255 {
    u8 r, g, b, a;
} Color255;

function void
fillRect(Renderer* renderer, Rect2i rect, Color255 color) {
    assert(rect.x > 0 && rect.x < renderer->image.width);
    assert(rect.y > 0 && rect.y < renderer->image.height);
    isize right = rect.x + rect.width;
    isize bottom = rect.y + rect.height;
    assert(right > 0 && right < renderer->image.width);
    assert(bottom > 0 && bottom < renderer->image.height);

    u32 coloru32 =  (color.a << 24) | (color.r << 16) | (color.g << 8) | (color.b << 0);
    for (isize row = rect.y; row < bottom; row++) {
        for (isize column = rect.x; column < right; column++) {
            isize index = row * renderer->image.width + column;
            renderer->image.pixels[index] = coloru32;
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
    fillRect(&win32Rend.renderer, (Rect2i) {100, 100, 100, 100}, (Color255) {255, 0, 0, 255});

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

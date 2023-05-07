#include <Windows.h>

// clang-format off
#define assert(cond) do { if (cond) {} else { __debugbreak(); }} while (0)
// clang-format on

LRESULT CALLBACK
WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    LRESULT result = 0;
    switch (uMsg) {
        case WM_DESTROY: PostQuitMessage(0); break;
        default: result = DefWindowProc(hwnd, uMsg, wParam, lParam); break;
    }
    return result;
}

int WINAPI
WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASSEXW windowClass = {
        .cbSize = sizeof(WNDCLASSEXW),
        .lpfnWndProc = WindowProc,
        .hInstance = hInstance,
        .lpszClassName = L"MyWindowClass",
        .hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH),
    };

    RegisterClassExW(&windowClass);

    HWND window = CreateWindowExW(0, windowClass.lpszClassName, L"Window title", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1000, 1000, NULL, NULL, hInstance, NULL);
    assert(window);

    // TODO(khvorov) Find out if there is a better way
    ShowWindow(window, SW_SHOWMINIMIZED);
    ShowWindow(window, SW_SHOWNORMAL);

    for (MSG msg = {}; GetMessage(&msg, NULL, 0, 0);) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}

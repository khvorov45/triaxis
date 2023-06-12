#include "triaxis.c"

#undef function
#define WIN32_LEAN_AND_MEAN 1
#define VC_EXTRALEAN 1
#include <Windows.h>

#pragma comment(lib, "gdi32")
#pragma comment(lib, "user32")
#pragma comment(lib, "Winmm")

//
// SECTION D3D11
//

#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>

#undef max
#undef min
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsometimes-uninitialized"
#pragma clang diagnostic ignored "-Wunused-private-field"
#include "TracyD3D11.hpp"
#pragma clang diagnostic pop

#define asserthr(x) assert(SUCCEEDED(x))

#pragma comment(lib, "d3d11")
#pragma comment(lib, "dxguid")
#pragma comment(lib, "d3dcompiler")

// TODO(khvorov) Handle resizing

typedef struct D3D11Vertex {
    f32 pos[2];
    f32 uv[2];
} D3D11Vertex;

typedef struct D3D11Renderer {
    ID3D11DeviceContext*    context;
    ID3D11RenderTargetView* rtView;
    IDXGISwapChain1*        swapChain;
    ID3D11Texture2D*        texture;
    TracyD3D11Ctx           tracyD3D11Context;
} D3D11Renderer;

static D3D11Renderer
initD3D11(HWND window, isize windowWidth, isize windowHeight, Arena* scratch) {
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
        device->QueryInterface(IID_ID3D11InfoQueue, (void**)&info);
        info->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        info->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, TRUE);
        info->Release();
    }
#endif

    IDXGISwapChain1* swapChain = 0;
    {
        IDXGIDevice* dxgiDevice = 0;
        asserthr(device->QueryInterface(IID_IDXGIDevice, (void**)&dxgiDevice));

        IDXGIAdapter* dxgiAdapter = 0;
        asserthr(dxgiDevice->GetAdapter(&dxgiAdapter));

        IDXGIFactory2* factory = 0;
        asserthr(dxgiAdapter->GetParent(IID_IDXGIFactory2, (void**)&factory));

        DXGI_SWAP_CHAIN_DESC1 desc = {
            .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
            .SampleDesc = {.Count = 1, .Quality = 0},
            .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
            .BufferCount = 2,
            .Scaling = DXGI_SCALING_NONE,
            .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
        };

        asserthr(factory->CreateSwapChainForHwnd(device, window, &desc, NULL, NULL, &swapChain));

        factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);

        factory->Release();
        dxgiAdapter->Release();
        dxgiDevice->Release();
    }

    {
        D3D11Vertex data[] = {
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

        ID3D11Buffer* vbuffer = 0;
        device->CreateBuffer(&desc, &initial, &vbuffer);

        UINT offset = 0;
        UINT stride = sizeof(D3D11Vertex);
        context->IASetVertexBuffers(0, 1, &vbuffer, &stride, &offset);
    }

    {
        D3D11_INPUT_ELEMENT_DESC desc[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(D3D11Vertex, pos), D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(D3D11Vertex, uv), D3D11_INPUT_PER_VERTEX_DATA, 0},
        };

        UINT flags = D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR | D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS;
#ifdef TRIAXIS_debuginfo
        flags |= D3DCOMPILE_DEBUG;
#endif
#ifndef TRIAXIS_optimise
        flags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

        ID3DBlob* verror = 0;
        ID3DBlob* vblob = 0;

        ID3DBlob* perror = 0;
        ID3DBlob* pblob = 0;
        {
            TempMemory temp = beginTempMemory(scratch);

            Str hlsl = {};
            {
                void*         buf = arenaFreePtr(scratch);
                HANDLE        handle = CreateFileW(L"code/shader.hlsl", GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
                DWORD         bytesRead = 0;
                LARGE_INTEGER filesize = {};
                GetFileSizeEx(handle, &filesize);
                ReadFile(handle, buf, filesize.QuadPart, &bytesRead, 0);
                assert(bytesRead == filesize.QuadPart);
                CloseHandle(handle);
                arenaChangeUsed(scratch, bytesRead);
                hlsl = (Str) {(char*)buf, (isize)bytesRead};
            }

            HRESULT vresult = D3DCompile(hlsl.ptr, hlsl.len, NULL, NULL, NULL, "vs", "vs_5_0", flags, 0, &vblob, &verror);
            if (FAILED(vresult)) {
                char* msg = (char*)verror->GetBufferPointer();
                OutputDebugStringA(msg);
                assert(!"failed to compile");
            }

            HRESULT presult = D3DCompile(hlsl.ptr, hlsl.len, NULL, NULL, NULL, "ps", "ps_5_0", flags, 0, &pblob, &perror);
            if (FAILED(presult)) {
                char* msg = (char*)perror->GetBufferPointer();
                OutputDebugStringA(msg);
                assert(!"failed to compile");
            }
            endTempMemory(temp);
        }

        ID3D11VertexShader* vshader = 0;
        device->CreateVertexShader(vblob->GetBufferPointer(), vblob->GetBufferSize(), NULL, &vshader);
        context->VSSetShader(vshader, NULL, 0);

        ID3D11PixelShader* pshader = 0;
        device->CreatePixelShader(pblob->GetBufferPointer(), pblob->GetBufferSize(), NULL, &pshader);
        context->PSSetShader(pshader, NULL, 0);

        ID3D11InputLayout* layout = 0;
        device->CreateInputLayout(desc, ARRAYSIZE(desc), vblob->GetBufferPointer(), vblob->GetBufferSize(), &layout);
        context->IASetInputLayout(layout);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

        pblob->Release();
        vblob->Release();
    }

    ID3D11Texture2D* texture = 0;
    {
        D3D11_TEXTURE2D_DESC desc = {
            .Width = (UINT)windowWidth,
            .Height = (UINT)windowHeight,
            .MipLevels = 1,
            .ArraySize = 1,
            .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
            .SampleDesc = {.Count = 1, .Quality = 0},
            .Usage = D3D11_USAGE_DYNAMIC,
            .BindFlags = D3D11_BIND_SHADER_RESOURCE,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        };

        device->CreateTexture2D(&desc, 0, &texture);

        ID3D11ShaderResourceView* view = 0;
        device->CreateShaderResourceView((ID3D11Resource*)texture, NULL, &view);
        context->PSSetShaderResources(0, 1, &view);
    }

    {
        D3D11_SAMPLER_DESC desc = {
            .Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
            .AddressU = D3D11_TEXTURE_ADDRESS_WRAP,
            .AddressV = D3D11_TEXTURE_ADDRESS_WRAP,
            .AddressW = D3D11_TEXTURE_ADDRESS_WRAP,
        };
        ID3D11SamplerState* sampler = 0;
        device->CreateSamplerState(&desc, &sampler);
        context->PSSetSamplers(0, 1, &sampler);
    }

    {
        D3D11_RASTERIZER_DESC desc = {
            .FillMode = D3D11_FILL_SOLID,
            .CullMode = D3D11_CULL_NONE,
        };
        ID3D11RasterizerState* rasterizerState = 0;
        device->CreateRasterizerState(&desc, &rasterizerState);
        context->RSSetState(rasterizerState);
    }

    ID3D11RenderTargetView* rtView = 0;
    {
        asserthr(swapChain->ResizeBuffers(0, windowWidth, windowHeight, DXGI_FORMAT_UNKNOWN, 0));

        ID3D11Texture2D* backbuffer = 0;
        swapChain->GetBuffer(0, IID_ID3D11Texture2D, (void**)&backbuffer);
        device->CreateRenderTargetView((ID3D11Resource*)backbuffer, NULL, &rtView);
        assert(rtView);
        backbuffer->Release();
    }

    {
        D3D11_VIEWPORT viewport = {
            .TopLeftX = 0,
            .TopLeftY = 0,
            .Width = (FLOAT)windowWidth,
            .Height = (FLOAT)windowHeight,
            .MinDepth = 0,
            .MaxDepth = 1,
        };
        context->RSSetViewports(1, &viewport);
    }

    TracyD3D11Ctx tracyD3D11Context = TracyD3D11Context(device, context);

    D3D11Renderer rend = {
        .context = context,
        .rtView = rtView,
        .swapChain = swapChain,
        .texture = texture,
        .tracyD3D11Context = tracyD3D11Context,
    };
    return rend;
}

static void
d3d11present(D3D11Renderer rend, Texture tex) {
    {
        FLOAT color[] = {0.0f, 0.0, 0.0f, 1.f};
        rend.context->ClearRenderTargetView(rend.rtView, color);
    }

    {
        D3D11_MAPPED_SUBRESOURCE mappedTexture = {};
        rend.context->Map((ID3D11Resource*)rend.texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedTexture);
        u32* pixels = (u32*)mappedTexture.pData;
        TracyCZoneN(tracyCtx, "present copymem", true);
        copymem(pixels, tex.ptr, tex.width * tex.height * sizeof(u32));
        TracyCZoneEnd(tracyCtx);
        rend.context->Unmap((ID3D11Resource*)rend.texture, 0);
    }

    {
        TracyD3D11Zone(rend.tracyD3D11Context, "draw quad");
        rend.context->OMSetRenderTargets(1, &rend.rtView, 0);
        rend.context->Draw(4, 0);
    }

    HRESULT presentResult = rend.swapChain->Present(1, 0);
    TracyD3D11Collect(rend.tracyD3D11Context);
    asserthr(presentResult);
    if (presentResult == DXGI_STATUS_OCCLUDED) {
        Sleep(10);
    }
}

//
// SECTION Timing
//

#include <timeapi.h>

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

//
// SECTION Main
//

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

    HWND window = 0;
    {
        TempMemory temp = beginTempMemory(&state->scratch);
        LPWSTR     exename = (LPWSTR)arenaFreePtr(&state->scratch);
        GetModuleFileNameW(hInstance, exename, arenaFreeSize(&state->scratch) / sizeof(u16));

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

    swRendererSetImageSize(&state->renderer, state->windowWidth, state->windowHeight);
    D3D11Renderer d3d11rend = initD3D11(window, state->windowWidth, state->windowHeight, &state->scratch);

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
                        InputKey key = InputKey_Up;
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

        d3d11present(d3d11rend, (Texture) {state->renderer.image.ptr, state->renderer.image.width, state->renderer.image.height});

        TracyCZoneEnd(tracyFrameCtx);
    }

    TracyD3D11Destroy(d3d11rend.tracyD3D11Context);
    return 0;
}

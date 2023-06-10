#include "triaxis.c"

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

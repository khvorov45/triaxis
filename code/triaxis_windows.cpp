#include "triaxis.c"

#undef function
#undef BYTE
#define WIN32_LEAN_AND_MEAN 1
#define VC_EXTRALEAN 1
#include <Windows.h>
#include <hidusage.h>

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

typedef struct D3D11Common {
    ID3D11DeviceContext*    context;
    ID3D11Device*           device;
    ID3D11RenderTargetView* rtView;
    IDXGISwapChain1*        swapChain;
    TracyD3D11Ctx           tracyD3D11Context;
    Str                     hlsl;
} D3D11Common;

static D3D11Common
initD3D11Common(HWND window, isize viewportWidth, isize viewportHeight, Arena* perm) {
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

    ID3D11RenderTargetView* rtView = 0;
    {
        asserthr(swapChain->ResizeBuffers(0, viewportWidth, viewportHeight, DXGI_FORMAT_UNKNOWN, 0));

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
            .Width = (FLOAT)viewportWidth,
            .Height = (FLOAT)viewportHeight,
            .MinDepth = 0,
            .MaxDepth = 1,
        };
        context->RSSetViewports(1, &viewport);
    }

    TracyD3D11Ctx tracyD3D11Context = TracyD3D11Context(device, context);

    Str hlsl = {};
    {
        void*         buf = arenaFreePtr(perm);
        HANDLE        handle = CreateFileW(L"code/shader.hlsl", GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
        DWORD         bytesRead = 0;
        LARGE_INTEGER filesize = {};
        GetFileSizeEx(handle, &filesize);
        ReadFile(handle, buf, filesize.QuadPart, &bytesRead, 0);
        assert(bytesRead == filesize.QuadPart);
        CloseHandle(handle);
        arenaChangeUsed(perm, bytesRead);
        hlsl = (Str) {(char*)buf, (isize)bytesRead};
    }

    D3D11Common common = {
        .context = context,
        .device = device,
        .rtView = rtView,
        .swapChain = swapChain,
        .tracyD3D11Context = tracyD3D11Context,
        .hlsl = hlsl,
    };
    return common;
}

static ID3DBlob*
compileShader(Str hlsl, const char* name, const char* kind) {
    UINT flags = D3DCOMPILE_PACK_MATRIX_ROW_MAJOR | D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS;
#ifdef TRIAXIS_debuginfo
    flags |= D3DCOMPILE_DEBUG;
#endif
#ifndef TRIAXIS_optimise
    flags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ID3DBlob* error = 0;
    ID3DBlob* blob = 0;

    HRESULT result = D3DCompile(hlsl.ptr, hlsl.len, NULL, NULL, NULL, name, kind, flags, 0, &blob, &error);
    if (FAILED(result)) {
        char* msg = (char*)error->GetBufferPointer();
        OutputDebugStringA(msg);
        assert(!"failed to compile");
    }

    return blob;
}

typedef struct D3D11BlitterVertex {
    f32 pos[2];
    f32 uv[2];
} D3D11BlitterVertex;

typedef struct D3D11Blitter {
    D3D11Common*              common;
    ID3D11Buffer*             vbuffer;
    ID3D11VertexShader*       vshader;
    ID3D11PixelShader*        pshader;
    ID3D11InputLayout*        layout;
    ID3D11Texture2D*          texture;
    ID3D11ShaderResourceView* textureView;
    ID3D11SamplerState*       sampler;
    ID3D11RasterizerState*    rasterizerState;
} D3D11Blitter;

static D3D11Blitter
initD3D11Blitter(D3D11Common* common, isize textureWidth, isize textureHeight) {
    D3D11Blitter blitter = {.common = common};

    {
        D3D11BlitterVertex data[] = {
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
        common->device->CreateBuffer(&desc, &initial, &blitter.vbuffer);
    }

    {
        D3D11_INPUT_ELEMENT_DESC desc[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(D3D11BlitterVertex, pos), D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(D3D11BlitterVertex, uv), D3D11_INPUT_PER_VERTEX_DATA, 0},
        };

        {
            ID3DBlob* blob = compileShader(common->hlsl, "blittervs", "vs_5_0");
            common->device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), NULL, &blitter.vshader);
            common->device->CreateInputLayout(desc, ARRAYSIZE(desc), blob->GetBufferPointer(), blob->GetBufferSize(), &blitter.layout);
            blob->Release();
        }

        {
            ID3DBlob* pblob = compileShader(common->hlsl, "blitterps", "ps_5_0");
            common->device->CreatePixelShader(pblob->GetBufferPointer(), pblob->GetBufferSize(), NULL, &blitter.pshader);
            pblob->Release();
        }
    }

    {
        D3D11_TEXTURE2D_DESC desc = {
            .Width = (UINT)textureWidth,
            .Height = (UINT)textureHeight,
            .MipLevels = 1,
            .ArraySize = 1,
            .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
            .SampleDesc = {.Count = 1, .Quality = 0},
            .Usage = D3D11_USAGE_DYNAMIC,
            .BindFlags = D3D11_BIND_SHADER_RESOURCE,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        };

        common->device->CreateTexture2D(&desc, 0, &blitter.texture);
        common->device->CreateShaderResourceView((ID3D11Resource*)blitter.texture, NULL, &blitter.textureView);
    }

    {
        D3D11_SAMPLER_DESC desc = {
            .Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
            .AddressU = D3D11_TEXTURE_ADDRESS_WRAP,
            .AddressV = D3D11_TEXTURE_ADDRESS_WRAP,
            .AddressW = D3D11_TEXTURE_ADDRESS_WRAP,
        };
        common->device->CreateSamplerState(&desc, &blitter.sampler);
    }

    {
        D3D11_RASTERIZER_DESC desc = {
            .FillMode = D3D11_FILL_SOLID,
            .CullMode = D3D11_CULL_NONE,
        };
        common->device->CreateRasterizerState(&desc, &blitter.rasterizerState);
    }

    return blitter;
}

static void
d3d11blit(D3D11Blitter blitter, Texture tex) {
    {
        UINT offset = 0;
        UINT stride = sizeof(D3D11BlitterVertex);
        blitter.common->context->IASetVertexBuffers(0, 1, &blitter.vbuffer, &stride, &offset);
    }

    blitter.common->context->VSSetShader(blitter.vshader, NULL, 0);
    blitter.common->context->IASetInputLayout(blitter.layout);
    blitter.common->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    blitter.common->context->PSSetShader(blitter.pshader, NULL, 0);
    blitter.common->context->PSSetShaderResources(0, 1, &blitter.textureView);
    blitter.common->context->PSSetSamplers(0, 1, &blitter.sampler);
    blitter.common->context->RSSetState(blitter.rasterizerState);

    {
        FLOAT color[] = {0.0f, 0.0, 0.0f, 1.f};
        blitter.common->context->ClearRenderTargetView(blitter.common->rtView, color);
    }

    {
        D3D11_MAPPED_SUBRESOURCE mappedTexture = {};
        blitter.common->context->Map((ID3D11Resource*)blitter.texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedTexture);
        u32* pixels = (u32*)mappedTexture.pData;
        TracyCZoneN(tracyCtx, "present copymem", true);
        copymem(pixels, tex.ptr, tex.width * tex.height * sizeof(u32));
        TracyCZoneEnd(tracyCtx);
        blitter.common->context->Unmap((ID3D11Resource*)blitter.texture, 0);
    }

    {
        TracyD3D11Zone(blitter.common->tracyD3D11Context, "draw quad");
        blitter.common->context->OMSetRenderTargets(1, &blitter.common->rtView, 0);
        blitter.common->context->Draw(4, 0);
    }

    HRESULT presentResult = blitter.common->swapChain->Present(1, 0);
    TracyD3D11Collect(blitter.common->tracyD3D11Context);
    asserthr(presentResult);
    if (presentResult == DXGI_STATUS_OCCLUDED) {
        Sleep(10);
    }
}

typedef struct D3D11ConstCamera {
    Rotor3f orientation;
    V3f     pos;
    f32     tanHalfFovX;
    f32     tanHalfFovY;
    u8      pad[12];
} D3D11ConstCamera;

typedef struct D3D11ConstMesh {
    Rotor3f orientation;
    V3f     pos;
    u8      pad1[4];
} D3D11ConstMesh;

typedef struct D3D11TriFilledVertex {
    V2f     pos;
    Color01 color;
} D3D11TriFilledVertex;

typedef struct D3D11Renderer {
    D3D11Common*           common;
    ID3D11Buffer*          vbuffer;
    ID3D11Buffer*          ibuffer;
    ID3D11Buffer*          colorBuffer;
    ID3D11VertexShader*    vshader;
    ID3D11InputLayout*     layout;
    ID3D11PixelShader*     pshader;
    ID3D11RasterizerState* rasterizerState;

    ID3D11Buffer* constCamera;
    ID3D11Buffer* constMesh;

    struct {
        isize               vertexCap;
        ID3D11Buffer*       vertices;
        ID3D11VertexShader* vshader;
        ID3D11InputLayout*  layout;
        ID3D11PixelShader*  pshader;
    } triFilled;
} D3D11Renderer;

static D3D11Renderer
initD3D11Renderer(D3D11Common* common, State* state) {
    D3D11Renderer renderer = {.common = common};

    {
        D3D11_BUFFER_DESC desc = {
            .ByteWidth = (UINT)(state->meshStorage.vertices.len * sizeof(*state->meshStorage.vertices.ptr)),
            .Usage = D3D11_USAGE_IMMUTABLE,
            .BindFlags = D3D11_BIND_VERTEX_BUFFER,
        };
        D3D11_SUBRESOURCE_DATA initial = {.pSysMem = state->meshStorage.vertices.ptr};
        common->device->CreateBuffer(&desc, &initial, &renderer.vbuffer);
    }

    {
        D3D11_BUFFER_DESC desc = {
            .ByteWidth = (UINT)(state->meshStorage.indices.len * sizeof(*state->meshStorage.indices.ptr)),
            .Usage = D3D11_USAGE_IMMUTABLE,
            .BindFlags = D3D11_BIND_INDEX_BUFFER,
        };
        D3D11_SUBRESOURCE_DATA initial = {.pSysMem = state->meshStorage.indices.ptr};
        common->device->CreateBuffer(&desc, &initial, &renderer.ibuffer);
    }

    {
        D3D11_BUFFER_DESC desc = {
            .ByteWidth = (UINT)(state->meshStorage.colors.len * sizeof(*state->meshStorage.colors.ptr)),
            .Usage = D3D11_USAGE_IMMUTABLE,
            .BindFlags = D3D11_BIND_VERTEX_BUFFER,
        };
        D3D11_SUBRESOURCE_DATA initial = {.pSysMem = state->meshStorage.colors.ptr};
        common->device->CreateBuffer(&desc, &initial, &renderer.colorBuffer);
    }

    {
        renderer.triFilled.vertexCap = 1024;
        D3D11_BUFFER_DESC desc = {
            .ByteWidth = (UINT)(renderer.triFilled.vertexCap * sizeof(D3D11TriFilledVertex)),
            .Usage = D3D11_USAGE_DYNAMIC,
            .BindFlags = D3D11_BIND_VERTEX_BUFFER,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        };
        common->device->CreateBuffer(&desc, 0, &renderer.triFilled.vertices);
    }

    {
        D3D11_INPUT_ELEMENT_DESC desc[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };

        {
            ID3DBlob* blob = compileShader(common->hlsl, "renderervs", "vs_5_0");
            common->device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), NULL, &renderer.vshader);
            common->device->CreateInputLayout(desc, ARRAYSIZE(desc), blob->GetBufferPointer(), blob->GetBufferSize(), &renderer.layout);
            blob->Release();
        }

        {
            ID3DBlob* pblob = compileShader(common->hlsl, "rendererps", "ps_5_0");
            common->device->CreatePixelShader(pblob->GetBufferPointer(), pblob->GetBufferSize(), NULL, &renderer.pshader);
            pblob->Release();
        }
    }

    // TODO(khvorov) Compress?
    {
        D3D11_INPUT_ELEMENT_DESC desc[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(D3D11TriFilledVertex, pos), D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(D3D11TriFilledVertex, color), D3D11_INPUT_PER_VERTEX_DATA, 0},
        };

        {
            ID3DBlob* blob = compileShader(common->hlsl, "trifilledvs", "vs_5_0");
            common->device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), NULL, &renderer.triFilled.vshader);
            common->device->CreateInputLayout(desc, ARRAYSIZE(desc), blob->GetBufferPointer(), blob->GetBufferSize(), &renderer.triFilled.layout);
            blob->Release();
        }

        {
            ID3DBlob* pblob = compileShader(common->hlsl, "trifilledps", "ps_5_0");
            common->device->CreatePixelShader(pblob->GetBufferPointer(), pblob->GetBufferSize(), NULL, &renderer.triFilled.pshader);
            pblob->Release();
        }
    }

    {
        D3D11_RASTERIZER_DESC desc = {
            .FillMode = D3D11_FILL_SOLID,
            .CullMode = D3D11_CULL_BACK,
        };
        common->device->CreateRasterizerState(&desc, &renderer.rasterizerState);
    }

    {
        D3D11_BUFFER_DESC desc = {
            .ByteWidth = sizeof(D3D11ConstCamera),
            .Usage = D3D11_USAGE_DYNAMIC,
            .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        };
        common->device->CreateBuffer(&desc, 0, &renderer.constCamera);

        desc.ByteWidth = sizeof(D3D11ConstMesh);
        common->device->CreateBuffer(&desc, 0, &renderer.constMesh);
    }

    return renderer;
}

static void
d3d11render(D3D11Renderer renderer, State* state) {
    {
        UINT          offsets[] = {0, 0};
        UINT          strides[] = {sizeof(*state->meshStorage.vertices.ptr), sizeof(*state->meshStorage.colors.ptr)};
        ID3D11Buffer* buffers[] = {renderer.vbuffer, renderer.colorBuffer};
        renderer.common->context->IASetVertexBuffers(0, arrayCount(buffers), buffers, strides, offsets);
    }

    renderer.common->context->IASetIndexBuffer(renderer.ibuffer, DXGI_FORMAT_R32_UINT, 0);
    renderer.common->context->VSSetShader(renderer.vshader, NULL, 0);
    renderer.common->context->IASetInputLayout(renderer.layout);
    renderer.common->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    renderer.common->context->PSSetShader(renderer.pshader, NULL, 0);
    renderer.common->context->RSSetState(renderer.rasterizerState);

    {
        ID3D11Buffer* buffers[] = {renderer.constCamera, renderer.constMesh};
        renderer.common->context->VSSetConstantBuffers(0, arrayCount(buffers), buffers);
    }

    {
        D3D11_MAPPED_SUBRESOURCE mappedCamera = {};
        renderer.common->context->Map((ID3D11Resource*)renderer.constCamera, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedCamera);
        D3D11ConstCamera* constCamera = (D3D11ConstCamera*)mappedCamera.pData;
        constCamera->orientation = state->camera.currentOrientation;
        constCamera->pos = state->camera.pos;
        constCamera->tanHalfFovX = state->camera.tanHalfFov.x;
        constCamera->tanHalfFovY = state->camera.tanHalfFov.y;
        renderer.common->context->Unmap((ID3D11Resource*)renderer.constCamera, 0);
    }

    {
        FLOAT color[] = {0.1f, 0.1, 0.1f, 1.f};
        renderer.common->context->ClearRenderTargetView(renderer.common->rtView, color);
    }

    renderer.common->context->OMSetRenderTargets(1, &renderer.common->rtView, 0);

    for (isize meshIndex = 0; meshIndex < state->meshes.len; meshIndex++) {
        Mesh mesh = state->meshes.ptr[meshIndex];

        D3D11_MAPPED_SUBRESOURCE mappedMesh = {};
        renderer.common->context->Map((ID3D11Resource*)renderer.constMesh, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedMesh);
        D3D11ConstMesh* constMesh = (D3D11ConstMesh*)mappedMesh.pData;
        constMesh->orientation = mesh.orientation;
        constMesh->pos = mesh.pos;
        renderer.common->context->Unmap((ID3D11Resource*)renderer.constMesh, 0);

        i32 baseVertex = mesh.vertices.ptr - state->meshStorage.vertices.ptr;
        i32 baseIndex = (i32*)mesh.indices.ptr - (i32*)state->meshStorage.indices.ptr;
        renderer.common->context->DrawIndexed(mesh.indices.len * 3, baseIndex, baseVertex);
    }

    // TODO(khvorov) Debug overlay
    {
        D3D11_MAPPED_SUBRESOURCE mappedTriFilledVertices = {};
        renderer.common->context->Map((ID3D11Resource*)renderer.triFilled.vertices, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedTriFilledVertices);

        struct {
            D3D11TriFilledVertex* ptr;
            isize                 len;
            isize                 cap;
        } triFilled = {(D3D11TriFilledVertex*)mappedTriFilledVertices.pData, 0, renderer.triFilled.vertexCap};

        const struct nk_command* cmd = 0;
        nk_foreach(cmd, &state->ui) {
            switch (cmd->type) {
                case NK_COMMAND_NOP: break;
                case NK_COMMAND_SCISSOR: break;
                case NK_COMMAND_LINE: break;
                case NK_COMMAND_CURVE: break;
                case NK_COMMAND_RECT: break;

                case NK_COMMAND_RECT_FILLED: {
                    struct nk_command_rect_filled* rect = (struct nk_command_rect_filled*)cmd;

                    i32 x0 = rect->x;
                    i32 x1 = rect->x + rect->w;
                    i32 y0 = rect->y;
                    i32 y1 = rect->y + rect->h;

                    V2f topleft = {(f32)x0, (f32)y0};
                    V2f topright = {(f32)x1, (f32)y0};
                    V2f bottomleft = {(f32)x0, (f32)y1};
                    V2f bottomright = {(f32)x1, (f32)y1};

                    Color01 color = color255to01(nkcolorTo255(rect->color));

                    arrpush(triFilled, ((D3D11TriFilledVertex) {topleft, color}));
                    arrpush(triFilled, ((D3D11TriFilledVertex) {topright, color}));
                    arrpush(triFilled, ((D3D11TriFilledVertex) {bottomleft, color}));

                    arrpush(triFilled, ((D3D11TriFilledVertex) {topright, color}));
                    arrpush(triFilled, ((D3D11TriFilledVertex) {bottomright, color}));
                    arrpush(triFilled, ((D3D11TriFilledVertex) {bottomleft, color}));
                } break;

                case NK_COMMAND_RECT_MULTI_COLOR: break;
                case NK_COMMAND_CIRCLE: break;
                case NK_COMMAND_CIRCLE_FILLED: break;
                case NK_COMMAND_ARC: break;
                case NK_COMMAND_ARC_FILLED: break;
                case NK_COMMAND_TRIANGLE: break;
                case NK_COMMAND_TRIANGLE_FILLED: break;
                case NK_COMMAND_POLYGON: break;
                case NK_COMMAND_POLYGON_FILLED: break;
                case NK_COMMAND_POLYLINE: break;
                case NK_COMMAND_TEXT: break;
                case NK_COMMAND_IMAGE: break;
                case NK_COMMAND_CUSTOM: break;
            }
        }

        renderer.common->context->Unmap((ID3D11Resource*)renderer.triFilled.vertices, 0);

        {
            UINT          offsets[] = {0};
            UINT          strides[] = {sizeof(D3D11TriFilledVertex)};
            ID3D11Buffer* buffers[] = {renderer.triFilled.vertices};
            renderer.common->context->IASetVertexBuffers(0, arrayCount(buffers), buffers, strides, offsets);
        }

        renderer.common->context->VSSetShader(renderer.triFilled.vshader, NULL, 0);
        renderer.common->context->IASetInputLayout(renderer.triFilled.layout);
        renderer.common->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        renderer.common->context->PSSetShader(renderer.triFilled.pshader, NULL, 0);

        renderer.common->context->Draw(triFilled.len, 0);
    }

    HRESULT presentResult = renderer.common->swapChain->Present(1, 0);
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
        case WM_ERASEBKGND: result = TRUE; break;
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

    {
        RAWINPUTDEVICE mouse = {
            .usUsagePage = HID_USAGE_PAGE_GENERIC,
            .usUsage = HID_USAGE_GENERIC_MOUSE,
            .hwndTarget = window,
        };

        assert(RegisterRawInputDevices(&mouse, 1, sizeof(RAWINPUTDEVICE)) == TRUE);
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

    swRendererSetImageSize(&state->swRenderer, state->windowWidth, state->windowHeight);
    D3D11Common   d3d11common = initD3D11Common(window, state->windowWidth, state->windowHeight, &state->perm);
    D3D11Blitter  d3d11blitter = initD3D11Blitter(&d3d11common, state->windowWidth, state->windowHeight);
    D3D11Renderer d3d11renderer = initD3D11Renderer(&d3d11common, state);

    bool prevWindowIsForeground = false;
    bool curWindowIsForeground = false;

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
                            case VK_SPACE: key = InputKey_Up; break;
                            case VK_CONTROL: key = InputKey_Down; break;
                            case VK_SHIFT: key = InputKey_MoveFaster; break;
                            case VK_UP: key = InputKey_RotateZY; break;
                            case VK_DOWN: key = InputKey_RotateYZ; break;
                            case VK_LEFT: key = InputKey_RotateXZ; break;
                            case VK_RIGHT: key = InputKey_RotateZX; break;
                            case 'Q': key = InputKey_RotateXY; break;
                            case 'E': key = InputKey_RotateYX; break;
                            case VK_TAB: key = InputKey_ToggleDebugTriangles; break;
                            case 'T': key = InputKey_ToggleSW; break;
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

                    case WM_INPUT: {
                        RAWINPUT input = {};
                        UINT     size = sizeof(RAWINPUT);
                        assert(GetRawInputData((HRAWINPUT)msg.lParam, RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER)));
                        if (input.header.dwType == RIM_TYPEMOUSE && input.data.mouse.usFlags == MOUSE_MOVE_RELATIVE) {
                            state->input.mouse.dx = input.data.mouse.lLastX;
                            state->input.mouse.dy = input.data.mouse.lLastY;
                        }
                    } break;

                    case WM_MOUSEMOVE: break;

                    default: {
                        TranslateMessage(&msg);
                        DispatchMessage(&msg);
                    } break;
                }
            }

            {
                POINT point = {};
                GetCursorPos(&point);
                state->input.mouse.x = point.x;
                state->input.mouse.y = point.y;
            }

            {
                prevWindowIsForeground = curWindowIsForeground;
                curWindowIsForeground = GetForegroundWindow() == window;

                if (prevWindowIsForeground != curWindowIsForeground) {
                    // NOTE(khvorov) Clip cursor
                    if (curWindowIsForeground) {
                        RECT rect = {};
                        GetClientRect(window, &rect);
                        POINT topleft = {rect.left, rect.top};
                        ClientToScreen(window, &topleft);
                        POINT bottomright = {rect.right, rect.bottom};
                        ClientToScreen(window, &bottomright);
                        RECT screen = {.left = topleft.x, .right = bottomright.x, .top = topleft.y, .bottom = bottomright.y};
                        ClipCursor(&screen);
                    }

                    ShowCursor(FALSE);
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

        if (state->useSW) {
            {
                TracyCZoneN(tracyCtx, "render", true);
                swRender(state);
                TracyCZoneEnd(tracyCtx);
            }

            d3d11blit(d3d11blitter, (Texture) {state->swRenderer.image.ptr, state->swRenderer.image.width, state->swRenderer.image.height});
        } else {
            d3d11render(d3d11renderer, state);
        }

        nk_clear(&state->ui);
        TracyCZoneEnd(tracyFrameCtx);
    }

    TracyD3D11Destroy(d3d11common.tracyD3D11Context);

    // TODO(khvorov) Does this prevent bluescreens?
    {
        d3d11renderer.vbuffer->Release();
        d3d11renderer.ibuffer->Release();
        d3d11renderer.colorBuffer->Release();
        d3d11renderer.vshader->Release();
        d3d11renderer.layout->Release();
        d3d11renderer.pshader->Release();
        d3d11renderer.rasterizerState->Release();
        d3d11renderer.constCamera->Release();
        d3d11renderer.constMesh->Release();

        d3d11renderer.triFilled.vertices->Release();
        d3d11renderer.triFilled.vshader->Release();
        d3d11renderer.triFilled.layout->Release();
        d3d11renderer.triFilled.pshader->Release();

        d3d11blitter.vbuffer->Release();
        d3d11blitter.vshader->Release();
        d3d11blitter.pshader->Release();
        d3d11blitter.layout->Release();
        d3d11blitter.texture->Release();
        d3d11blitter.textureView->Release();
        d3d11blitter.sampler->Release();
        d3d11blitter.rasterizerState->Release();

        d3d11common.swapChain->Release();
        d3d11common.rtView->Release();
        d3d11common.device->Release();
        d3d11common.context->Release();
    }

    return 0;
}

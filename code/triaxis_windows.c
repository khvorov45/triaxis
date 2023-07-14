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

#define COBJMACROS
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dxgidebug.h>

#pragma comment(lib, "d3d11")
#pragma comment(lib, "dxguid")

// TODO(khvorov) Precompile in release?
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler")

#define asserthr(x) assert(SUCCEEDED(x))

static Str
readEntireFile(Arena* arena, LPCWSTR path) {
    void*         buf = arenaFreePtr(arena);
    HANDLE        handle = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    DWORD         bytesRead = 0;
    LARGE_INTEGER filesize = {};
    assert(filesize.QuadPart <= arenaFreeSize(arena));
    GetFileSizeEx(handle, &filesize);
    ReadFile(handle, buf, filesize.QuadPart, &bytesRead, 0);
    assert(bytesRead == filesize.QuadPart);
    CloseHandle(handle);
    arenaChangeUsed(arena, bytesRead);
    Str result = (Str) {(char*)buf, (isize)bytesRead};
    return result;
}

// TODO(khvorov) Handle resizing

typedef struct D3D11Common {
    ID3D11DeviceContext*    context;
    ID3D11Device*           device;
    ID3D11RenderTargetView* rtView;
    IDXGISwapChain1*        swapChain;
} D3D11Common;

static D3D11Common
initD3D11Common(HWND window, isize viewportWidth, isize viewportHeight) {
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
        ID3D11Device_QueryInterface(device, &IID_ID3D11InfoQueue, (void**)&info);
        ID3D11InfoQueue_SetBreakOnSeverity(info, D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
        ID3D11InfoQueue_SetBreakOnSeverity(info, D3D11_MESSAGE_SEVERITY_ERROR, TRUE);
        ID3D11InfoQueue_Release(info);
    }

    {
        HMODULE dxgiDebug = LoadLibraryA("dxgidebug.dll");
        if (dxgiDebug) {
            HRESULT(WINAPI * dxgiGetDebugInterface)
            (REFIID riid, void** ppDebug);

            *(FARPROC*)&dxgiGetDebugInterface = GetProcAddress(dxgiDebug, "DXGIGetDebugInterface");

            IDXGIInfoQueue* info = 0;
            asserthr(dxgiGetDebugInterface(&IID_IDXGIInfoQueue, (void**)&info));
            IDXGIInfoQueue_SetBreakOnSeverity(info, DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            IDXGIInfoQueue_SetBreakOnSeverity(info, DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, TRUE);
            IDXGIInfoQueue_Release(info);
        }
    }
#endif

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

        IDXGIFactory2_MakeWindowAssociation(factory, window, DXGI_MWA_NO_ALT_ENTER);

        IDXGIFactory2_Release(factory);
        IDXGIAdapter_Release(dxgiAdapter);
        IDXGIDevice_Release(dxgiDevice);
    }

    ID3D11RenderTargetView* rtView = 0;
    {
        asserthr(IDXGISwapChain1_ResizeBuffers(swapChain, 0, viewportWidth, viewportHeight, DXGI_FORMAT_UNKNOWN, 0));

        ID3D11Texture2D* backbuffer = 0;
        IDXGISwapChain1_GetBuffer(swapChain, 0, &IID_ID3D11Texture2D, (void**)&backbuffer);
        ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource*)backbuffer, NULL, &rtView);
        assert(rtView);
        ID3D11Texture2D_Release(backbuffer);
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
        ID3D11DeviceContext_RSSetViewports(context, 1, &viewport);
    }

    D3D11Common common = {
        .context = context,
        .device = device,
        .rtView = rtView,
        .swapChain = swapChain,
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
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    ID3DBlob* error = 0;
    ID3DBlob* blob = 0;

    HRESULT result = D3DCompile(hlsl.ptr, hlsl.len, NULL, NULL, NULL, name, kind, flags, 0, &blob, &error);
    if (FAILED(result)) {
        char* msg = (char*)ID3D10Blob_GetBufferPointer(error);
        OutputDebugStringA(msg);
        assert(!"failed to compile");
    }

    return blob;
}

typedef struct VSPS {
    ID3D11VertexShader* vs;
    ID3D11InputLayout*  layout;
    ID3D11PixelShader*  ps;
} VSPS;

static VSPS
compileVSPS(D3D11_INPUT_ELEMENT_DESC* desc, isize descCount, LPCWSTR path, ID3D11Device* device, Arena* scratch) {
    TempMemory temp = beginTempMemory(scratch);

    VSPS result = {};

    Str hlsl = readEntireFile(scratch, path);

    {
        ID3DBlob* blob = compileShader(hlsl, "vs", "vs_5_0");
        ID3D11Device_CreateVertexShader(device, ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob), NULL, &result.vs);
        ID3D11Device_CreateInputLayout(device, desc, descCount, ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob), &result.layout);
        ID3D10Blob_Release(blob);
    }

    {
        ID3DBlob* pblob = compileShader(hlsl, "ps", "ps_5_0");
        ID3D11Device_CreatePixelShader(device, ID3D10Blob_GetBufferPointer(pblob), ID3D10Blob_GetBufferSize(pblob), NULL, &result.ps);
        ID3D10Blob_Release(pblob);
    }

    endTempMemory(temp);
    return result;
}

typedef struct D3D11BlitterVertex {
    f32 pos[2];
    f32 uv[2];
} D3D11BlitterVertex;

typedef struct D3D11Blitter {
    D3D11Common*           common;
    ID3D11Buffer*          vbuffer;
    VSPS                   vsps;
    ID3D11SamplerState*    sampler;
    ID3D11RasterizerState* rasterizerState;

    struct {
        ID3D11Texture2D*          tex2d;
        ID3D11ShaderResourceView* view;
        isize                     width, height;
    } tex;
} D3D11Blitter;

static D3D11Blitter
initD3D11Blitter(D3D11Common* common, isize textureWidth, isize textureHeight, Arena* scratch) {
    TempMemory temp = beginTempMemory(scratch);

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
        ID3D11Device_CreateBuffer(common->device, &desc, &initial, &blitter.vbuffer);
    }

    {
        D3D11_INPUT_ELEMENT_DESC desc[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(D3D11BlitterVertex, pos), D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(D3D11BlitterVertex, uv), D3D11_INPUT_PER_VERTEX_DATA, 0},
        };

        blitter.vsps = compileVSPS(desc, arrayCount(desc), L"code/blitter.hlsl", common->device, scratch);
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

        ID3D11Device_CreateTexture2D(common->device, &desc, 0, &blitter.tex.tex2d);
        ID3D11Device_CreateShaderResourceView(common->device, (ID3D11Resource*)blitter.tex.tex2d, NULL, &blitter.tex.view);

        blitter.tex.width = textureWidth;
        blitter.tex.height = textureHeight;
    }

    {
        D3D11_SAMPLER_DESC desc = {
            .Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
            .AddressU = D3D11_TEXTURE_ADDRESS_WRAP,
            .AddressV = D3D11_TEXTURE_ADDRESS_WRAP,
            .AddressW = D3D11_TEXTURE_ADDRESS_WRAP,
        };
        ID3D11Device_CreateSamplerState(common->device, &desc, &blitter.sampler);
    }

    {
        D3D11_RASTERIZER_DESC desc = {
            .FillMode = D3D11_FILL_SOLID,
            .CullMode = D3D11_CULL_NONE,
        };
        ID3D11Device_CreateRasterizerState(common->device, &desc, &blitter.rasterizerState);
    }

    endTempMemory(temp);
    return blitter;
}

static void
d3d11blit(D3D11Blitter blitter, Texture tex) {
    {
        UINT offset = 0;
        UINT stride = sizeof(D3D11BlitterVertex);
        ID3D11DeviceContext_IASetVertexBuffers(blitter.common->context, 0, 1, &blitter.vbuffer, &stride, &offset);
    }

    ID3D11DeviceContext_VSSetShader(blitter.common->context, blitter.vsps.vs, NULL, 0);
    ID3D11DeviceContext_IASetInputLayout(blitter.common->context, blitter.vsps.layout);
    ID3D11DeviceContext_IASetPrimitiveTopology(blitter.common->context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D11DeviceContext_PSSetShader(blitter.common->context, blitter.vsps.ps, NULL, 0);
    ID3D11DeviceContext_PSSetShaderResources(blitter.common->context, 0, 1, &blitter.tex.view);
    ID3D11DeviceContext_PSSetSamplers(blitter.common->context, 0, 1, &blitter.sampler);
    ID3D11DeviceContext_RSSetState(blitter.common->context, blitter.rasterizerState);

    {
        FLOAT color[] = {0.0f, 0.0, 0.0f, 1.f};
        ID3D11DeviceContext_ClearRenderTargetView(blitter.common->context, blitter.common->rtView, color);
    }

    {
        assert(tex.width <= blitter.tex.width);
        assert(tex.height <= blitter.tex.height);

        D3D11_MAPPED_SUBRESOURCE mappedTexture = {};
        ID3D11DeviceContext_Map(blitter.common->context, (ID3D11Resource*)blitter.tex.tex2d, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedTexture);
        u32* pixels = (u32*)mappedTexture.pData;
        timedSectionStart("present copymem");
        for (isize row = 0; row < tex.height; row++) {
            u32* srcRow = tex.ptr + row * tex.pitch;
            u32* destRow = pixels + row * blitter.tex.width;
            memcpy_(destRow, srcRow, tex.width * sizeof(u32));
        }
        timedSectionEnd();
        ID3D11DeviceContext_Unmap(blitter.common->context, (ID3D11Resource*)blitter.tex.tex2d, 0);
    }

    {
        ID3D11DeviceContext_OMSetRenderTargets(blitter.common->context, 1, &blitter.common->rtView, 0);
        ID3D11DeviceContext_Draw(blitter.common->context, 4, 0);
    }

    HRESULT presentResult = IDXGISwapChain1_Present(blitter.common->swapChain, 1, 0);
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
    f32     nearClipZ;
    f32     farClipZ;
    u8      pad[4];
} D3D11ConstCamera;

typedef struct D3D11ConstMesh {
    Rotor3f orientation;
    V3f     pos;
    u8      pad1[4];
} D3D11ConstMesh;

typedef struct D3D11FontConstDims {
    V2f screen;
    V2f tex;
} D3D11FontConstDims;

typedef struct D3D11TriFilledConstDims {
    V2f screen;
    V2f pad;
} D3D11TriFilledConstDims;

typedef struct D3D11TriFilledVertex {
    V2f     pos;
    Color01 color;
} D3D11TriFilledVertex;

typedef struct D3D11FontVertex {
    V2f     scr;
    V2f     tex;
    Color01 color;
} D3D11FontVertex;

typedef struct D3D11Renderer {
    D3D11Common* common;

    ID3D11RasterizerState*   rasterizerState;
    ID3D11DepthStencilState* depthSpencilState;
    ID3D11DepthStencilView*  depthStencilView;

    struct {
        ID3D11Buffer* vbuffer;
        ID3D11Buffer* ibuffer;
        ID3D11Buffer* colorBuffer;
        VSPS          vsps;
        ID3D11Buffer* constCamera;
        ID3D11Buffer* constMesh;
    } mesh;

    struct {
        isize         vertexCap;
        ID3D11Buffer* vertices;
        VSPS          vsps;
        ID3D11Buffer* constDims;
    } triFilled;

    struct {
        isize                     vertexCap;
        ID3D11Buffer*             vertices;
        VSPS                      vsps;
        ID3D11ShaderResourceView* textureView;
        ID3D11SamplerState*       sampler;
        ID3D11BlendState*         blend;
        ID3D11Buffer*             constDims;
    } font;
} D3D11Renderer;

static D3D11Renderer
initD3D11Renderer(D3D11Common* common, State* state) {
    TempMemory temp = beginTempMemory(&state->scratch);

    D3D11Renderer renderer = {.common = common};

    {
        D3D11_BUFFER_DESC desc = {
            .ByteWidth = (UINT)(state->meshStorage.vertices.len * sizeof(*state->meshStorage.vertices.ptr)),
            .Usage = D3D11_USAGE_IMMUTABLE,
            .BindFlags = D3D11_BIND_VERTEX_BUFFER,
        };
        D3D11_SUBRESOURCE_DATA initial = {.pSysMem = state->meshStorage.vertices.ptr};
        ID3D11Device_CreateBuffer(common->device, &desc, &initial, &renderer.mesh.vbuffer);
    }

    {
        D3D11_BUFFER_DESC desc = {
            .ByteWidth = (UINT)(state->meshStorage.indices.len * sizeof(*state->meshStorage.indices.ptr)),
            .Usage = D3D11_USAGE_IMMUTABLE,
            .BindFlags = D3D11_BIND_INDEX_BUFFER,
        };
        D3D11_SUBRESOURCE_DATA initial = {.pSysMem = state->meshStorage.indices.ptr};
        ID3D11Device_CreateBuffer(common->device, &desc, &initial, &renderer.mesh.ibuffer);
    }

    {
        D3D11_BUFFER_DESC desc = {
            .ByteWidth = (UINT)(state->meshStorage.colors.len * sizeof(*state->meshStorage.colors.ptr)),
            .Usage = D3D11_USAGE_IMMUTABLE,
            .BindFlags = D3D11_BIND_VERTEX_BUFFER,
        };
        D3D11_SUBRESOURCE_DATA initial = {.pSysMem = state->meshStorage.colors.ptr};
        ID3D11Device_CreateBuffer(common->device, &desc, &initial, &renderer.mesh.colorBuffer);
    }

    {
        renderer.triFilled.vertexCap = 1024;
        D3D11_BUFFER_DESC desc = {
            .ByteWidth = (UINT)(renderer.triFilled.vertexCap * sizeof(D3D11TriFilledVertex)),
            .Usage = D3D11_USAGE_DYNAMIC,
            .BindFlags = D3D11_BIND_VERTEX_BUFFER,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        };
        ID3D11Device_CreateBuffer(common->device, &desc, 0, &renderer.triFilled.vertices);
    }

    {
        renderer.font.vertexCap = 1024;
        D3D11_BUFFER_DESC desc = {
            .ByteWidth = (UINT)(renderer.font.vertexCap * sizeof(D3D11FontVertex)),
            .Usage = D3D11_USAGE_DYNAMIC,
            .BindFlags = D3D11_BIND_VERTEX_BUFFER,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        };
        ID3D11Device_CreateBuffer(common->device, &desc, 0, &renderer.font.vertices);
    }

    {
        D3D11_INPUT_ELEMENT_DESC desc[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };

        renderer.mesh.vsps = compileVSPS(desc, arrayCount(desc), L"code/renderer.hlsl", common->device, &state->scratch);
    }

    {
        D3D11_INPUT_ELEMENT_DESC desc[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(D3D11TriFilledVertex, pos), D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(D3D11TriFilledVertex, color), D3D11_INPUT_PER_VERTEX_DATA, 0},
        };

        renderer.triFilled.vsps = compileVSPS(desc, arrayCount(desc), L"code/trifilled.hlsl", common->device, &state->scratch);
    }

    {
        D3D11_INPUT_ELEMENT_DESC desc[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(D3D11FontVertex, scr), D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(D3D11FontVertex, tex), D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(D3D11FontVertex, color), D3D11_INPUT_PER_VERTEX_DATA, 0},
        };

        renderer.font.vsps = compileVSPS(desc, arrayCount(desc), L"code/font.hlsl", common->device, &state->scratch);
    }

    {
        D3D11_TEXTURE2D_DESC desc = {
            .Width = (UINT)state->font.atlasW,
            .Height = (UINT)state->font.atlasH,
            .MipLevels = 1,
            .ArraySize = 1,
            .Format = DXGI_FORMAT_A8_UNORM,
            .SampleDesc = {.Count = 1, .Quality = 0},
            .Usage = D3D11_USAGE_IMMUTABLE,
            .BindFlags = D3D11_BIND_SHADER_RESOURCE,
        };

        D3D11_SUBRESOURCE_DATA initial = {.pSysMem = state->font.atlas, .SysMemPitch = (UINT)state->font.atlasW};

        ID3D11Texture2D* tex = 0;
        ID3D11Device_CreateTexture2D(common->device, &desc, &initial, &tex);
        ID3D11Device_CreateShaderResourceView(common->device, (ID3D11Resource*)tex, 0, &renderer.font.textureView);
        ID3D11Texture2D_Release(tex);
    }

    {
        D3D11_BLEND_DESC desc = {
            .RenderTarget[0] = {
                .BlendEnable = TRUE,
                .SrcBlend = D3D11_BLEND_SRC_ALPHA,
                .DestBlend = D3D11_BLEND_INV_SRC_ALPHA,
                .BlendOp = D3D11_BLEND_OP_ADD,
                .SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA,
                .DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA,
                .BlendOpAlpha = D3D11_BLEND_OP_ADD,
                .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL,
            },
        };
        ID3D11Device_CreateBlendState(common->device, &desc, &renderer.font.blend);
    }

    {
        D3D11_SAMPLER_DESC desc = {
            .Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
            .AddressU = D3D11_TEXTURE_ADDRESS_WRAP,
            .AddressV = D3D11_TEXTURE_ADDRESS_WRAP,
            .AddressW = D3D11_TEXTURE_ADDRESS_WRAP,
        };
        ID3D11Device_CreateSamplerState(common->device, &desc, &renderer.font.sampler);
    }

    {
        D3D11_RASTERIZER_DESC desc = {
            .FillMode = D3D11_FILL_SOLID,
            .CullMode = D3D11_CULL_BACK,
            .DepthClipEnable = true,
        };
        ID3D11Device_CreateRasterizerState(common->device, &desc, &renderer.rasterizerState);
    }

    {
        D3D11_BUFFER_DESC desc = {
            .ByteWidth = sizeof(D3D11ConstCamera),
            .Usage = D3D11_USAGE_DYNAMIC,
            .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        };
        ID3D11Device_CreateBuffer(common->device, &desc, 0, &renderer.mesh.constCamera);

        desc.ByteWidth = sizeof(D3D11ConstMesh);
        ID3D11Device_CreateBuffer(common->device, &desc, 0, &renderer.mesh.constMesh);

        // TODO(khvorov) Resizing
        {
            desc.ByteWidth = sizeof(D3D11FontConstDims);
            D3D11FontConstDims     initDims = {.screen = {(f32)state->windowWidth, (f32)state->windowHeight}, .tex = {(f32)state->font.atlasW, (f32)state->font.atlasH}};
            D3D11_SUBRESOURCE_DATA init = {.pSysMem = &initDims};
            ID3D11Device_CreateBuffer(common->device, &desc, &init, &renderer.font.constDims);
        }

        // TODO(khvorov) Resizing
        {
            desc.ByteWidth = sizeof(D3D11TriFilledConstDims);
            D3D11TriFilledConstDims initDims = {.screen = {(f32)state->windowWidth, (f32)state->windowHeight}};
            D3D11_SUBRESOURCE_DATA  init = {.pSysMem = &initDims};
            ID3D11Device_CreateBuffer(common->device, &desc, &init, &renderer.triFilled.constDims);
        }
    }

    {
        D3D11_DEPTH_STENCIL_DESC desc = {
            .DepthEnable = TRUE,
            .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL,
            .DepthFunc = D3D11_COMPARISON_LESS,
            .StencilEnable = FALSE,
            .StencilReadMask = 0xFF,
            .StencilWriteMask = 0xFF,
            .FrontFace = {
                .StencilFailOp = D3D11_STENCIL_OP_KEEP,
                .StencilDepthFailOp = D3D11_STENCIL_OP_KEEP,
                .StencilPassOp = D3D11_STENCIL_OP_KEEP,
                .StencilFunc = D3D11_COMPARISON_ALWAYS,
            },
            .BackFace = {
                .StencilFailOp = D3D11_STENCIL_OP_KEEP,
                .StencilDepthFailOp = D3D11_STENCIL_OP_KEEP,
                .StencilPassOp = D3D11_STENCIL_OP_KEEP,
                .StencilFunc = D3D11_COMPARISON_ALWAYS,
            },
        };
        ID3D11Device_CreateDepthStencilState(common->device, &desc, &renderer.depthSpencilState);
    }

    {
        // TODO(khvorov) Resizing
        D3D11_TEXTURE2D_DESC desc = {
            .Width = (UINT)state->windowWidth,
            .Height = (UINT)state->windowHeight,
            .MipLevels = 1,
            .ArraySize = 1,
            .Format = DXGI_FORMAT_D32_FLOAT,
            .SampleDesc = {1, 0},
            .Usage = D3D11_USAGE_DEFAULT,
            .BindFlags = D3D11_BIND_DEPTH_STENCIL,
        };

        ID3D11Texture2D* depth = 0;
        ID3D11Device_CreateTexture2D(common->device, &desc, NULL, &depth);
        ID3D11Device_CreateDepthStencilView(common->device, (ID3D11Resource*)depth, NULL, &renderer.depthStencilView);
        ID3D11Texture2D_Release(depth);
    }

    endTempMemory(temp);
    return renderer;
}

static void
d3d11render(D3D11Renderer renderer, State* state) {
    {
        UINT          offsets[] = {0, 0};
        UINT          strides[] = {sizeof(*state->meshStorage.vertices.ptr), sizeof(*state->meshStorage.colors.ptr)};
        ID3D11Buffer* buffers[] = {renderer.mesh.vbuffer, renderer.mesh.colorBuffer};
        ID3D11DeviceContext_IASetVertexBuffers(renderer.common->context, 0, arrayCount(buffers), buffers, strides, offsets);
    }

    ID3D11DeviceContext_IASetIndexBuffer(renderer.common->context, renderer.mesh.ibuffer, DXGI_FORMAT_R32_UINT, 0);
    ID3D11DeviceContext_VSSetShader(renderer.common->context, renderer.mesh.vsps.vs, NULL, 0);
    ID3D11DeviceContext_IASetInputLayout(renderer.common->context, renderer.mesh.vsps.layout);
    ID3D11DeviceContext_IASetPrimitiveTopology(renderer.common->context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ID3D11DeviceContext_PSSetShader(renderer.common->context, renderer.mesh.vsps.ps, NULL, 0);
    ID3D11DeviceContext_RSSetState(renderer.common->context, renderer.rasterizerState);
    ID3D11DeviceContext_OMSetDepthStencilState(renderer.common->context, renderer.depthSpencilState, 0);

    {
        ID3D11Buffer* buffers[] = {renderer.mesh.constCamera, renderer.mesh.constMesh};
        ID3D11DeviceContext_VSSetConstantBuffers(renderer.common->context, 0, arrayCount(buffers), buffers);
    }

    {
        D3D11_MAPPED_SUBRESOURCE mappedCamera = {};
        ID3D11DeviceContext_Map(renderer.common->context, (ID3D11Resource*)renderer.mesh.constCamera, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedCamera);
        D3D11ConstCamera* constCamera = (D3D11ConstCamera*)mappedCamera.pData;
        constCamera->orientation = state->camera.currentOrientation;
        constCamera->pos = state->camera.pos;
        constCamera->tanHalfFovX = state->camera.tanHalfFov.x;
        constCamera->tanHalfFovY = state->camera.tanHalfFov.y;
        constCamera->nearClipZ = state->camera.nearClipZ;
        constCamera->farClipZ = state->camera.farClipZ;
        ID3D11DeviceContext_Unmap(renderer.common->context, (ID3D11Resource*)renderer.mesh.constCamera, 0);
    }

    {
        FLOAT color[] = {0.0f, 0.0, 0.0f, 1.f};
        ID3D11DeviceContext_ClearRenderTargetView(renderer.common->context, renderer.common->rtView, color);
    }

    ID3D11DeviceContext_ClearDepthStencilView(renderer.common->context, renderer.depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    ID3D11DeviceContext_OMSetRenderTargets(renderer.common->context, 1, &renderer.common->rtView, renderer.depthStencilView);

    for (isize meshIndex = 0; meshIndex < state->meshes.len; meshIndex++) {
        Mesh mesh = state->meshes.ptr[meshIndex];

        D3D11_MAPPED_SUBRESOURCE mappedMesh = {};
        ID3D11DeviceContext_Map(renderer.common->context, (ID3D11Resource*)renderer.mesh.constMesh, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedMesh);
        D3D11ConstMesh* constMesh = (D3D11ConstMesh*)mappedMesh.pData;
        constMesh->orientation = mesh.orientation;
        constMesh->pos = mesh.pos;
        ID3D11DeviceContext_Unmap(renderer.common->context, (ID3D11Resource*)renderer.mesh.constMesh, 0);

        i32 baseVertex = mesh.vertices.ptr - state->meshStorage.vertices.ptr;
        i32 baseIndex = (i32*)mesh.indices.ptr - (i32*)state->meshStorage.indices.ptr;
        ID3D11DeviceContext_DrawIndexed(renderer.common->context, mesh.indices.len * 3, baseIndex, baseVertex);
    }

    if (state->showDebugUI) {
        ID3D11DeviceContext_OMSetRenderTargets(renderer.common->context, 1, &renderer.common->rtView, 0);

        D3D11_MAPPED_SUBRESOURCE mappedTriFilledVertices = {};
        ID3D11DeviceContext_Map(renderer.common->context, (ID3D11Resource*)renderer.triFilled.vertices, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedTriFilledVertices);

        struct {
            D3D11TriFilledVertex* ptr;
            isize                 len;
            isize                 cap;
        } triFilled = {(D3D11TriFilledVertex*)mappedTriFilledVertices.pData, 0, renderer.triFilled.vertexCap};

        D3D11_MAPPED_SUBRESOURCE mappedFontVertices = {};
        ID3D11DeviceContext_Map(renderer.common->context, (ID3D11Resource*)renderer.font.vertices, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedFontVertices);

        struct {
            D3D11FontVertex* ptr;
            isize            len;
            isize            cap;
        } font = {(D3D11FontVertex*)mappedFontVertices.pData, 0, renderer.font.vertexCap};

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

                case NK_COMMAND_TEXT: {
                    struct nk_command_text* text = (struct nk_command_text*)cmd;

                    Str     str = {text->string, text->length};
                    Color01 color = color255to01(nkcolorTo255(text->foreground));

                    i32 curx = text->x;
                    for (isize strInd = 0; strInd < str.len; strInd++) {
                        char  ch = str.ptr[strInd];
                        Glyph glyph = state->font.ascii[(i32)ch];

                        i32 texx0 = glyph.x;
                        i32 texx1 = glyph.x + glyph.w;
                        i32 texy0 = glyph.y;
                        i32 texy1 = glyph.y + glyph.h;

                        V2f textopleft = {(f32)texx0, (f32)texy0};
                        V2f textopright = {(f32)texx1, (f32)texy0};
                        V2f texbottomleft = {(f32)texx0, (f32)texy1};
                        V2f texbottomright = {(f32)texx1, (f32)texy1};

                        i32 scrx0 = curx;
                        i32 scrx1 = scrx0 + glyph.w;
                        i32 scry0 = text->y;
                        i32 scry1 = scry0 + glyph.h;

                        V2f scrtopleft = {(f32)scrx0, (f32)scry0};
                        V2f scrtopright = {(f32)scrx1, (f32)scry0};
                        V2f scrbottomleft = {(f32)scrx0, (f32)scry1};
                        V2f scrbottomright = {(f32)scrx1, (f32)scry1};

                        arrpush(font, ((D3D11FontVertex) {scrtopleft, textopleft, color}));
                        arrpush(font, ((D3D11FontVertex) {scrtopright, textopright, color}));
                        arrpush(font, ((D3D11FontVertex) {scrbottomleft, texbottomleft, color}));

                        arrpush(font, ((D3D11FontVertex) {scrtopright, textopright, color}));
                        arrpush(font, ((D3D11FontVertex) {scrbottomright, texbottomright, color}));
                        arrpush(font, ((D3D11FontVertex) {scrbottomleft, texbottomleft, color}));

                        curx += glyph.advance;
                    }

                } break;

                case NK_COMMAND_IMAGE: break;
                case NK_COMMAND_CUSTOM: break;
            }
        }

        ID3D11DeviceContext_Unmap(renderer.common->context, (ID3D11Resource*)renderer.triFilled.vertices, 0);
        ID3D11DeviceContext_Unmap(renderer.common->context, (ID3D11Resource*)renderer.font.vertices, 0);

        {
            UINT          offsets[] = {0};
            UINT          strides[] = {sizeof(D3D11TriFilledVertex)};
            ID3D11Buffer* buffers[] = {renderer.triFilled.vertices};
            ID3D11DeviceContext_IASetVertexBuffers(renderer.common->context, 0, arrayCount(buffers), buffers, strides, offsets);
        }

        {
            ID3D11Buffer* buffers[] = {renderer.triFilled.constDims};
            ID3D11DeviceContext_VSSetConstantBuffers(renderer.common->context, 0, arrayCount(buffers), buffers);
        }

        ID3D11DeviceContext_VSSetShader(renderer.common->context, renderer.triFilled.vsps.vs, NULL, 0);
        ID3D11DeviceContext_IASetInputLayout(renderer.common->context, renderer.triFilled.vsps.layout);
        ID3D11DeviceContext_IASetPrimitiveTopology(renderer.common->context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        ID3D11DeviceContext_PSSetShader(renderer.common->context, renderer.triFilled.vsps.ps, NULL, 0);

        ID3D11DeviceContext_Draw(renderer.common->context, triFilled.len, 0);

        {
            UINT          offsets[] = {0};
            UINT          strides[] = {sizeof(D3D11FontVertex)};
            ID3D11Buffer* buffers[] = {renderer.font.vertices};
            ID3D11DeviceContext_IASetVertexBuffers(renderer.common->context, 0, arrayCount(buffers), buffers, strides, offsets);
        }

        {
            ID3D11Buffer* buffers[] = {renderer.font.constDims};
            ID3D11DeviceContext_VSSetConstantBuffers(renderer.common->context, 0, arrayCount(buffers), buffers);
        }

        ID3D11DeviceContext_PSSetShaderResources(renderer.common->context, 0, 1, &renderer.font.textureView);
        ID3D11DeviceContext_PSSetSamplers(renderer.common->context, 0, 1, &renderer.font.sampler);

        ID3D11DeviceContext_VSSetShader(renderer.common->context, renderer.font.vsps.vs, NULL, 0);
        ID3D11DeviceContext_IASetInputLayout(renderer.common->context, renderer.font.vsps.layout);
        ID3D11DeviceContext_IASetPrimitiveTopology(renderer.common->context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        ID3D11DeviceContext_PSSetShader(renderer.common->context, renderer.font.vsps.ps, NULL, 0);

        ID3D11DeviceContext_OMSetBlendState(renderer.common->context, renderer.font.blend, NULL, ~0U);

        ID3D11DeviceContext_Draw(renderer.common->context, font.len, 0);
    }

    HRESULT presentResult = IDXGISwapChain1_Present(renderer.common->swapChain, 1, 0);
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

// NOTE(khvorov) Only to be accessed in windowProc
static State* globalState = 0;

LRESULT CALLBACK
windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    LRESULT result = 0;
    switch (uMsg) {
        case WM_DESTROY: PostQuitMessage(0); break;
        case WM_ERASEBKGND: result = TRUE; break;
        case WM_KILLFOCUS: {
            inputClearKeys(&globalState->input);
        } break;
        case WM_SETFOCUS: {
            RECT rect = {};
            GetClientRect(hwnd, &rect);
            POINT topleft = {rect.left, rect.top};
            ClientToScreen(hwnd, &topleft);
            POINT bottomright = {rect.right, rect.bottom};
            ClientToScreen(hwnd, &bottomright);
            RECT screen = {.left = topleft.x, .right = bottomright.x, .top = topleft.y, .bottom = bottomright.y};
            ClipCursor(&screen);
        } break;
        default: result = DefWindowProcW(hwnd, uMsg, wParam, lParam); break;
    }
    return result;
}

int WINAPI
WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    unused(hPrevInstance);
    unused(lpCmdLine);
    unused(nCmdShow);

    Timer timer = {.clock = createClock(), .update = getClockMarker()};

    f64 rdtscFreqPerMicrosecond = 1.0f;
#ifdef TRIAXIS_profile
    {
        u64 begin = __rdtsc();
        Sleep(100);
        f64 waited = msSinceLastUpdate(&timer);
        u64 end = __rdtsc();
        u64 ticksu = end - begin;
        f64 ticksf = (f64)ticksu;
        f64 microseconds = waited * 1000.0f;
        rdtscFreqPerMicrosecond = ticksf / microseconds;
    }
#endif

    State* state = 0;
    {
        isize memSize = 1 * 1024 * 1024 * 1024;
        void* memBase = VirtualAlloc(0, memSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        assert(memBase);
        state = initState(memBase, memSize, rdtscFreqPerMicrosecond);
    }

    globalState = state;
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

    if (!state->showDebugUI) {
        ShowCursor(FALSE);
    }

    // NOTE(khvorov) Windows will sleep for random amounts of time if we don't do this
    {
        TIMECAPS caps = {};
        timeGetDevCaps(&caps, sizeof(TIMECAPS));
        timeBeginPeriod(caps.wPeriodMin);
    }

    swRendererSetImageSize(&state->swRenderer, state->windowWidth, state->windowHeight);
    D3D11Common   d3d11common = initD3D11Common(window, state->windowWidth, state->windowHeight);
    D3D11Blitter  d3d11blitter = initD3D11Blitter(&d3d11common, state->windowWidth, state->windowHeight, &state->scratch);
    D3D11Renderer d3d11renderer = initD3D11Renderer(&d3d11common, state);

    for (bool running = true; running;) {
        timedSectionStart("frame");

        assert(state->scratch.tempCount == 0);
        assert(state->scratch.used == 0);

        // NOTE(khvorov) Input
        {
            timedSectionStart("input");

            inputBeginFrame(&state->input);
            for (MSG msg = {}; PeekMessageA(&msg, 0, 0, 0, PM_REMOVE);) {
                switch (msg.message) {
                    case WM_QUIT: running = false; break;

                    case WM_SYSKEYDOWN:
                    case WM_SYSKEYUP:
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
                            case 'O': key = InputKey_ToggleOutlines; break;
                            case 'T': key = InputKey_ToggleSW; break;
                            case VK_ESCAPE: key = InputKey_ToggleDebugUI; break;

                            case VK_F4: {
                                keyFound = false;
                                if (msg.message == WM_SYSKEYDOWN) {
                                    running = false;
                                }
                            } break;

                            default: keyFound = false; break;
                        }
                        if (keyFound) {
                            if (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN) {
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

            timedSectionEnd();
        }

        bool prevShowDebugUI = state->showDebugUI;
        {
            f32 ms = msSinceLastUpdate(&timer);
            update(state, ms / 1000.0f);
        }
        if (prevShowDebugUI != state->showDebugUI) {
            ShowCursor(state->showDebugUI);
        }

        if (state->useSW) {
            {
                swRender(state);
            }

            d3d11blit(d3d11blitter, state->swRenderer.texture);
        } else {
            d3d11render(d3d11renderer, state);
        }

        nk_clear(&state->ui);

        timedSectionEnd();
    }

    // TODO(khvorov) Does this prevent bluescreens?
    {
        ID3D11Buffer_Release(d3d11renderer.mesh.vbuffer);
        ID3D11Buffer_Release(d3d11renderer.mesh.ibuffer);
        ID3D11Buffer_Release(d3d11renderer.mesh.colorBuffer);
        ID3D11VertexShader_Release(d3d11renderer.mesh.vsps.vs);
        ID3D11InputLayout_Release(d3d11renderer.mesh.vsps.layout);
        ID3D11PixelShader_Release(d3d11renderer.mesh.vsps.ps);
        ID3D11Buffer_Release(d3d11renderer.mesh.constCamera);
        ID3D11Buffer_Release(d3d11renderer.mesh.constMesh);

        ID3D11RasterizerState_Release(d3d11renderer.rasterizerState);
        ID3D11DepthStencilState_Release(d3d11renderer.depthSpencilState);
        ID3D11DepthStencilView_Release(d3d11renderer.depthStencilView);

        ID3D11Buffer_Release(d3d11renderer.triFilled.vertices);
        ID3D11VertexShader_Release(d3d11renderer.triFilled.vsps.vs);
        ID3D11InputLayout_Release(d3d11renderer.triFilled.vsps.layout);
        ID3D11PixelShader_Release(d3d11renderer.triFilled.vsps.ps);
        ID3D11Buffer_Release(d3d11renderer.triFilled.constDims);

        ID3D11Buffer_Release(d3d11renderer.font.vertices);
        ID3D11VertexShader_Release(d3d11renderer.font.vsps.vs);
        ID3D11InputLayout_Release(d3d11renderer.font.vsps.layout);
        ID3D11PixelShader_Release(d3d11renderer.font.vsps.ps);
        ID3D11ShaderResourceView_Release(d3d11renderer.font.textureView);
        ID3D11SamplerState_Release(d3d11renderer.font.sampler);
        ID3D11BlendState_Release(d3d11renderer.font.blend);
        ID3D11Buffer_Release(d3d11renderer.font.constDims);

        ID3D11Buffer_Release(d3d11blitter.vbuffer);
        ID3D11VertexShader_Release(d3d11blitter.vsps.vs);
        ID3D11PixelShader_Release(d3d11blitter.vsps.ps);
        ID3D11InputLayout_Release(d3d11blitter.vsps.layout);
        ID3D11Texture1D_Release(d3d11blitter.tex.tex2d);
        ID3D11ShaderResourceView_Release(d3d11blitter.tex.view);
        ID3D11SamplerState_Release(d3d11blitter.sampler);
        ID3D11RasterizerState_Release(d3d11blitter.rasterizerState);

        IDXGISwapChain1_Release(d3d11common.swapChain);
        ID3D11RenderTargetView_Release(d3d11common.rtView);
        ID3D11Device_Release(d3d11common.device);
        ID3D11DeviceContext_Release(d3d11common.context);
    }

    spall_buffer_quit(&globalSpallProfile, &globalSpallBuffer);
    spall_quit(&globalSpallProfile);

    return 0;
}

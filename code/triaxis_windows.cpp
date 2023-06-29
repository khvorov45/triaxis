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
    TracyD3D11Ctx           tracyD3D11Context;
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

    D3D11Common common = {
        .context = context,
        .device = device,
        .rtView = rtView,
        .swapChain = swapChain,
        .tracyD3D11Context = tracyD3D11Context,
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
        char* msg = (char*)error->GetBufferPointer();
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
        device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), NULL, &result.vs);
        device->CreateInputLayout(desc, descCount, blob->GetBufferPointer(), blob->GetBufferSize(), &result.layout);
        blob->Release();
    }

    {
        ID3DBlob* pblob = compileShader(hlsl, "ps", "ps_5_0");
        device->CreatePixelShader(pblob->GetBufferPointer(), pblob->GetBufferSize(), NULL, &result.ps);
        pblob->Release();
    }

    endTempMemory(temp);
    return result;
}

typedef struct D3D11BlitterVertex {
    f32 pos[2];
    f32 uv[2];
} D3D11BlitterVertex;

typedef struct D3D11Blitter {
    D3D11Common*              common;
    ID3D11Buffer*             vbuffer;
    VSPS                      vsps;
    ID3D11Texture2D*          texture;
    ID3D11ShaderResourceView* textureView;
    ID3D11SamplerState*       sampler;
    ID3D11RasterizerState*    rasterizerState;
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
        common->device->CreateBuffer(&desc, &initial, &blitter.vbuffer);
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

    endTempMemory(temp);
    return blitter;
}

static void
d3d11blit(D3D11Blitter blitter, Texture tex) {
    {
        UINT offset = 0;
        UINT stride = sizeof(D3D11BlitterVertex);
        blitter.common->context->IASetVertexBuffers(0, 1, &blitter.vbuffer, &stride, &offset);
    }

    blitter.common->context->VSSetShader(blitter.vsps.vs, NULL, 0);
    blitter.common->context->IASetInputLayout(blitter.vsps.layout);
    blitter.common->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    blitter.common->context->PSSetShader(blitter.vsps.ps, NULL, 0);
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

    ID3D11RasterizerState* rasterizerState;

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
        common->device->CreateBuffer(&desc, &initial, &renderer.mesh.vbuffer);
    }

    {
        D3D11_BUFFER_DESC desc = {
            .ByteWidth = (UINT)(state->meshStorage.indices.len * sizeof(*state->meshStorage.indices.ptr)),
            .Usage = D3D11_USAGE_IMMUTABLE,
            .BindFlags = D3D11_BIND_INDEX_BUFFER,
        };
        D3D11_SUBRESOURCE_DATA initial = {.pSysMem = state->meshStorage.indices.ptr};
        common->device->CreateBuffer(&desc, &initial, &renderer.mesh.ibuffer);
    }

    {
        D3D11_BUFFER_DESC desc = {
            .ByteWidth = (UINT)(state->meshStorage.colors.len * sizeof(*state->meshStorage.colors.ptr)),
            .Usage = D3D11_USAGE_IMMUTABLE,
            .BindFlags = D3D11_BIND_VERTEX_BUFFER,
        };
        D3D11_SUBRESOURCE_DATA initial = {.pSysMem = state->meshStorage.colors.ptr};
        common->device->CreateBuffer(&desc, &initial, &renderer.mesh.colorBuffer);
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
        renderer.font.vertexCap = 1024;
        D3D11_BUFFER_DESC desc = {
            .ByteWidth = (UINT)(renderer.font.vertexCap * sizeof(D3D11FontVertex)),
            .Usage = D3D11_USAGE_DYNAMIC,
            .BindFlags = D3D11_BIND_VERTEX_BUFFER,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        };
        common->device->CreateBuffer(&desc, 0, &renderer.font.vertices);
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
        common->device->CreateTexture2D(&desc, &initial, &tex);
        common->device->CreateShaderResourceView((ID3D11Resource*)tex, 0, &renderer.font.textureView);
        tex->Release();
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
        common->device->CreateBlendState(&desc, &renderer.font.blend);
    }

    {
        D3D11_SAMPLER_DESC desc = {
            .Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
            .AddressU = D3D11_TEXTURE_ADDRESS_WRAP,
            .AddressV = D3D11_TEXTURE_ADDRESS_WRAP,
            .AddressW = D3D11_TEXTURE_ADDRESS_WRAP,
        };
        common->device->CreateSamplerState(&desc, &renderer.font.sampler);
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
        common->device->CreateBuffer(&desc, 0, &renderer.mesh.constCamera);

        desc.ByteWidth = sizeof(D3D11ConstMesh);
        common->device->CreateBuffer(&desc, 0, &renderer.mesh.constMesh);

        // TODO(khvorov) Resizing
        {
            desc.ByteWidth = sizeof(D3D11FontConstDims);
            D3D11FontConstDims     initDims = {.screen = {(f32)state->windowWidth, (f32)state->windowHeight}, .tex = {(f32)state->font.atlasW, (f32)state->font.atlasH}};
            D3D11_SUBRESOURCE_DATA init = {.pSysMem = &initDims};
            common->device->CreateBuffer(&desc, &init, &renderer.font.constDims);
        }

        // TODO(khvorov) Resizing
        {
            desc.ByteWidth = sizeof(D3D11TriFilledConstDims);
            D3D11TriFilledConstDims initDims = {.screen = {(f32)state->windowWidth, (f32)state->windowHeight}};
            D3D11_SUBRESOURCE_DATA  init = {.pSysMem = &initDims};
            common->device->CreateBuffer(&desc, &init, &renderer.triFilled.constDims);
        }
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
        renderer.common->context->IASetVertexBuffers(0, arrayCount(buffers), buffers, strides, offsets);
    }

    renderer.common->context->IASetIndexBuffer(renderer.mesh.ibuffer, DXGI_FORMAT_R32_UINT, 0);
    renderer.common->context->VSSetShader(renderer.mesh.vsps.vs, NULL, 0);
    renderer.common->context->IASetInputLayout(renderer.mesh.vsps.layout);
    renderer.common->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    renderer.common->context->PSSetShader(renderer.mesh.vsps.ps, NULL, 0);
    renderer.common->context->RSSetState(renderer.rasterizerState);

    {
        ID3D11Buffer* buffers[] = {renderer.mesh.constCamera, renderer.mesh.constMesh};
        renderer.common->context->VSSetConstantBuffers(0, arrayCount(buffers), buffers);
    }

    {
        D3D11_MAPPED_SUBRESOURCE mappedCamera = {};
        renderer.common->context->Map((ID3D11Resource*)renderer.mesh.constCamera, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedCamera);
        D3D11ConstCamera* constCamera = (D3D11ConstCamera*)mappedCamera.pData;
        constCamera->orientation = state->camera.currentOrientation;
        constCamera->pos = state->camera.pos;
        constCamera->tanHalfFovX = state->camera.tanHalfFov.x;
        constCamera->tanHalfFovY = state->camera.tanHalfFov.y;
        renderer.common->context->Unmap((ID3D11Resource*)renderer.mesh.constCamera, 0);
    }

    {
        FLOAT color[] = {0.0f, 0.0, 0.0f, 1.f};
        renderer.common->context->ClearRenderTargetView(renderer.common->rtView, color);
    }

    renderer.common->context->OMSetRenderTargets(1, &renderer.common->rtView, 0);

    for (isize meshIndex = 0; meshIndex < state->meshes.len; meshIndex++) {
        Mesh mesh = state->meshes.ptr[meshIndex];

        D3D11_MAPPED_SUBRESOURCE mappedMesh = {};
        renderer.common->context->Map((ID3D11Resource*)renderer.mesh.constMesh, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedMesh);
        D3D11ConstMesh* constMesh = (D3D11ConstMesh*)mappedMesh.pData;
        constMesh->orientation = mesh.orientation;
        constMesh->pos = mesh.pos;
        renderer.common->context->Unmap((ID3D11Resource*)renderer.mesh.constMesh, 0);

        i32 baseVertex = mesh.vertices.ptr - state->meshStorage.vertices.ptr;
        i32 baseIndex = (i32*)mesh.indices.ptr - (i32*)state->meshStorage.indices.ptr;
        renderer.common->context->DrawIndexed(mesh.indices.len * 3, baseIndex, baseVertex);
    }

    if (state->showDebugUI) {
        D3D11_MAPPED_SUBRESOURCE mappedTriFilledVertices = {};
        renderer.common->context->Map((ID3D11Resource*)renderer.triFilled.vertices, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedTriFilledVertices);

        struct {
            D3D11TriFilledVertex* ptr;
            isize                 len;
            isize                 cap;
        } triFilled = {(D3D11TriFilledVertex*)mappedTriFilledVertices.pData, 0, renderer.triFilled.vertexCap};

        D3D11_MAPPED_SUBRESOURCE mappedFontVertices = {};
        renderer.common->context->Map((ID3D11Resource*)renderer.font.vertices, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedFontVertices);

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

        renderer.common->context->Unmap((ID3D11Resource*)renderer.triFilled.vertices, 0);
        renderer.common->context->Unmap((ID3D11Resource*)renderer.font.vertices, 0);

        {
            UINT          offsets[] = {0};
            UINT          strides[] = {sizeof(D3D11TriFilledVertex)};
            ID3D11Buffer* buffers[] = {renderer.triFilled.vertices};
            renderer.common->context->IASetVertexBuffers(0, arrayCount(buffers), buffers, strides, offsets);
        }

        {
            ID3D11Buffer* buffers[] = {renderer.triFilled.constDims};
            renderer.common->context->VSSetConstantBuffers(0, arrayCount(buffers), buffers);
        }

        renderer.common->context->VSSetShader(renderer.triFilled.vsps.vs, NULL, 0);
        renderer.common->context->IASetInputLayout(renderer.triFilled.vsps.layout);
        renderer.common->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        renderer.common->context->PSSetShader(renderer.triFilled.vsps.ps, NULL, 0);

        renderer.common->context->Draw(triFilled.len, 0);

        {
            UINT          offsets[] = {0};
            UINT          strides[] = {sizeof(D3D11FontVertex)};
            ID3D11Buffer* buffers[] = {renderer.font.vertices};
            renderer.common->context->IASetVertexBuffers(0, arrayCount(buffers), buffers, strides, offsets);
        }

        {
            ID3D11Buffer* buffers[] = {renderer.font.constDims};
            renderer.common->context->VSSetConstantBuffers(0, arrayCount(buffers), buffers);
        }

        renderer.common->context->PSSetShaderResources(0, 1, &renderer.font.textureView);
        renderer.common->context->PSSetSamplers(0, 1, &renderer.font.sampler);

        renderer.common->context->VSSetShader(renderer.font.vsps.vs, NULL, 0);
        renderer.common->context->IASetInputLayout(renderer.font.vsps.layout);
        renderer.common->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        renderer.common->context->PSSetShader(renderer.font.vsps.ps, NULL, 0);

        renderer.common->context->OMSetBlendState(renderer.font.blend, NULL, ~0U);

        renderer.common->context->Draw(font.len, 0);
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
    D3D11Common   d3d11common = initD3D11Common(window, state->windowWidth, state->windowHeight);
    D3D11Blitter  d3d11blitter = initD3D11Blitter(&d3d11common, state->windowWidth, state->windowHeight, &state->scratch);
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
                    } else {
                        inputClearKeys(&state->input);
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
        d3d11renderer.mesh.vbuffer->Release();
        d3d11renderer.mesh.ibuffer->Release();
        d3d11renderer.mesh.colorBuffer->Release();
        d3d11renderer.mesh.vsps.vs->Release();
        d3d11renderer.mesh.vsps.layout->Release();
        d3d11renderer.mesh.vsps.ps->Release();
        d3d11renderer.rasterizerState->Release();
        d3d11renderer.mesh.constCamera->Release();
        d3d11renderer.mesh.constMesh->Release();

        d3d11renderer.triFilled.vertices->Release();
        d3d11renderer.triFilled.vsps.vs->Release();
        d3d11renderer.triFilled.vsps.layout->Release();
        d3d11renderer.triFilled.vsps.ps->Release();
        d3d11renderer.triFilled.constDims->Release();

        d3d11renderer.font.vertices->Release();
        d3d11renderer.font.vsps.vs->Release();
        d3d11renderer.font.vsps.layout->Release();
        d3d11renderer.font.vsps.ps->Release();
        d3d11renderer.font.textureView->Release();
        d3d11renderer.font.sampler->Release();
        d3d11renderer.font.blend->Release();
        d3d11renderer.font.constDims->Release();

        d3d11blitter.vbuffer->Release();
        d3d11blitter.vsps.vs->Release();
        d3d11blitter.vsps.ps->Release();
        d3d11blitter.vsps.layout->Release();
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

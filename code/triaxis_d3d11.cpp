#include "triaxis_d3d11.h"

#define asserthr(x) assert(SUCCEEDED(x))

// TODO(khvorov) Handle resizing

typedef struct D3D11Vertex {
    f32 pos[2];
    f32 uv[2];
} D3D11Vertex;

D3D11Renderer
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

    ID3D11Buffer* vbuffer = 0;
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
        device->CreateBuffer(&desc, &initial, &vbuffer);
    }

    ID3D11InputLayout*  layout = 0;
    ID3D11VertexShader* vshader = 0;
    ID3D11PixelShader*  pshader = 0;
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

        device->CreateVertexShader(vblob->GetBufferPointer(), vblob->GetBufferSize(), NULL, &vshader);
        device->CreatePixelShader(pblob->GetBufferPointer(), pblob->GetBufferSize(), NULL, &pshader);
        device->CreateInputLayout(desc, ARRAYSIZE(desc), vblob->GetBufferPointer(), vblob->GetBufferSize(), &layout);

        pblob->Release();
        vblob->Release();
    }

    ID3D11ShaderResourceView* textureView = 0;
    ID3D11Texture2D*          texture = 0;
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
        device->CreateShaderResourceView((ID3D11Resource*)texture, NULL, &textureView);
    }

    ID3D11SamplerState* sampler = 0;
    {
        D3D11_SAMPLER_DESC desc = {
            .Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
            .AddressU = D3D11_TEXTURE_ADDRESS_WRAP,
            .AddressV = D3D11_TEXTURE_ADDRESS_WRAP,
            .AddressW = D3D11_TEXTURE_ADDRESS_WRAP,
        };

        device->CreateSamplerState(&desc, &sampler);
    }

    ID3D11RasterizerState* rasterizerState = 0;
    {
        D3D11_RASTERIZER_DESC desc = {
            .FillMode = D3D11_FILL_SOLID,
            .CullMode = D3D11_CULL_NONE,
        };
        device->CreateRasterizerState(&desc, &rasterizerState);
    }

    ID3D11RenderTargetView* rtView = 0;
    {
        asserthr(swapChain->ResizeBuffers(0, windowWidth, windowHeight, DXGI_FORMAT_UNKNOWN, 0));

        ID3D11Texture2D* backbuffer = 0;
        swapChain->GetBuffer(0, IID_ID3D11Texture2D, (void**)&backbuffer);
        device->CreateRenderTargetView((ID3D11Resource*)backbuffer, NULL, &rtView);
        assert(rtView);
        backbuffer->Release();

        D3D11_TEXTURE2D_DESC depthDesc = {
            .Width = (UINT)windowWidth,
            .Height = (UINT)windowHeight,
            .MipLevels = 1,
            .ArraySize = 1,
            .Format = DXGI_FORMAT_D32_FLOAT,
            .SampleDesc = {.Count = 1, .Quality = 0},
            .Usage = D3D11_USAGE_DEFAULT,
            .BindFlags = D3D11_BIND_DEPTH_STENCIL,
        };

        ID3D11Texture2D* depth = 0;
        device->CreateTexture2D(&depthDesc, NULL, &depth);
        depth->Release();
    }

    D3D11_VIEWPORT viewport = {
        .TopLeftX = 0,
        .TopLeftY = 0,
        .Width = (FLOAT)windowWidth,
        .Height = (FLOAT)windowHeight,
        .MinDepth = 0,
        .MaxDepth = 1,
    };

    {
        context->IASetInputLayout(layout);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        UINT offset = 0;
        UINT stride = sizeof(D3D11Vertex);
        context->IASetVertexBuffers(0, 1, &vbuffer, &stride, &offset);
    }

    context->VSSetShader(vshader, NULL, 0);

    context->RSSetViewports(1, &viewport);
    context->RSSetState(rasterizerState);

    context->PSSetSamplers(0, 1, &sampler);
    context->PSSetShaderResources(0, 1, &textureView);
    context->PSSetShader(pshader, NULL, 0);

    D3D11Renderer rend = {
        .context = context,
        .rtView = rtView,
        .swapChain = swapChain,
        .texture = texture,
    };
    return rend;
}

void
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

    rend.context->OMSetRenderTargets(1, &rend.rtView, 0);
    rend.context->Draw(4, 0);
    HRESULT presentResult = rend.swapChain->Present(1, 0);
    asserthr(presentResult);
    if (presentResult == DXGI_STATUS_OCCLUDED) {
        Sleep(10);
    }
}

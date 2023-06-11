#include "triaxis.h"

#undef function
#define COBJMACROS
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>

typedef struct D3D11Renderer {
    ID3D11DeviceContext*    context;
    ID3D11RenderTargetView* rtView;
    IDXGISwapChain1*        swapChain;
    ID3D11InputLayout*      layout;
    ID3D11Texture2D*        texture;
} D3D11Renderer;

D3D11Renderer initD3D11(HWND window, isize windowWidth, isize windowHeight, Arena* scratch);
void          d3d11present(D3D11Renderer rend, Texture tex);

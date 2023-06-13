//
// SECTION Blitter
//

struct BlitterVSInput {
    float2 pos   : POSITION;
    float2 uv    : TEXCOORD;
};

struct BlitterPSInput {
    float4 pos   : SV_POSITION;
    float2 uv    : TEXCOORD;
};

sampler blitterSampler : register(s0);

Texture2D<float4> blitterTexture : register(t0);

BlitterPSInput
blittervs(BlitterVSInput input) {
    BlitterPSInput output;
    output.pos = float4(input.pos, 0, 1);
    output.uv = input.uv;
    return output;
}

float4
blitterps(BlitterPSInput input) : SV_TARGET {
    float4 tex = blitterTexture.Sample(blitterSampler, input.uv);
    return tex;
}

//
// SECTION Renderer
//

cbuffer RendererVSConstant : register(b0) {
    float3 RendererVSConstant_meshPos;
    float3 RendererVSConstant_cameraPos;
    float RendererVSConstant_tanHalfFovX;
    float RendererVSConstant_heightOverWidth;
};

struct RendererVSInput {
    float3 pos : POSITION;
};

struct RendererPSInput {
    float4 pos: SV_POSITION;
    float3 color: COLOR;
};

RendererPSInput
renderervs(RendererVSInput input) {    
    float3 vtxWorld;
    {
        // float3 rot = rotor3fRotateV3f(mesh.orientation, vtxModel);
        float3 rot = input.pos;
        float3 trans = rot + RendererVSConstant_meshPos;
        vtxWorld = trans;
    }
    
    float3 vtxCamera;
    {
        float3 trans = vtxWorld - RendererVSConstant_cameraPos;
        // Rotor3f cameraRotationRev = rotor3fReverse(camera.orientation);
        // float3 rot = rotor3fRotateV3f(cameraRotationRev, trans);
        float3 rot = trans;
        vtxCamera = rot;
    }

    float3 vtxScreen;
    {
        float2 plane = float2(vtxCamera.x / vtxCamera.z, vtxCamera.y / vtxCamera.z);
        float tanHalfFovY = RendererVSConstant_tanHalfFovX * RendererVSConstant_heightOverWidth;
        float2 screen = float2(plane.x / RendererVSConstant_tanHalfFovX, plane.y / tanHalfFovY);
        vtxScreen = float3(screen, vtxCamera.z);
    }

    RendererPSInput output;
    output.pos = float4(vtxScreen, 1);
    output.color = vtxWorld;    
    return output;
}

float4
rendererps(RendererPSInput input) : SV_TARGET {
    float4 color = float4(input.color, 1);
    return color;
}

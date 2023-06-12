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
    float4x4 RendererVSConstant_transform;
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
    RendererPSInput output;
    float4 input4d = float4(input.pos, 1);
    output.pos = mul(RendererVSConstant_transform, input4d);
    output.color = input.pos;
    return output;
}

float4
rendererps(RendererPSInput input) : SV_TARGET {
    float4 color = float4(input.color, 1);
    return color;
}

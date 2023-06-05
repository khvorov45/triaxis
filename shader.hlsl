struct VS_INPUT {
    float2 pos   : POSITION;
    float2 uv    : TEXCOORD;
    float3 color : COLOR;
};

struct PS_INPUT {
    float4 pos   : SV_POSITION;
    float2 uv    : TEXCOORD;
    float4 color : COLOR;
};

cbuffer cbuffer0 : register(b0) {
    float4x4 uTransform;
}

sampler sampler0 : register(s0);

Texture2D<float4> texture0 : register(t0);

PS_INPUT
vs(VS_INPUT input) {
    PS_INPUT output;
    output.pos = float4(input.pos, 0, 1);
    output.uv = input.uv;
    output.color = float4(input.color, 1);
    return output;
}

float4
ps(PS_INPUT input) : SV_TARGET {
    float4 tex = texture0.Sample(sampler0, input.uv);
    return input.color * tex;
}

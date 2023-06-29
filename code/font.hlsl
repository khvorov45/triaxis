struct VSInput {
    float2 pos: POSITION;
    float2 uv: TEXCOORD;
    float4 color: COLOR;
};

struct PSInput {
    float4 pos: SV_POSITION;
    float2 uv: TEXCOORD;
    float4 color: COLOR;
};

sampler Sampler: register(s0);

Texture2D<float4> Texture: register(t0);

cbuffer ConstDims: register(b0) {
    float2 ConstDims_screen;
    float2 ConstDims_tex;
};

PSInput
vs(VSInput input) {
    PSInput output;
    output.pos = float4(input.pos.x / ConstDims_screen.x * 2 - 1, -(input.pos.y / ConstDims_screen.y * 2 - 1), 0, 1);
    output.uv = float2(input.uv.x / ConstDims_tex.x, input.uv.y / ConstDims_tex.y);
    output.color = input.color;
    return output;
}

float4
ps(PSInput input) : SV_TARGET {
    float4 tex = Texture.Sample(Sampler, input.uv);
    float4 result = float4(input.color.rgb, input.color.a * tex.a);
    return result;
}

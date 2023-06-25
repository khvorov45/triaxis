struct VSInput {
    float2 pos: POSITION;
    float2 uv: TEXCOORD;
};

struct PSInput {
    float4 pos: SV_POSITION;
    float2 uv: TEXCOORD;
};

sampler Sampler: register(s0);

Texture2D<float4> Texture: register(t0);

PSInput
vs(VSInput input) {
    PSInput output;
    output.pos = float4(input.pos, 0, 1);
    output.uv = input.uv;
    return output;
}

float4
ps(PSInput input) : SV_TARGET {
    float4 tex = Texture.Sample(Sampler, input.uv);
    return tex;
}

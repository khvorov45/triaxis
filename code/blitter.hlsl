struct blitter_VSInput {
    float2 pos: POSITION;
    float2 uv: TEXCOORD;
};

struct blitter_PSInput {
    float4 pos: SV_POSITION;
    float2 uv: TEXCOORD;
};

sampler blitter_Sampler: register(s0);

Texture2D<float4> blitter_Texture: register(t0);

blitter_PSInput
blitter_vs(blitter_VSInput input) {
    blitter_PSInput output;
    output.pos = float4(input.pos, 0, 1);
    output.uv = input.uv;
    return output;
}

float4
blitter_ps(blitter_PSInput input) : SV_TARGET {
    float4 tex = blitter_Texture.Sample(blitter_Sampler, input.uv);
    return tex;
}

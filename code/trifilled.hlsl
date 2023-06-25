struct VSInput {
    float2 pos: POSITION;
    float4 color: COLOR;
};

struct PSInput {
    float4 pos: SV_POSITION;
    float4 color: COLOR;
};

PSInput
vs(VSInput input) {
    PSInput output;
    // TODO(khvorov) Pass viewport width/height probably
    output.pos = float4(input.pos.x / 1600 * 2 - 1, -(input.pos.y / 800 * 2 - 1), 1, 1);
    output.color = input.color;
    return output;
}

float4
ps(PSInput input): SV_TARGET {
    return input.color;
}

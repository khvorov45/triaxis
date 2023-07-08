struct VSInput {
    float2 pos: POSITION;
    float4 color: COLOR;
};

struct PSInput {
    float4 pos: SV_POSITION;
    float4 color: COLOR;
};

cbuffer ConstDims: register(b0) {
    float2 ConstDims_screen;
};

PSInput
vs(VSInput input) {
    PSInput output;
    output.pos = float4(input.pos.x / ConstDims_screen.x * 2 - 1, -(input.pos.y / ConstDims_screen.y * 2 - 1), 0, 1);
    output.color = input.color;
    return output;
}

float4
ps(PSInput input): SV_TARGET {
    return input.color;
}

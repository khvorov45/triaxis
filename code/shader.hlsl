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

cbuffer ConstCamera : register(b0) {
    float4 ConstCamera_orientation;
    float3 ConstCamera_pos;
    float ConstCamera_fovx;
    float ConstCamera_fovy;
};

cbuffer ConstMesh : register(b1) {
    float4 ConstMesh_orientation;
    float3 ConstMesh_pos;
};

struct RendererVSInput {
    float3 pos : POSITION;
};

struct RendererPSInput {
    float4 pos: SV_POSITION;
    float3 color: COLOR;
};

float3
rotor3fRotateV3f(float4 r, float3 v) {
    float x = r.x * v.x + v.y * r.y + v.z * r.z;
    float y = r.x * v.y - v.x * r.y + v.z * r.w;
    float z = r.x * v.z - v.x * r.z - v.y * r.w;
    float t = v.x * r.w - v.y * r.z + v.z * r.y;

    float3 result = float3(
        r.x * x + y * r.y + z * r.z + t * r.w,
        r.x * y - x * r.y - t * r.z + z * r.w,
        r.x * z + t * r.y - x * r.z - y * r.w
    );

    return result;
}

float4
rotor3fReverse(float4 r) {
    float4 result = { r.x, -r.y, -r.z, -r.w };
    return result;
}

RendererPSInput
renderervs(RendererVSInput input) {    
    float3 vtxWorld;
    {
        float3 vtxModel = input.pos;
        float3 rot = rotor3fRotateV3f(ConstMesh_orientation, vtxModel);
        float3 trans = rot + ConstMesh_pos;
        vtxWorld = trans;
    }
    
    float3 vtxCamera;
    {
        float3 trans = vtxWorld - ConstCamera_pos;
        float4 cameraRotationRev = rotor3fReverse(ConstCamera_orientation);
        float3 rot = rotor3fRotateV3f(cameraRotationRev, trans);
        vtxCamera = rot;
    }

    float3 vtxScreen;
    {
        float2 plane = float2(vtxCamera.x / vtxCamera.z, vtxCamera.y / vtxCamera.z);
        float2 screen = float2(plane.x / ConstCamera_fovx, plane.y / ConstCamera_fovy);
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

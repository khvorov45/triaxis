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

struct VSInput {
    float3 pos: POSITION;
    float4 color: COLOR;
};

struct PSInput {
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

PSInput
vs(VSInput input) {
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
        float2 plane = float2(vtxCamera.x, vtxCamera.y);
        float2 screen = float2(plane.x / ConstCamera_fovx, plane.y / ConstCamera_fovy);
        vtxScreen = float3(screen, vtxCamera.z);
    }

    PSInput output;
    output.pos = float4(vtxScreen, vtxCamera.z);
    output.color = input.color.rgb;
    return output;
}

float4
ps(PSInput input): SV_TARGET {
    float4 color = float4(input.color, 1);
    return color;
}

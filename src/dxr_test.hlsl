// Minimal DXR RTPSO smoke test.
// 64x64 rays are traced toward a large triangle; the result buffer stores 1 for
// hits. Compiled to DXIL (lib_6_3) by GitHub Actions.

struct Payload {
    float4 color;
    uint hit;
    uint pad0;
    uint pad1;
    uint pad2;
};

struct ShadowPayload {
    uint occluded;
};

RaytracingAccelerationStructure gTlas : register(t0);
RWStructuredBuffer<uint> gOut : register(u0);

cbuffer Params : register(b0) {
    uint width;
    uint height;
    uint pad0;
    uint pad1;
};

[shader("raygeneration")]
void RayGen() {
    uint2 id = DispatchRaysIndex().xy;
    uint2 dim = DispatchRaysDimensions().xy;
    float2 uv = (float2(id) + 0.5) / float2(dim);
    float2 ndc = uv * 2.0 - 1.0;

    RayDesc ray;
    ray.Origin = float3(ndc.x * 0.8, ndc.y * 0.8, -1.0);
    ray.Direction = float3(0.0, 0.0, 1.0);
    ray.TMin = 0.001;
    ray.TMax = 100.0;

    Payload p;
    p.color = float4(0, 0, 0, 1);
    p.hit = 0;
    p.pad0 = p.pad1 = p.pad2 = 0;

    TraceRay(gTlas, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, p);

    if (id.x < width && id.y < height) {
        gOut[id.y * width + id.x] = p.hit;
    }
}

[shader("closesthit")]
void ClosestHit(inout Payload p, in float2 bary : SV_Barycentrics) {
    p.hit = 1;
    p.color = float4(bary.x, bary.y, 1.0 - bary.x - bary.y, 1.0);
}

[shader("miss")]
void Miss(inout Payload p) {
    p.hit = 0;
    p.color = float4(0.05, 0.05, 0.1, 1.0);
}

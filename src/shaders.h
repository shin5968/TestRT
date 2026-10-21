#pragma once

// HLSL sources embedded in the executable. Compiled at runtime with D3DCompile
// (DXBC, shader model 5.0). DXIL is intentionally not used.

static const char* kPathTracerHLSL = R"HLSL(
struct BVHNode {
    float3 bmin; uint leftFirst;
    float3 bmax; uint count;
};

struct Prim {
    float3 p0; float radius;
    float3 p1; uint type;
    float3 p2; uint mat;
    float3 p3; uint pad0;
    float3 n;  uint pad1;
    float3 albedo; uint pad2;
    float3 emission; uint pad3;
};

cbuffer Frame : register(b0) {
    float4 camPos;
    float4 camFwd;
    float4 camRight;
    float4 camUp;
    uint width;
    uint height;
    uint frameIndex;
    uint maxBounces;
    uint seed;
    uint primCount;
    uint lightIndex;
    uint accumulate;
    float4 lightColor;
    float tanHalfFov;
    float3 pad;
};

StructuredBuffer<BVHNode> gNodes : register(t0);
StructuredBuffer<Prim> gPrims : register(t1);
RWTexture2D<float4> gAccum : register(u0);
RWByteAddressBuffer gRayCounter : register(u1);

static uint gRng;

float rnd() {
    gRng = gRng * 747796405u + 2891336453u;
    uint w = ((gRng >> ((gRng >> 28u) + 4u)) ^ gRng) * 277803737u;
    w = (w >> 22u) ^ w;
    return (w & 0x00FFFFFFu) / 16777216.0;
}

float3 cosineHemisphere(float3 n) {
    float u1 = rnd();
    float u2 = rnd();
    float r = sqrt(u1);
    float phi = 6.28318530718 * u2;
    float3 t = abs(n.y) < 0.99 ? normalize(cross(n, float3(0, 1, 0)))
                               : normalize(cross(n, float3(1, 0, 0)));
    float3 b = cross(n, t);
    return normalize(t * cos(phi) * r + b * sin(phi) * r + n * sqrt(max(0.0, 1.0 - u1)));
}

bool refractRay(float3 I, float3 N, float eta, out float3 T) {
    float cosi = -dot(N, I);
    float k = 1.0 - eta * eta * (1.0 - cosi * cosi);
    if (k < 0.0) return false;
    T = eta * I + (eta * cosi - sqrt(k)) * N;
    return true;
}

bool aabbHit(BVHNode n, float3 ro, float3 rd, float tmin, float tmax) {
    float3 inv = 1.0 / rd;
    float3 t0 = (n.bmin - ro) * inv;
    float3 t1 = (n.bmax - ro) * inv;
    float3 tn = min(t0, t1);
    float3 tf = max(t0, t1);
    float tNear = max(max(tn.x, tn.y), tn.z);
    float tFar = min(min(tf.x, tf.y), tf.z);
    return tFar >= max(tNear, tmin) && tNear <= tmax;
}

struct Hit {
    float t;
    float3 pos;
    float3 n;
    float3 albedo;
    float3 emission;
    uint mat;
    uint type;
    uint prim;
};

bool intersectSphere(Prim p, float3 ro, float3 rd, float tmin, inout float tmax,
                     out float3 n, out float3 alb) {
    float3 oc = ro - p.p0;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - p.radius * p.radius;
    float disc = b * b - c;
    if (disc < 0.0) return false;
    float s = sqrt(disc);
    float t = -b - s;
    if (t < tmin) t = -b + s;
    if (t < tmin || t > tmax) return false;
    tmax = t;
    n = normalize(ro + rd * t - p.p0);
    alb = p.albedo;
    return true;
}

bool triHit(float3 a, float3 b, float3 c, float3 ro, float3 rd, float tmin, inout float tmax) {
    float3 e1 = b - a;
    float3 e2 = c - a;
    float3 pv = cross(rd, e2);
    float det = dot(e1, pv);
    if (abs(det) < 1e-8) return false;
    float inv = 1.0 / det;
    float3 tv = ro - a;
    float u = dot(tv, pv) * inv;
    if (u < 0.0 || u > 1.0) return false;
    float3 qv = cross(tv, e1);
    float v = dot(rd, qv) * inv;
    if (v < 0.0 || u + v > 1.0) return false;
    float t = dot(e2, qv) * inv;
    if (t < tmin || t > tmax) return false;
    tmax = t;
    return true;
}

bool intersectQuad(Prim p, float3 ro, float3 rd, float tmin, inout float tmax,
                   out float3 n, out float3 alb) {
    n = p.n;
    alb = p.albedo;
    bool hit = false;
    float t1 = tmax;
    if (triHit(p.p0, p.p1, p.p2, ro, rd, tmin, t1)) hit = true;
    if (triHit(p.p0, p.p2, p.p3, ro, rd, tmin, t1)) hit = true;
    if (hit) tmax = t1;
    return hit;
}

bool trace(float3 ro, float3 rd, float tmin, float tmax, out Hit hit) {
    hit.t = tmax;
    hit.mat = 0;
    hit.type = 0;
    hit.prim = 0;
    bool found = false;

    uint stack[32];
    uint sp = 0;
    stack[sp++] = 0;

    [loop] while (sp > 0) {
        uint ni = stack[--sp];
        BVHNode node = gNodes[ni];
        if (!aabbHit(node, ro, rd, tmin, hit.t)) continue;

        if (node.count > 0) {
            [loop] for (uint i = 0; i < node.count; ++i) {
                uint pi = node.leftFirst + i;
                Prim p = gPrims[pi];
                float3 n, alb;
                float t = hit.t;
                bool h;
                if (p.type == 0) h = intersectSphere(p, ro, rd, tmin, t, n, alb);
                else h = intersectQuad(p, ro, rd, tmin, t, n, alb);
                if (h) {
                    found = true;
                    hit.t = t;
                    hit.pos = ro + rd * t;
                    hit.n = n;
                    hit.albedo = alb;
                    hit.emission = p.emission;
                    hit.mat = p.mat;
                    hit.type = p.type;
                    hit.prim = pi;
                }
            }
        } else {
            if (sp + 2 <= 32) {
                stack[sp++] = node.leftFirst;
                stack[sp++] = node.leftFirst + 1;
            }
        }
    }
    return found;
}

bool occluded(float3 ro, float3 rd, float tmax) {
    uint stack[32];
    uint sp = 0;
    stack[sp++] = 0;
    [loop] while (sp > 0) {
        uint ni = stack[--sp];
        BVHNode node = gNodes[ni];
        if (!aabbHit(node, ro, rd, 1e-3, tmax)) continue;
        if (node.count > 0) {
            [loop] for (uint i = 0; i < node.count; ++i) {
                uint pi = node.leftFirst + i;
                Prim p = gPrims[pi];
                float3 n, alb;
                float t = tmax;
                bool h;
                if (p.type == 0) h = intersectSphere(p, ro, rd, 1e-3, t, n, alb);
                else h = intersectQuad(p, ro, rd, 1e-3, t, n, alb);
                if (h) return true;
            }
        } else {
            if (sp + 2 <= 32) {
                stack[sp++] = node.leftFirst;
                stack[sp++] = node.leftFirst + 1;
            }
        }
    }
    return false;
}

float3 sampleLight(float3 pos, float3 n, float3 albedo, inout uint rayCount) {
    Prim lp = gPrims[lightIndex];
    float u = rnd();
    float v = rnd();
    float3 e1 = lp.p1 - lp.p0;
    float3 e2 = lp.p3 - lp.p0;
    float3 lpPos = lp.p0 + e1 * u + e2 * v;
    float area = length(cross(e1, e2));
    float3 toL = lpPos - pos;
    float dist = length(toL);
    if (dist < 1e-4) return float3(0, 0, 0);
    float3 L = toL / dist;
    float cosS = saturate(dot(n, L));
    float3 ln = normalize(lp.n);
    float cosL = saturate(dot(ln, -L));
    if (cosS <= 0.0 || cosL <= 0.0) return float3(0, 0, 0);
    rayCount++;
    if (occluded(pos + n * 1e-3, L, dist - 1e-3)) return float3(0, 0, 0);
    float3 brdf = albedo * (1.0 / 3.14159265359);
    return brdf * lp.emission * (cosS * cosL / (dist * dist)) * area;
}

float3 tracePath(float3 ro, float3 rd, inout uint rayCount) {
    float3 radiance = float3(0, 0, 0);
    float3 throughput = float3(1, 1, 1);
    bool specular = true;

    [loop] for (uint bounce = 0; bounce <= maxBounces; ++bounce) {
        Hit h;
        rayCount++;
        if (!trace(ro, rd, 1e-3, 1e9, h)) {
            radiance += throughput * float3(0.02, 0.03, 0.05);
            break;
        }
        if (h.mat == 1) {
            if (bounce == 0 || specular) radiance += throughput * h.emission;
            break;
        }

        float3 n = dot(h.n, rd) < 0.0 ? h.n : -h.n;

        if (h.mat == 0) {
            specular = false;
            radiance += throughput * sampleLight(h.pos, n, h.albedo, rayCount);
            throughput *= h.albedo;
            rd = cosineHemisphere(n);
            ro = h.pos + n * 1e-3;
        } else if (h.mat == 2) {
            specular = true;
            throughput *= h.albedo;
            rd = reflect(rd, n);
            ro = h.pos + n * 1e-3;
        } else {
            specular = true;
            float3 I = rd;
            // Geometric normal, not the shading normal flipped toward the ray.
            bool entering = dot(I, h.n) < 0.0;
            float3 N = entering ? h.n : -h.n;
            float eta = entering ? (1.0 / 1.5) : 1.5;
            float cosi = saturate(-dot(I, N));
            float F = 0.04 + 0.96 * pow(1.0 - cosi, 5.0);
            float3 dir;
            if (rnd() < F) {
                dir = reflect(I, N);
            } else {
                float3 t;
                if (refractRay(I, N, eta, t)) dir = t;
                else dir = reflect(I, N);
            }
            rd = normalize(dir);
            ro = h.pos + rd * 1e-3;
        }

        if (max(throughput.x, max(throughput.y, throughput.z)) < 0.01) break;
    }
    return min(radiance, float3(32, 32, 32));
}

groupshared uint gGroupRays;

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID) {
    if (gtid.x == 0 && gtid.y == 0) gGroupRays = 0;
    GroupMemoryBarrierWithGroupSync();

    uint rayCount = 0;
    if (tid.x < width && tid.y < height) {
        uint px = tid.x;
        uint py = tid.y;
        uint h = px * 1973u ^ py * 9277u ^ frameIndex * 26699u ^ seed * 3331u;
        gRng = h * 747796405u + 2891336453u;

        float aspect = (float)width / (float)height;
        float2 jitter = float2(rnd(), rnd());
        float2 ndc = float2(2.0 * (px + jitter.x) / width - 1.0,
                            1.0 - 2.0 * (py + jitter.y) / height);
        float3 dir = normalize(camFwd.xyz
                               + ndc.x * aspect * tanHalfFov * camRight.xyz
                               + ndc.y * tanHalfFov * camUp.xyz);

        float3 c = tracePath(camPos.xyz, dir, rayCount);

        float4 prev = ((accumulate != 0) && (frameIndex > 0)) ? gAccum[int2(px, py)]
                                                              : float4(0, 0, 0, 0);
        gAccum[int2(px, py)] = prev + float4(c, 1.0);
    }

    InterlockedAdd(gGroupRays, rayCount);
    GroupMemoryBarrierWithGroupSync();
    if (gtid.x == 0 && gtid.y == 0 && gGroupRays > 0)
        gRayCounter.InterlockedAdd(0, gGroupRays);
}
)HLSL";

static const char* kCompositeHLSL = R"HLSL(
cbuffer Composite : register(b0) {
    float invSamples;
    uint showOverlay;
    float2 pad;
};

Texture2D<float4> gAccumSRV : register(t0);
Texture2D<float4> gOverlay : register(t1);
SamplerState gPoint : register(s0);

struct VSOut {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID) {
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    o.uv = float2(uv.x, 1.0 - uv.y);
    return o;
}

float3 aces(float3 x) {
    x *= 0.6;
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

float4 PSMain(VSOut i) : SV_TARGET {
    float3 c = gAccumSRV.SampleLevel(gPoint, i.uv, 0).rgb * invSamples;
    c = aces(c);
    c = pow(saturate(c), 1.0 / 2.2);
    if (showOverlay != 0) {
        float4 o = gOverlay.SampleLevel(gPoint, i.uv, 0);
        c = lerp(c, o.rgb, o.a);
    }
    return float4(c, 1.0);
}
)HLSL";

#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Must match shaders.h (HLSL) exactly.
struct BVHNode {
    float bmin[3];
    uint32_t leftFirst;
    float bmax[3];
    uint32_t count;
};

struct PrimGPU {
    float p0[3];
    float radius;      // sphere radius, quad unused
    float p1[3];
    uint32_t type;     // 0 = sphere, 1 = quad
    float p2[3];
    uint32_t mat;      // 0 diffuse, 1 emissive, 2 metal, 3 dielectric
    float p3[3];
    uint32_t pad0;
    float n[3];
    uint32_t pad1;
    float albedo[3];
    uint32_t pad2;
    float emission[3];
    uint32_t pad3;
};
static_assert(sizeof(PrimGPU) == 112, "PrimGPU layout must match HLSL");
static_assert(sizeof(BVHNode) == 32, "BVHNode layout must match HLSL");

struct Scene {
    std::vector<PrimGPU> prims;
    std::vector<BVHNode> nodes;
    uint32_t lightIndex = 0;
    uint64_t hash = 0;
    double buildMs = 0.0;

    void build();
    size_t primitiveCount() const { return prims.size(); }
    size_t nodeCount() const { return nodes.size(); }
};

void buildCornellScene(Scene& scene);

#include "scene.h"

#include "util.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

struct Vec3 {
    float x, y, z;
};

Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float length(const Vec3& a) { return std::sqrt(dot(a, a)); }
Vec3 normalize(const Vec3& a) {
    float l = length(a);
    if (l <= 1e-8f) return {0, 0, 0};
    return {a.x / l, a.y / l, a.z / l};
}

void set3(float* dst, float x, float y, float z) {
    dst[0] = x;
    dst[1] = y;
    dst[2] = z;
}

PrimGPU makeSphere(Vec3 c, float r, Vec3 albedo, uint32_t mat) {
    PrimGPU p{};
    set3(p.p0, c.x, c.y, c.z);
    p.radius = r;
    p.type = 0;
    p.mat = mat;
    set3(p.albedo, albedo.x, albedo.y, albedo.z);
    return p;
}

PrimGPU makeQuad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 albedo, Vec3 emission, uint32_t mat) {
    PrimGPU p{};
    set3(p.p0, a.x, a.y, a.z);
    set3(p.p1, b.x, b.y, b.z);
    set3(p.p2, c.x, c.y, c.z);
    set3(p.p3, d.x, d.y, d.z);
    Vec3 n = normalize(cross(sub(b, a), sub(c, a)));
    set3(p.n, n.x, n.y, n.z);
    set3(p.albedo, albedo.x, albedo.y, albedo.z);
    set3(p.emission, emission.x, emission.y, emission.z);
    p.type = 1;
    p.mat = mat;
    return p;
}

void primBounds(const PrimGPU& p, float* bmin, float* bmax) {
    if (p.type == 1) {
        bmin[0] = std::min(std::min(p.p0[0], p.p1[0]), std::min(p.p2[0], p.p3[0]));
        bmin[1] = std::min(std::min(p.p0[1], p.p1[1]), std::min(p.p2[1], p.p3[1]));
        bmin[2] = std::min(std::min(p.p0[2], p.p1[2]), std::min(p.p2[2], p.p3[2]));
        bmax[0] = std::max(std::max(p.p0[0], p.p1[0]), std::max(p.p2[0], p.p3[0]));
        bmax[1] = std::max(std::max(p.p0[1], p.p1[1]), std::max(p.p2[1], p.p3[1]));
        bmax[2] = std::max(std::max(p.p0[2], p.p1[2]), std::max(p.p2[2], p.p3[2]));
    } else {
        for (int i = 0; i < 3; ++i) {
            bmin[i] = p.p0[i] - p.radius;
            bmax[i] = p.p0[i] + p.radius;
        }
    }
}

float centroidAxis(const PrimGPU& p, int axis) {
    if (p.type == 1) {
        return (p.p0[axis] + p.p1[axis] + p.p2[axis] + p.p3[axis]) * 0.25f;
    }
    return p.p0[axis];
}

struct Builder {
    std::vector<PrimGPU> work;
    std::vector<BVHNode> nodes;

    uint32_t allocNode() {
        nodes.emplace_back();
        return (uint32_t)nodes.size() - 1;
    }

    void buildRange(uint32_t nodeIndex, int start, int count) {
        float bmin[3] = {1e30f, 1e30f, 1e30f};
        float bmax[3] = {-1e30f, -1e30f, -1e30f};
        float cmin[3] = {1e30f, 1e30f, 1e30f};
        float cmax[3] = {-1e30f, -1e30f, -1e30f};
        for (int i = start; i < start + count; ++i) {
            float pbmin[3], pbmax[3];
            primBounds(work[i], pbmin, pbmax);
            for (int a = 0; a < 3; ++a) {
                bmin[a] = std::min(bmin[a], pbmin[a]);
                bmax[a] = std::max(bmax[a], pbmax[a]);
                float c = centroidAxis(work[i], a);
                cmin[a] = std::min(cmin[a], c);
                cmax[a] = std::max(cmax[a], c);
            }
        }
        for (int a = 0; a < 3; ++a) {
            nodes[nodeIndex].bmin[a] = bmin[a];
            nodes[nodeIndex].bmax[a] = bmax[a];
        }

        if (count <= 4) {
            nodes[nodeIndex].leftFirst = (uint32_t)start;
            nodes[nodeIndex].count = (uint32_t)count;
            return;
        }

        int axis = 0;
        float best = cmax[0] - cmin[0];
        for (int a = 1; a < 3; ++a) {
            float e = cmax[a] - cmin[a];
            if (e > best) {
                best = e;
                axis = a;
            }
        }
        int mid = start + count / 2;
        std::nth_element(work.begin() + start, work.begin() + mid, work.begin() + start + count,
                         [axis](const PrimGPU& a, const PrimGPU& b) {
                             float ca = centroidAxis(a, axis);
                             float cb = centroidAxis(b, axis);
                             if (ca != cb) return ca < cb;
                             return centroidAxis(a, 0) < centroidAxis(b, 0);
                         });

        uint32_t left = allocNode();
        uint32_t right = allocNode();
        nodes[nodeIndex].leftFirst = left;
        nodes[nodeIndex].count = 0;
        buildRange(left, start, mid - start);
        buildRange(right, mid, start + count - mid);
    }
};

}  // namespace

void Scene::build() { buildCornellScene(*this); }

void buildCornellScene(Scene& scene) {
    scene.prims.clear();
    scene.nodes.clear();

    const Vec3 white{0.73f, 0.73f, 0.73f};
    const Vec3 red{0.65f, 0.12f, 0.10f};
    const Vec3 green{0.12f, 0.55f, 0.15f};
    const Vec3 black{0.0f, 0.0f, 0.0f};
    const Vec3 lightColor{16.0f, 16.0f, 16.0f};

    // Cornell box: x[-1,1], y[0,2], z[0,2]; front (z=0) is open.
    scene.prims.push_back(makeQuad({-1, 0, 0}, {1, 0, 0}, {1, 0, 2}, {-1, 0, 2}, white, black, 0)); // floor
    scene.prims.push_back(makeQuad({-1, 2, 0}, {1, 2, 0}, {1, 2, 2}, {-1, 2, 2}, white, black, 0)); // ceiling
    scene.prims.push_back(makeQuad({-1, 0, 2}, {1, 0, 2}, {1, 2, 2}, {-1, 2, 2}, white, black, 0)); // back
    scene.prims.push_back(makeQuad({-1, 0, 0}, {-1, 0, 2}, {-1, 2, 2}, {-1, 2, 0}, red, black, 0)); // left
    scene.prims.push_back(makeQuad({1, 0, 0}, {1, 0, 2}, {1, 2, 2}, {1, 2, 0}, green, black, 0));   // right

    // Ceiling light (slightly below the ceiling).
    scene.prims.push_back(makeQuad({-0.35f, 1.995f, 0.75f}, {0.35f, 1.995f, 0.75f},
                                   {0.35f, 1.995f, 1.25f}, {-0.35f, 1.995f, 1.25f},
                                   black, lightColor, 1));

    scene.prims.push_back(makeSphere({-0.48f, 0.48f, 1.35f}, 0.48f, {0.78f, 0.78f, 0.78f}, 0));
    scene.prims.push_back(makeSphere({0.45f, 0.43f, 0.90f}, 0.43f, {0.92f, 0.75f, 0.38f}, 2));
    scene.prims.push_back(makeSphere({0.12f, 0.27f, 0.38f}, 0.27f, {0.95f, 0.95f, 0.95f}, 3));

    uint64_t t0 = GetTickCount64();

    Builder b;
    b.work = scene.prims;
    b.nodes.reserve(scene.prims.size() * 2 + 1);
    b.allocNode();
    b.buildRange(0, 0, (int)b.work.size());

    scene.prims = std::move(b.work);
    scene.nodes = std::move(b.nodes);

    for (size_t i = 0; i < scene.prims.size(); ++i) {
        if (scene.prims[i].mat == 1 && scene.prims[i].emission[0] > 0.0f) {
            scene.lightIndex = (uint32_t)i;
            break;
        }
    }

    uint64_t h = fnv1a(scene.prims.data(), scene.prims.size() * sizeof(PrimGPU));
    h = fnv1a(scene.nodes.data(), scene.nodes.size() * sizeof(BVHNode), h);
    scene.hash = h;
    scene.buildMs = (double)(GetTickCount64() - t0);
    logf("scene: %zu prims, %zu nodes, light=%u, hash=%016llX, build=%.1fms",
         scene.prims.size(), scene.nodes.size(), scene.lightIndex,
         (unsigned long long)scene.hash, scene.buildMs);
}

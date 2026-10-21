#pragma once

#include "d3d12_ctx.h"
#include "scene.h"

#include <string>
#include <vector>

// Must match shaders.h Frame cbuffer (28 x 32-bit values).
struct alignas(16) FrameCB {
    float camPos[4];
    float camFwd[4];
    float camRight[4];
    float camUp[4];
    uint32_t width;
    uint32_t height;
    uint32_t frameIndex;
    uint32_t maxBounces;
    uint32_t seed;
    uint32_t primCount;
    uint32_t lightIndex;
    uint32_t accumulate;
    float lightColor[4];
    float tanHalfFov;
    float pad[3];
};
static_assert(sizeof(FrameCB) == 128, "FrameCB must match HLSL cbuffer");

struct BenchTiming {
    std::vector<double> gpuMs;
    uint64_t rayCount = 0;
    bool gpuTimingAvailable = false;
    uint64_t timestampFrequency = 0;
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    bool initDevice(D3D12Context& ctx, D3D12Api* api, const Scene& scene);
    bool resize(D3D12Context& ctx, int w, int h);
    void destroy();

    bool renderFrame(D3D12Context& ctx, const FrameCB& cb,
                     const uint8_t* overlayBGRA, bool overlayDirty,
                     bool showOverlay, bool copyBackbuffer, bool skipCompute = false);

    bool saveScreenshotBMP(const std::string& path);
    bool screenshotPending() const { return screenshotPending_; }

    bool benchInit(D3D12Context& ctx, uint32_t maxFrames);
    void benchResetCounter(D3D12Context& ctx);
    void benchBeginFrame(D3D12Context& ctx, uint32_t frame);
    void benchEndFrame(D3D12Context& ctx, uint32_t frame);
    bool benchFinish(D3D12Context& ctx, uint32_t frames, BenchTiming& timing);
    void benchDestroy();

    const std::string& shaderHash() const { return shaderHash_; }
    double shaderCompileMs() const { return shaderCompileMs_; }

private:
    bool compileShaders(D3D12Api* api);
    bool createPSOs(D3D12Context& ctx);
    bool createSceneBuffers(D3D12Context& ctx, const Scene& scene);
    bool createCounter(D3D12Context& ctx);

    D3D12Api* api_ = nullptr;
    ComPtr<ID3D12RootSignature> computeRoot_;
    ComPtr<ID3D12RootSignature> graphicsRoot_;
    ComPtr<ID3D12PipelineState> computePSO_;
    ComPtr<ID3D12PipelineState> graphicsPSO_;

    ComPtr<ID3DBlob> csBlob_;
    ComPtr<ID3DBlob> vsBlob_;
    ComPtr<ID3DBlob> psBlob_;
    std::string shaderHash_;
    double shaderCompileMs_ = 0.0;

    ComPtr<ID3D12Resource> nodeBuf_;
    ComPtr<ID3D12Resource> primBuf_;
    ComPtr<ID3D12Resource> counterBuf_;
    ComPtr<ID3D12Resource> counterReadback_;
    D3D12_RESOURCE_STATES counterState_ = D3D12_RESOURCE_STATE_COMMON;

    ComPtr<ID3D12Resource> accumTex_;
    ComPtr<ID3D12Resource> overlayTex_;
    ComPtr<ID3D12Resource> overlayUpload_;
    ComPtr<ID3D12Resource> shotReadback_;
    D3D12_RESOURCE_STATES accumState_ = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES overlayState_ = D3D12_RESOURCE_STATE_COMMON;

    UINT width_ = 0;
    UINT height_ = 0;
    UINT overlayRowPitch_ = 0;
    UINT shotRowPitch_ = 0;
    bool overlayInitialized_ = false;
    bool screenshotPending_ = false;
    int screenshotFrameAge_ = 0;

    // benchmarks
    ComPtr<ID3D12QueryHeap> queryHeap_;
    ComPtr<ID3D12Resource> queryReadback_;
    uint32_t maxBenchFrames_ = 0;
    bool benchQueriesOk_ = false;
};

#include "renderer.h"

#include "shaders.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

UINT align256(UINT v) { return (v + 255u) & ~255u; }

bool compileShader(D3D12Api* api, const char* src, const char* entry, const char* target,
                   ComPtr<ID3DBlob>& out) {
    if (!api->d3dCompile) {
        logf("D3DCompile unavailable; cannot compile %s", entry);
        return false;
    }
    ComPtr<ID3DBlob> err;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    HRESULT hr = api->d3dCompile(src, strlen(src), "rtprobe.hlsl", nullptr, nullptr,
                                 entry, target, flags, 0, out.put(), err.put());
    if (FAILED(hr)) {
        logf("D3DCompile(%s, %s) failed: %s", entry, target, hrStr(hr).c_str());
        if (err && err->GetBufferSize()) {
            std::string msg((const char*)err->GetBufferPointer(), err->GetBufferSize());
            logf("compiler: %s", msg.c_str());
        }
        return false;
    }
    return true;
}

bool createRootSignature(D3D12Api* api, ID3D12Device* device,
                         const D3D12_ROOT_SIGNATURE_DESC& desc,
                         ID3D12RootSignature** out) {
    if (!api || (!api->serializeRootSignature && !api->serializeVersionedRootSignature)) {
        logf("root signature serialization exports unavailable");
        return false;
    }
    ComPtr<ID3DBlob> blob, err;
    HRESULT hr;
    if (api->serializeRootSignature) {
        hr = api->serializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0,
                                         blob.put(), err.put());
    } else {
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC vdesc{};
        vdesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
        vdesc.Desc_1_0 = desc;
        hr = api->serializeVersionedRootSignature(&vdesc, blob.put(), err.put());
    }
    if (FAILED(hr)) {
        logf("D3D12SerializeRootSignature failed: %s", hrStr(hr).c_str());
        if (err && err->GetBufferSize()) {
            logf("root sig error: %s", std::string((const char*)err->GetBufferPointer(),
                                                   err->GetBufferSize()).c_str());
        }
        return false;
    }
    hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                     IID_ID3D12RootSignature, (void**)out);
    if (FAILED(hr)) {
        logf("CreateRootSignature failed: %s", hrStr(hr).c_str());
        return false;
    }
    return true;
}

bool createTexture(ID3D12Device* device, UINT w, UINT h, DXGI_FORMAT format,
                   D3D12_RESOURCE_FLAGS flags, ID3D12Resource** out) {
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = w;
    rd.Height = h;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = format;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = flags;

    HRESULT hr = device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                 D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                 IID_ID3D12Resource, (void**)out);
    if (FAILED(hr)) {
        logf("CreateCommittedResource(texture %ux%u fmt=%d) failed: %s",
             w, h, (int)format, hrStr(hr).c_str());
        return false;
    }
    return true;
}

bool createStructuredUpload(ID3D12Device* device, const void* data, UINT64 bytes,
                            UINT /*stride*/, ID3D12Resource** out) {
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = bytes;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Flags = D3D12_RESOURCE_FLAG_NONE;

    HRESULT hr = device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                 D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                 IID_ID3D12Resource, (void**)out);
    if (FAILED(hr)) {
        logf("structured upload buffer failed: %s", hrStr(hr).c_str());
        return false;
    }
    void* mapped = nullptr;
    hr = (*out)->Map(0, nullptr, &mapped);
    if (FAILED(hr)) {
        logf("Map(upload) failed: %s", hrStr(hr).c_str());
        return false;
    }
    memcpy(mapped, data, (size_t)bytes);
    (*out)->Unmap(0, nullptr);
    return true;
}

}  // namespace

Renderer::~Renderer() { destroy(); }

bool Renderer::initDevice(D3D12Context& ctx, D3D12Api* api, const Scene& scene) {
    api_ = api;

    uint64_t t0 = GetTickCount64();
    if (!compileShaders(api)) return false;
    shaderCompileMs_ = (double)(GetTickCount64() - t0);
    logf("shader compile: %.1f ms", shaderCompileMs_);

    shaderHash_ = strf("%016llX", (unsigned long long)(
        fnv1aStr(std::string(kPathTracerHLSL)) ^
        (fnv1aStr(std::string(kCompositeHLSL)) * 1099511628211ull)));

    if (!createPSOs(ctx)) return false;
    if (!createSceneBuffers(ctx, scene)) return false;
    if (!createCounter(ctx)) return false;
    return true;
}

bool Renderer::compileShaders(D3D12Api* api) {
    if (!compileShader(api, kPathTracerHLSL, "main", "cs_5_0", csBlob_)) return false;
    if (!compileShader(api, kCompositeHLSL, "VSMain", "vs_5_0", vsBlob_)) return false;
    if (!compileShader(api, kCompositeHLSL, "PSMain", "ps_5_0", psBlob_)) return false;
    return true;
}

bool Renderer::createPSOs(D3D12Context& ctx) {
    {
        D3D12_DESCRIPTOR_RANGE uavRange{};
        uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors = 1;
        uavRange.BaseShaderRegister = 0;
        uavRange.RegisterSpace = 0;
        uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER params[5]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.RegisterSpace = 0;
        params[0].Constants.Num32BitValues = 32;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[1].Descriptor.ShaderRegister = 0;
        params[1].Descriptor.RegisterSpace = 0;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[2].Descriptor.ShaderRegister = 1;
        params[2].Descriptor.RegisterSpace = 0;
        params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[3].DescriptorTable.NumDescriptorRanges = 1;
        params[3].DescriptorTable.pDescriptorRanges = &uavRange;
        params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[4].Descriptor.ShaderRegister = 1;
        params[4].Descriptor.RegisterSpace = 0;
        params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC desc{};
        desc.NumParameters = 5;
        desc.pParameters = params;
        desc.NumStaticSamplers = 0;
        desc.pStaticSamplers = nullptr;
        desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        if (!createRootSignature(api_, ctx.device.get(), desc, computeRoot_.put())) return false;

        D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = computeRoot_.get();
        pso.CS.pShaderBytecode = csBlob_->GetBufferPointer();
        pso.CS.BytecodeLength = csBlob_->GetBufferSize();
        pso.NodeMask = 0;
        pso.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        HRESULT hr = ctx.device->CreateComputePipelineState(&pso, IID_ID3D12PipelineState,
                                                            (void**)computePSO_.put());
        if (FAILED(hr)) {
            logf("CreateComputePipelineState failed: %s", hrStr(hr).c_str());
            return false;
        }
        logf("compute PSO created");
    }

    {
        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = 2;
        srvRange.BaseShaderRegister = 0;
        srvRange.RegisterSpace = 0;
        srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.RegisterSpace = 0;
        params[0].Constants.Num32BitValues = 4;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges = &srvRange;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MipLODBias = 0;
        sampler.MaxAnisotropy = 1;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 0;
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC desc{};
        desc.NumParameters = 2;
        desc.pParameters = params;
        desc.NumStaticSamplers = 1;
        desc.pStaticSamplers = &sampler;
        desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        if (!createRootSignature(api_, ctx.device.get(), desc, graphicsRoot_.put())) return false;

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = graphicsRoot_.get();
        pso.VS.pShaderBytecode = vsBlob_->GetBufferPointer();
        pso.VS.BytecodeLength = vsBlob_->GetBufferSize();
        pso.PS.pShaderBytecode = psBlob_->GetBufferPointer();
        pso.PS.BytecodeLength = psBlob_->GetBufferSize();

        D3D12_BLEND_DESC blend{};
        blend.AlphaToCoverageEnable = FALSE;
        blend.IndependentBlendEnable = FALSE;
        blend.RenderTarget[0].BlendEnable = FALSE;
        blend.RenderTarget[0].LogicOpEnable = FALSE;
        blend.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
        blend.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
        blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
        blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pso.BlendState = blend;
        pso.SampleMask = UINT_MAX;

        D3D12_RASTERIZER_DESC raster{};
        raster.FillMode = D3D12_FILL_MODE_SOLID;
        raster.CullMode = D3D12_CULL_MODE_NONE;
        raster.FrontCounterClockwise = FALSE;
        raster.DepthBias = 0;
        raster.DepthBiasClamp = 0.0f;
        raster.SlopeScaledDepthBias = 0.0f;
        raster.DepthClipEnable = TRUE;
        raster.MultisampleEnable = FALSE;
        raster.AntialiasedLineEnable = FALSE;
        raster.ForcedSampleCount = 0;
        raster.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
        pso.RasterizerState = raster;

        D3D12_DEPTH_STENCIL_DESC ds{};
        ds.DepthEnable = FALSE;
        ds.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        ds.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        ds.StencilEnable = FALSE;
        pso.DepthStencilState = ds;
        pso.InputLayout = {nullptr, 0};
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets = 1;
        pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pso.DSVFormat = DXGI_FORMAT_UNKNOWN;
        pso.SampleDesc.Count = 1;
        pso.SampleDesc.Quality = 0;
        pso.NodeMask = 0;
        pso.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

        HRESULT hr = ctx.device->CreateGraphicsPipelineState(&pso, IID_ID3D12PipelineState,
                                                             (void**)graphicsPSO_.put());
        if (FAILED(hr)) {
            logf("CreateGraphicsPipelineState failed: %s", hrStr(hr).c_str());
            return false;
        }
        logf("graphics PSO created");
    }
    return true;
}

bool Renderer::createSceneBuffers(D3D12Context& ctx, const Scene& scene) {
    if (scene.nodes.empty() || scene.prims.empty()) return false;
    if (!createStructuredUpload(ctx.device.get(), scene.nodes.data(),
                                scene.nodes.size() * sizeof(BVHNode), sizeof(BVHNode),
                                nodeBuf_.put()))
        return false;
    if (!createStructuredUpload(ctx.device.get(), scene.prims.data(),
                                scene.prims.size() * sizeof(PrimGPU), sizeof(PrimGPU),
                                primBuf_.put()))
        return false;
    logf("scene buffers: nodes=%zu bytes, prims=%zu bytes",
         scene.nodes.size() * sizeof(BVHNode), scene.prims.size() * sizeof(PrimGPU));
    return true;
}

bool Renderer::createCounter(D3D12Context& ctx) {
    if (!createBuffer(ctx.device.get(), 4, D3D12_HEAP_TYPE_DEFAULT,
                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, counterBuf_.put(),
                      D3D12_RESOURCE_STATE_COMMON))
        return false;
    if (!createBuffer(ctx.device.get(), 4, D3D12_HEAP_TYPE_READBACK,
                      D3D12_RESOURCE_FLAG_NONE, counterReadback_.put(),
                      D3D12_RESOURCE_STATE_COPY_DEST))
        return false;

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = DXGI_FORMAT_R32_UINT;
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.FirstElement = 0;
    uav.Buffer.NumElements = 1;
    uav.Buffer.StructureByteStride = 0;
    uav.Buffer.CounterOffsetInBytes = 0;
    ctx.device->CreateUnorderedAccessView(counterBuf_.get(), nullptr, &uav, ctx.srvCpu(3));

    ctx.waitForGpu();
    ctx.allocators[0]->Reset();
    ctx.list->Reset(ctx.allocators[0].get(), nullptr);
    transition(ctx.list.get(), counterBuf_.get(), D3D12_RESOURCE_STATE_COMMON,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    UINT zeros[4] = {0, 0, 0, 0};
    ctx.list->ClearUnorderedAccessViewUint(ctx.srvGpu(3), ctx.srvCpu(3), counterBuf_.get(),
                                           zeros, 0, nullptr);
    ctx.list->Close();
    ID3D12CommandList* lists[] = {ctx.list.get()};
    ctx.queue->ExecuteCommandLists(1, lists);
    ctx.waitForGpu();
    counterState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    logf("ray counter initialized");
    return true;
}

bool Renderer::resize(D3D12Context& ctx, int w, int h) {
    if (w <= 0 || h <= 0) return false;
    ctx.waitForGpu();

    accumTex_.reset();
    overlayTex_.reset();
    overlayUpload_.reset();
    shotReadback_.reset();

    width_ = (UINT)w;
    height_ = (UINT)h;
    accumState_ = D3D12_RESOURCE_STATE_COMMON;
    overlayState_ = D3D12_RESOURCE_STATE_COMMON;
    overlayInitialized_ = false;
    screenshotPending_ = false;

    if (!createTexture(ctx.device.get(), width_, height_, DXGI_FORMAT_R32G32B32A32_FLOAT,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, accumTex_.put()))
        return false;
    if (!createTexture(ctx.device.get(), width_, height_, DXGI_FORMAT_B8G8R8A8_UNORM,
                       D3D12_RESOURCE_FLAG_NONE, overlayTex_.put()))
        return false;

    overlayRowPitch_ = align256(width_ * 4);
    shotRowPitch_ = overlayRowPitch_;

    if (!createBuffer(ctx.device.get(), (UINT64)overlayRowPitch_ * height_,
                      D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, overlayUpload_.put()))
        return false;
    if (!createBuffer(ctx.device.get(), (UINT64)shotRowPitch_ * height_,
                      D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, shotReadback_.put(),
                      D3D12_RESOURCE_STATE_COPY_DEST))
        return false;

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav.Texture2D.MipSlice = 0;
    ctx.device->CreateUnorderedAccessView(accumTex_.get(), nullptr, &uav, ctx.srvCpu(0));

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    srv.Texture2D.MostDetailedMip = 0;
    ctx.device->CreateShaderResourceView(accumTex_.get(), &srv, ctx.srvCpu(1));

    srv.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    ctx.device->CreateShaderResourceView(overlayTex_.get(), &srv, ctx.srvCpu(2));

    logf("renderer resize: %dx%d accum/overlay ready", w, h);
    return true;
}

bool Renderer::renderFrame(D3D12Context& ctx, const FrameCB& cb,
                           const uint8_t* overlayBGRA, bool overlayDirty,
                           bool showOverlay, bool copyBackbuffer, bool skipCompute) {
    if (!ctx.list || !ctx.hasSwapchain || !computePSO_ || !graphicsPSO_) return false;
    ID3D12GraphicsCommandList* list = ctx.list.get();
    ID3D12Resource* bb = ctx.backbuffers[ctx.backbufferIndex].get();

    static bool firstFrameLogged = false;
    if (!firstFrameLogged) {
        firstFrameLogged = true;
        logf("first renderFrame: %ux%u backbuffer=%u overlayInit=%d showOverlay=%d accumulate=%u skipCompute=%d",
             width_, height_, ctx.backbufferIndex, (int)overlayInitialized_, (int)showOverlay,
             cb.accumulate, (int)skipCompute);
    }

    ID3D12DescriptorHeap* heap = ctx.srvHeap.get();
    list->SetDescriptorHeaps(1, &heap);

    if (skipCompute) {
        // The overlay contains a full-screen image produced elsewhere (Vulkan RT).
        if (accumState_ != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            transition(list, accumTex_.get(), accumState_,
                       D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            accumState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }
    } else {
        if (accumState_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            transition(list, accumTex_.get(), accumState_,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            accumState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }

        list->SetComputeRootSignature(computeRoot_.get());
        list->SetPipelineState(computePSO_.get());
        list->SetComputeRoot32BitConstants(0, 32, &cb, 0);
        list->SetComputeRootShaderResourceView(1, nodeBuf_->GetGPUVirtualAddress());
        list->SetComputeRootShaderResourceView(2, primBuf_->GetGPUVirtualAddress());
        list->SetComputeRootDescriptorTable(3, ctx.srvGpu(0));
        list->SetComputeRootUnorderedAccessView(4, counterBuf_->GetGPUVirtualAddress());
        list->Dispatch((width_ + 7) / 8, (height_ + 7) / 8, 1);

        {
            D3D12_RESOURCE_BARRIER b{};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            b.UAV.pResource = accumTex_.get();
            list->ResourceBarrier(1, &b);
        }
        transition(list, accumTex_.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        accumState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    // Upload overlay if requested.
    if (overlayDirty && overlayBGRA && overlayUpload_) {
        void* mapped = nullptr;
        HRESULT hr = overlayUpload_->Map(0, nullptr, &mapped);
        if (SUCCEEDED(hr)) {
            uint8_t* dst = (uint8_t*)mapped;
            for (UINT y = 0; y < height_; ++y) {
                memcpy(dst + (size_t)y * overlayRowPitch_,
                       overlayBGRA + (size_t)y * width_ * 4, (size_t)width_ * 4);
            }
            overlayUpload_->Unmap(0, nullptr);

            transition(list, overlayTex_.get(), overlayState_, D3D12_RESOURCE_STATE_COPY_DEST);
            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = overlayUpload_.get();
            src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint.Offset = 0;
            src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            src.PlacedFootprint.Footprint.Width = width_;
            src.PlacedFootprint.Footprint.Height = height_;
            src.PlacedFootprint.Footprint.Depth = 1;
            src.PlacedFootprint.Footprint.RowPitch = overlayRowPitch_;

            D3D12_TEXTURE_COPY_LOCATION dstLoc{};
            dstLoc.pResource = overlayTex_.get();
            dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dstLoc.SubresourceIndex = 0;
            list->CopyTextureRegion(&dstLoc, 0, 0, 0, &src, nullptr);

            transition(list, overlayTex_.get(), D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            overlayState_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            overlayInitialized_ = true;
        }
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = ctx.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += (SIZE_T)ctx.rtvSize * ctx.backbufferIndex;
    list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    D3D12_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = (float)ctx.width;
    vp.Height = (float)ctx.height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    D3D12_RECT scissor{0, 0, (LONG)ctx.width, (LONG)ctx.height};
    list->RSSetViewports(1, &vp);
    list->RSSetScissorRects(1, &scissor);

    struct {
        float invSamples;
        uint32_t showOverlay;
        float pad[2];
    } cc;
    cc.invSamples = cb.accumulate ? (1.0f / (float)(cb.frameIndex + 1)) : 1.0f;
    cc.showOverlay = (showOverlay && overlayInitialized_) ? 1u : 0u;
    cc.pad[0] = cc.pad[1] = 0.0f;

    list->SetGraphicsRootSignature(graphicsRoot_.get());
    list->SetPipelineState(graphicsPSO_.get());
    list->SetGraphicsRoot32BitConstants(0, 4, &cc, 0);
    list->SetGraphicsRootDescriptorTable(1, ctx.srvGpu(1));
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3, 1, 0, 0);

    if (copyBackbuffer && shotReadback_) {
        transition(list, bb, D3D12_RESOURCE_STATE_RENDER_TARGET,
                   D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = shotReadback_.get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Offset = 0;
        dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        dst.PlacedFootprint.Footprint.Width = width_;
        dst.PlacedFootprint.Footprint.Height = height_;
        dst.PlacedFootprint.Footprint.Depth = 1;
        dst.PlacedFootprint.Footprint.RowPitch = shotRowPitch_;

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = bb;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = 0;
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        transition(list, bb, D3D12_RESOURCE_STATE_COPY_SOURCE,
                   D3D12_RESOURCE_STATE_RENDER_TARGET);
        screenshotPending_ = true;
        screenshotFrameAge_ = 0;
    }
    return true;
}

bool Renderer::saveScreenshotBMP(const std::string& path) {
    if (!screenshotPending_ || !shotReadback_) return false;

    void* mapped = nullptr;
    if (FAILED(shotReadback_->Map(0, nullptr, &mapped))) {
        logf("screenshot Map failed");
        return false;
    }
    const uint8_t* src = (const uint8_t*)mapped;

    uint32_t pixelBytes = width_ * height_ * 4;
    uint32_t fileSize = 54 + pixelBytes;
    uint8_t hdr[54] = {};
    hdr[0] = 'B';
    hdr[1] = 'M';
    memcpy(hdr + 2, &fileSize, 4);
    uint32_t offset = 54;
    memcpy(hdr + 10, &offset, 4);
    uint32_t dibSize = 40;
    memcpy(hdr + 14, &dibSize, 4);
    int32_t w = (int32_t)width_;
    int32_t h = -(int32_t)height_;
    memcpy(hdr + 18, &w, 4);
    memcpy(hdr + 22, &h, 4);
    uint16_t planes = 1;
    memcpy(hdr + 26, &planes, 2);
    uint16_t bpp = 32;
    memcpy(hdr + 28, &bpp, 2);
    uint32_t comp = 0;
    memcpy(hdr + 30, &comp, 4);
    memcpy(hdr + 34, &pixelBytes, 4);

    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        shotReadback_->Unmap(0, nullptr);
        logf("screenshot fopen failed: %s", path.c_str());
        return false;
    }
    fwrite(hdr, 1, sizeof(hdr), f);
    std::vector<uint8_t> row((size_t)width_ * 4);
    for (UINT y = 0; y < height_; ++y) {
        const uint8_t* s = src + (size_t)y * shotRowPitch_;
        for (UINT x = 0; x < width_; ++x) {
            row[x * 4 + 0] = s[x * 4 + 2];
            row[x * 4 + 1] = s[x * 4 + 1];
            row[x * 4 + 2] = s[x * 4 + 0];
            row[x * 4 + 3] = 255;
        }
        fwrite(row.data(), 1, row.size(), f);
    }
    fclose(f);
    shotReadback_->Unmap(0, nullptr);
    screenshotPending_ = false;
    logf("screenshot saved: %s", path.c_str());
    return true;
}

// ---------------------------------------------------------------------------
// Benchmark timing

bool Renderer::benchInit(D3D12Context& ctx, uint32_t maxFrames) {
    benchDestroy();
    maxBenchFrames_ = maxFrames;
    benchQueriesOk_ = false;

    D3D12_QUERY_HEAP_DESC qh{};
    qh.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    qh.Count = maxFrames * 2;
    qh.NodeMask = 0;
    HRESULT hr = ctx.device->CreateQueryHeap(&qh, IID_ID3D12QueryHeap, (void**)queryHeap_.put());
    if (FAILED(hr)) {
        logf("CreateQueryHeap(TIMESTAMP) failed: %s", hrStr(hr).c_str());
        return false;
    }
    if (!createBuffer(ctx.device.get(), (UINT64)maxFrames * 2 * sizeof(UINT64),
                      D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE,
                      queryReadback_.put(), D3D12_RESOURCE_STATE_COPY_DEST)) {
        return false;
    }
    benchQueriesOk_ = ctx.timestampFrequency() > 0;
    logf("bench queries: ok=%d freq=%llu maxFrames=%u", (int)benchQueriesOk_,
         (unsigned long long)ctx.timestampFrequency(), maxFrames);
    return true;
}

void Renderer::benchDestroy() {
    queryHeap_.reset();
    queryReadback_.reset();
    benchQueriesOk_ = false;
}

void Renderer::benchResetCounter(D3D12Context& ctx) {
    if (!counterBuf_ || !ctx.list) return;
    ctx.waitForGpu();
    ctx.allocators[0]->Reset();
    ctx.list->Reset(ctx.allocators[0].get(), nullptr);
    if (counterState_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        transition(ctx.list.get(), counterBuf_.get(), counterState_,
                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        counterState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    UINT zeros[4] = {0, 0, 0, 0};
    ctx.list->ClearUnorderedAccessViewUint(ctx.srvGpu(3), ctx.srvCpu(3), counterBuf_.get(),
                                           zeros, 0, nullptr);
    {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        b.UAV.pResource = counterBuf_.get();
        ctx.list->ResourceBarrier(1, &b);
    }
    ctx.list->Close();
    ID3D12CommandList* lists[] = {ctx.list.get()};
    ctx.queue->ExecuteCommandLists(1, lists);
    ctx.waitForGpu();
}

void Renderer::benchBeginFrame(D3D12Context& ctx, uint32_t frame) {
    if (!benchQueriesOk_ || !queryHeap_ || frame >= maxBenchFrames_) return;
    ctx.list->EndQuery(queryHeap_.get(), D3D12_QUERY_TYPE_TIMESTAMP, frame * 2);
}

void Renderer::benchEndFrame(D3D12Context& ctx, uint32_t frame) {
    if (!benchQueriesOk_ || !queryHeap_ || frame >= maxBenchFrames_) return;
    ctx.list->EndQuery(queryHeap_.get(), D3D12_QUERY_TYPE_TIMESTAMP, frame * 2 + 1);
}

bool Renderer::benchFinish(D3D12Context& ctx, uint32_t frames, BenchTiming& timing) {
    timing.gpuMs.clear();
    timing.rayCount = 0;
    timing.gpuTimingAvailable = false;
    timing.timestampFrequency = ctx.timestampFrequency();
    if (frames == 0) return false;
    if (frames > maxBenchFrames_) frames = maxBenchFrames_;

    ctx.waitForGpu();
    ctx.allocators[0]->Reset();
    ctx.list->Reset(ctx.allocators[0].get(), nullptr);

    if (benchQueriesOk_ && queryHeap_ && queryReadback_) {
        ctx.list->ResolveQueryData(queryHeap_.get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, frames * 2,
                                   queryReadback_.get(), 0);
    }

    if (counterBuf_ && counterReadback_) {
        transition(ctx.list.get(), counterBuf_.get(), counterState_,
                   D3D12_RESOURCE_STATE_COPY_SOURCE);
        counterState_ = D3D12_RESOURCE_STATE_COPY_SOURCE;
        ctx.list->CopyResource(counterReadback_.get(), counterBuf_.get());
        transition(ctx.list.get(), counterBuf_.get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        counterState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    ctx.list->Close();
    ID3D12CommandList* lists[] = {ctx.list.get()};
    ctx.queue->ExecuteCommandLists(1, lists);
    ctx.waitForGpu();

    if (benchQueriesOk_ && queryReadback_) {
        void* mapped = nullptr;
        if (SUCCEEDED(queryReadback_->Map(0, nullptr, &mapped))) {
            const UINT64* ts = (const UINT64*)mapped;
            double freq = (double)timing.timestampFrequency;
            timing.gpuMs.reserve(frames);
            for (uint32_t i = 0; i < frames; ++i) {
                UINT64 a = ts[i * 2];
                UINT64 b = ts[i * 2 + 1];
                if (b >= a && freq > 0.0) {
                    timing.gpuMs.push_back((double)(b - a) * 1000.0 / freq);
                } else {
                    timing.gpuMs.push_back(0.0);
                }
            }
            queryReadback_->Unmap(0, nullptr);
            timing.gpuTimingAvailable = !timing.gpuMs.empty();
        }
    }

    if (counterReadback_) {
        void* mapped = nullptr;
        if (SUCCEEDED(counterReadback_->Map(0, nullptr, &mapped))) {
            timing.rayCount = *(const uint32_t*)mapped;
            counterReadback_->Unmap(0, nullptr);
        }
    }

    logf("bench finish: frames=%u gpuTiming=%d rayCount=%llu", frames,
         (int)timing.gpuTimingAvailable, (unsigned long long)timing.rayCount);
    return true;
}

void Renderer::destroy() {
    benchDestroy();
    computePSO_.reset();
    graphicsPSO_.reset();
    computeRoot_.reset();
    graphicsRoot_.reset();
    csBlob_.reset();
    vsBlob_.reset();
    psBlob_.reset();
    nodeBuf_.reset();
    primBuf_.reset();
    counterBuf_.reset();
    counterReadback_.reset();
    accumTex_.reset();
    overlayTex_.reset();
    overlayUpload_.reset();
    shotReadback_.reset();
}

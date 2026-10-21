#include "dxr_test.h"

#include "dxr_test_dxil.h"
#include "util.h"

#include <algorithm>
#include <cstring>

namespace {

constexpr uint32_t kWidth = 64;
constexpr uint32_t kHeight = 64;
constexpr uint64_t kSbtRecordSize = 64;

bool createUploadBuffer(ID3D12Device* device, UINT64 size, ID3D12Resource** out) {
    return createBuffer(device, size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, out,
                        D3D12_RESOURCE_STATE_GENERIC_READ);
}

bool writeBuffer(ID3D12Resource* res, const void* data, size_t bytes) {
    void* mapped = nullptr;
    if (FAILED(res->Map(0, nullptr, &mapped)) || !mapped) return false;
    memcpy(mapped, data, bytes);
    res->Unmap(0, nullptr);
    return true;
}

bool serializeAndCreateRootSig(D3D12Api* api, ID3D12Device* device,
                               const D3D12_ROOT_SIGNATURE_DESC& desc,
                               ID3D12RootSignature** out, std::string& err) {
    if (!api || !api->serializeRootSignature) {
        err = "D3D12SerializeRootSignature unavailable";
        return false;
    }
    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> errBlob;
    HRESULT hr = api->serializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0,
                                             blob.put(), errBlob.put());
    if (FAILED(hr)) {
        err = "serialize root signature failed: " + hrStr(hr);
        if (errBlob && errBlob->GetBufferSize()) {
            logf("DXR root sig error: %.*s", (int)errBlob->GetBufferSize(),
                 (const char*)errBlob->GetBufferPointer());
        }
        return false;
    }
    hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                     IID_ID3D12RootSignature, (void**)out);
    if (FAILED(hr)) {
        err = "CreateRootSignature failed: " + hrStr(hr);
        return false;
    }
    return true;
}

}  // namespace

bool runDxrTest(D3D12Context& ctx, D3D12Api* api, DxrTestResult& out) {
    out = DxrTestResult{};
    out.attempted = true;
    out.width = kWidth;
    out.height = kHeight;
    out.total = kWidth * kHeight;

    if (!ctx.device5 || !ctx.list4) {
        out.error = "ID3D12Device5 / GraphicsCommandList4 unavailable";
        return false;
    }
    if (!ctx.device || !ctx.list || !ctx.queue || !ctx.srvHeap) {
        out.error = "D3D12 context not initialized";
        return false;
    }

    out.stage = "geometry";
    struct Vertex {
        float x, y, z;
    };
    // Large triangle at z=1. 64x64 rays from z=-1 in [-0.8, 0.8]^2 all hit it.
    Vertex verts[3] = {
        {-4.0f, -4.0f, 1.0f},
        {4.0f, -4.0f, 1.0f},
        {0.0f, 4.0f, 1.0f},
    };

    ComPtr<ID3D12Resource> vb;
    ComPtr<ID3D12Resource> instanceBuf;
    if (!createUploadBuffer(ctx.device.get(), sizeof(verts), vb.put()) ||
        !writeBuffer(vb.get(), verts, sizeof(verts))) {
        out.error = "vertex buffer creation failed";
        return false;
    }

    D3D12_RAYTRACING_INSTANCE_DESC inst{};
    inst.Transform[0][0] = 1.0f;
    inst.Transform[1][1] = 1.0f;
    inst.Transform[2][2] = 1.0f;
    inst.InstanceID = 0;
    inst.InstanceMask = 0xFF;
    inst.InstanceContributionToHitGroupIndex = 0;
    inst.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
    inst.AccelerationStructure = 0;  // filled after BLAS creation

    if (!createUploadBuffer(ctx.device.get(), sizeof(inst), instanceBuf.put())) {
        out.error = "instance buffer creation failed";
        return false;
    }

    out.stage = "blas";
    D3D12_RAYTRACING_GEOMETRY_DESC geom{};
    geom.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geom.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geom.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geom.Triangles.VertexCount = 3;
    geom.Triangles.VertexBuffer.StartAddress = vb->GetGPUVirtualAddress();
    geom.Triangles.VertexBuffer.StrideInBytes = sizeof(Vertex);
    geom.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;
    geom.Triangles.IndexCount = 0;
    geom.Triangles.IndexBuffer = 0;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blasInputs{};
    blasInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    blasInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    blasInputs.NumDescs = 1;
    blasInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    blasInputs.pGeometryDescs = &geom;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blasInfo{};
    ctx.device5->GetRaytracingAccelerationStructurePrebuildInfo(&blasInputs, &blasInfo);
    if (blasInfo.ResultDataMaxSizeInBytes == 0) {
        out.error = "BLAS prebuild returned zero size";
        return false;
    }

    ComPtr<ID3D12Resource> blasBuf;
    ComPtr<ID3D12Resource> blasScratch;
    if (!createBuffer(ctx.device.get(), blasInfo.ResultDataMaxSizeInBytes,
                      D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                      blasBuf.put(), D3D12_RESOURCE_STATE_COMMON) ||
        !createBuffer(ctx.device.get(), std::max<UINT64>(blasInfo.ScratchDataSizeInBytes, 256),
                      D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                      blasScratch.put(), D3D12_RESOURCE_STATE_COMMON)) {
        out.error = "BLAS buffer creation failed";
        return false;
    }

    inst.AccelerationStructure = blasBuf->GetGPUVirtualAddress();
    if (!writeBuffer(instanceBuf.get(), &inst, sizeof(inst))) {
        out.error = "instance upload failed";
        return false;
    }

    out.stage = "tlas";
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlasInputs{};
    tlasInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    tlasInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    tlasInputs.NumDescs = 1;
    tlasInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    tlasInputs.InstanceDescs = instanceBuf->GetGPUVirtualAddress();

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlasInfo{};
    ctx.device5->GetRaytracingAccelerationStructurePrebuildInfo(&tlasInputs, &tlasInfo);
    if (tlasInfo.ResultDataMaxSizeInBytes == 0) {
        out.error = "TLAS prebuild returned zero size";
        return false;
    }

    ComPtr<ID3D12Resource> tlasBuf;
    ComPtr<ID3D12Resource> tlasScratch;
    if (!createBuffer(ctx.device.get(), tlasInfo.ResultDataMaxSizeInBytes,
                      D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                      tlasBuf.put(), D3D12_RESOURCE_STATE_COMMON) ||
        !createBuffer(ctx.device.get(), std::max<UINT64>(tlasInfo.ScratchDataSizeInBytes, 256),
                      D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                      tlasScratch.put(), D3D12_RESOURCE_STATE_COMMON)) {
        out.error = "TLAS buffer creation failed";
        return false;
    }

    out.stage = "root_signature";
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.RegisterSpace = 0;
    params[0].Constants.Num32BitValues = 4;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &srvRange;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[2].Descriptor.ShaderRegister = 0;
    params[2].Descriptor.RegisterSpace = 0;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = 3;
    rsDesc.pParameters = params;
    rsDesc.NumStaticSamplers = 0;
    rsDesc.pStaticSamplers = nullptr;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3D12RootSignature> rootSig;
    std::string err;
    if (!serializeAndCreateRootSig(api, ctx.device.get(), rsDesc, rootSig.put(), err)) {
        out.error = err;
        return false;
    }

    out.stage = "state_object";
    D3D12_DXIL_LIBRARY_DESC dxilLib{};
    dxilLib.DXILLibrary.pShaderBytecode = dxr_test_dxil;
    dxilLib.DXILLibrary.BytecodeLength = dxr_test_dxilWordCount * sizeof(uint32_t);
    dxilLib.NumExports = 0;
    dxilLib.pExports = nullptr;

    D3D12_HIT_GROUP_DESC hitGroup{};
    hitGroup.HitGroupExport = L"HitGroup";
    hitGroup.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    hitGroup.AnyHitShaderImport = nullptr;
    hitGroup.ClosestHitShaderImport = L"ClosestHit";
    hitGroup.IntersectionShaderImport = nullptr;

    D3D12_RAYTRACING_SHADER_CONFIG shaderConfig{};
    shaderConfig.MaxPayloadSizeInBytes = 32;
    shaderConfig.MaxAttributeSizeInBytes = 8;

    D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig{};
    pipelineConfig.MaxTraceRecursionDepth = 1;

    D3D12_GLOBAL_ROOT_SIGNATURE globalRoot{};
    globalRoot.pGlobalRootSignature = rootSig.get();

    D3D12_STATE_SUBOBJECT subobjects[5]{};
    subobjects[0].Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
    subobjects[0].pDesc = &dxilLib;
    subobjects[1].Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    subobjects[1].pDesc = &hitGroup;
    subobjects[2].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
    subobjects[2].pDesc = &shaderConfig;
    subobjects[3].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
    subobjects[3].pDesc = &pipelineConfig;
    subobjects[4].Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
    subobjects[4].pDesc = &globalRoot;

    D3D12_STATE_OBJECT_DESC soDesc{};
    soDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    soDesc.NumSubobjects = 5;
    soDesc.pSubobjects = subobjects;

    ComPtr<ID3D12StateObject> stateObject;
    HRESULT hr = ctx.device5->CreateStateObject(&soDesc, IID_ID3D12StateObject,
                                                (void**)stateObject.put());
    if (FAILED(hr)) {
        out.error = "CreateStateObject failed: " + hrStr(hr);
        return false;
    }
    out.stateObjectCreated = true;

    ComPtr<ID3D12StateObjectProperties> soProps;
    hr = stateObject->QueryInterface(IID_ID3D12StateObjectProperties, (void**)soProps.put());
    if (FAILED(hr) || !soProps) {
        out.error = "ID3D12StateObjectProperties unavailable: " + hrStr(hr);
        return false;
    }

    void* rayId = soProps->GetShaderIdentifier(L"RayGen");
    void* missId = soProps->GetShaderIdentifier(L"Miss");
    void* hitId = soProps->GetShaderIdentifier(L"HitGroup");
    if (!rayId || !missId || !hitId) {
        out.error = "shader identifiers missing (RayGen/Miss/HitGroup)";
        return false;
    }

    out.stage = "shader_table";
    ComPtr<ID3D12Resource> sbt;
    const UINT64 sbtSize = kSbtRecordSize * 3;
    if (!createUploadBuffer(ctx.device.get(), sbtSize, sbt.put())) {
        out.error = "shader table buffer creation failed";
        return false;
    }
    {
        void* mapped = nullptr;
        if (FAILED(sbt->Map(0, nullptr, &mapped)) || !mapped) {
            out.error = "shader table map failed";
            return false;
        }
        memset(mapped, 0, (size_t)sbtSize);
        memcpy((uint8_t*)mapped + 0 * kSbtRecordSize, rayId,
               D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        memcpy((uint8_t*)mapped + 1 * kSbtRecordSize, missId,
               D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        memcpy((uint8_t*)mapped + 2 * kSbtRecordSize, hitId,
               D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        sbt->Unmap(0, nullptr);
    }

    out.stage = "output";
    ComPtr<ID3D12Resource> outputBuf;
    ComPtr<ID3D12Resource> readback;
    const UINT64 outBytes = (UINT64)kWidth * kHeight * sizeof(uint32_t);
    if (!createBuffer(ctx.device.get(), outBytes, D3D12_HEAP_TYPE_DEFAULT,
                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, outputBuf.put(),
                      D3D12_RESOURCE_STATE_COMMON) ||
        !createBuffer(ctx.device.get(), outBytes, D3D12_HEAP_TYPE_READBACK,
                      D3D12_RESOURCE_FLAG_NONE, readback.put(),
                      D3D12_RESOURCE_STATE_COPY_DEST)) {
        out.error = "output/readback buffer creation failed";
        return false;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_UNKNOWN;
    srv.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.RaytracingAccelerationStructure.Location = tlasBuf->GetGPUVirtualAddress();
    ctx.device->CreateShaderResourceView(nullptr, &srv, ctx.srvCpu(4));

    out.stage = "commands";
    hr = ctx.allocators[0]->Reset();
    if (SUCCEEDED(hr)) hr = ctx.list->Reset(ctx.allocators[0].get(), nullptr);
    if (FAILED(hr)) {
        out.error = "command list reset failed: " + hrStr(hr);
        return false;
    }

    transition(ctx.list.get(), blasBuf.get(), D3D12_RESOURCE_STATE_COMMON,
               D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
    transition(ctx.list.get(), blasScratch.get(), D3D12_RESOURCE_STATE_COMMON,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blasBuild{};
    blasBuild.DestAccelerationStructureData = blasBuf->GetGPUVirtualAddress();
    blasBuild.Inputs = blasInputs;
    blasBuild.SourceAccelerationStructureData = 0;
    blasBuild.ScratchAccelerationStructureData = blasScratch->GetGPUVirtualAddress();
    ctx.list4->BuildRaytracingAccelerationStructure(&blasBuild, 0, nullptr);

    {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        b.UAV.pResource = blasBuf.get();
        ctx.list->ResourceBarrier(1, &b);
    }

    transition(ctx.list.get(), tlasBuf.get(), D3D12_RESOURCE_STATE_COMMON,
               D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
    transition(ctx.list.get(), tlasScratch.get(), D3D12_RESOURCE_STATE_COMMON,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlasBuild{};
    tlasBuild.DestAccelerationStructureData = tlasBuf->GetGPUVirtualAddress();
    tlasBuild.Inputs = tlasInputs;
    tlasBuild.SourceAccelerationStructureData = 0;
    tlasBuild.ScratchAccelerationStructureData = tlasScratch->GetGPUVirtualAddress();
    ctx.list4->BuildRaytracingAccelerationStructure(&tlasBuild, 0, nullptr);

    {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        b.UAV.pResource = tlasBuf.get();
        ctx.list->ResourceBarrier(1, &b);
    }
    out.asBuilt = true;

    transition(ctx.list.get(), outputBuf.get(), D3D12_RESOURCE_STATE_COMMON,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    ID3D12DescriptorHeap* heaps[] = {ctx.srvHeap.get()};
    ctx.list4->SetDescriptorHeaps(1, heaps);
    ctx.list4->SetComputeRootSignature(rootSig.get());
    ctx.list4->SetPipelineState1(stateObject.get());

    UINT constants[4] = {kWidth, kHeight, 0, 0};
    ctx.list4->SetComputeRoot32BitConstants(0, 4, constants, 0);
    ctx.list4->SetComputeRootDescriptorTable(1, ctx.srvGpu(4));
    ctx.list4->SetComputeRootUnorderedAccessView(2, outputBuf->GetGPUVirtualAddress());

    D3D12_DISPATCH_RAYS_DESC dispatch{};
    const D3D12_GPU_VIRTUAL_ADDRESS sbtVA = sbt->GetGPUVirtualAddress();
    dispatch.RayGenerationShaderRecord.StartAddress = sbtVA;
    dispatch.RayGenerationShaderRecord.SizeInBytes = kSbtRecordSize;
    dispatch.MissShaderTable.StartAddress = sbtVA + kSbtRecordSize;
    dispatch.MissShaderTable.SizeInBytes = kSbtRecordSize;
    dispatch.MissShaderTable.StrideInBytes = kSbtRecordSize;
    dispatch.HitGroupTable.StartAddress = sbtVA + kSbtRecordSize * 2;
    dispatch.HitGroupTable.SizeInBytes = kSbtRecordSize;
    dispatch.HitGroupTable.StrideInBytes = kSbtRecordSize;
    dispatch.CallableShaderTable = {};
    dispatch.Width = kWidth;
    dispatch.Height = kHeight;
    dispatch.Depth = 1;
    ctx.list4->DispatchRays(&dispatch);
    out.dispatched = true;

    {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        b.UAV.pResource = outputBuf.get();
        ctx.list->ResourceBarrier(1, &b);
    }
    transition(ctx.list.get(), outputBuf.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
               D3D12_RESOURCE_STATE_COPY_SOURCE);
    ctx.list->CopyResource(readback.get(), outputBuf.get());

    hr = ctx.list->Close();
    if (FAILED(hr)) {
        out.error = "command list close failed: " + hrStr(hr);
        return false;
    }
    ID3D12CommandList* lists[] = {ctx.list.get()};
    ctx.queue->ExecuteCommandLists(1, lists);
    ctx.waitForGpu();

    HRESULT removed = ctx.device->GetDeviceRemovedReason();
    if (FAILED(removed)) {
        out.error = "device removed: " + hrStr(removed);
        return false;
    }

    out.stage = "readback";
    void* mapped = nullptr;
    if (FAILED(readback->Map(0, nullptr, &mapped)) || !mapped) {
        out.error = "readback map failed";
        return false;
    }
    const uint32_t* values = (const uint32_t*)mapped;
    uint32_t hits = 0;
    for (uint32_t i = 0; i < kWidth * kHeight; ++i) {
        if (values[i] != 0) ++hits;
    }
    readback->Unmap(0, nullptr);
    out.hits = hits;
    out.stage = "done";
    logf("DXR test: %u/%u hits, stateObject=%d asBuilt=%d dispatched=%d",
         out.hits, out.total, (int)out.stateObjectCreated, (int)out.asBuilt,
         (int)out.dispatched);
    return true;
}

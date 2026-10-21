#pragma once

#include "util.h"

struct ProbeResult {
    // adapter / device
    std::string adapterName;
    uint32_t vendorId = 0;
    uint32_t deviceId = 0;
    uint64_t dedicatedVram = 0;
    std::string featureLevel;
    int raytracingTier = -1;
    bool device5 = false;
    bool commandList4 = false;
    int shaderModelMajor = 0;
    int shaderModelMinor = 0;
    bool asPrebuildOk = false;
    uint64_t asResultSize = 0;
    uint64_t asScratchSize = 0;
    uint64_t memBudget = 0;
    uint64_t memUsage = 0;

    // modules / environment
    std::string d3d12Path, d3d12Version;
    std::string d3d12corePath, d3d12coreVersion;
    std::string dxgiPath, dxgiVersion;
    std::string compilerPath, compilerVersion;
    std::string dxcPath, dxcVersion;
    std::string vulkanPath;
    std::string wineVersion;
    std::string vkd3dConfig, vkd3dShaderModel, vkd3dFeatureLevel, dxvkConfig;

    // underlying Vulkan
    bool vkAvailable = false;
    std::string vkError;
    std::string vkApiVersion;
    std::string vkDeviceName;
    std::string vkDeviceApiVersion;
    std::string vkDriverName;
    std::string vkDriverInfo;
    bool vkRayQuery = false;
    bool vkRayPipeline = false;
    bool vkAccelStruct = false;
    bool vkDeferredOps = false;
    bool vkBufferDeviceAddress = false;
    bool vkTimestamps = false;
    float vkTimestampPeriod = 0.0f;
    std::string vkExtensions;   // comma separated RT-related extensions found

    std::string verdict;
};

void runD3D12Probe(ID3D12Device* device, ID3D12Device5* device5,
                   ID3D12GraphicsCommandList4* list4,
                   IDXGIAdapter1* adapter, ProbeResult& out);
void probeModules(ProbeResult& out);
void runVulkanProbe(ProbeResult& out);
std::string buildVerdict(const ProbeResult& p);
std::string tierStr(int tier);

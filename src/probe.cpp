#include "probe.h"

#include <vulkan/vulkan_core.h>

#include <cstring>

std::string tierStr(int tier) {
    switch (tier) {
    case 0: return "0 (NOT_SUPPORTED)";
    case 0xA: return "1.0";
    case 0xB: return "1.1";
    case 0xC: return "1.2";
    default: return strf("%d (unknown)", tier);
    }
}

static std::string vkVersionStr(uint32_t v) {
    return strf("%u.%u.%u", VK_VERSION_MAJOR(v), VK_VERSION_MINOR(v), VK_VERSION_PATCH(v));
}

static std::string wineVersion() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return {};
    auto fn = (const char*(*)(void))GetProcAddress(ntdll, "wine_get_version");
    if (!fn) return {};
    const char* v = fn();
    return v ? std::string(v) : std::string();
}

void probeModules(ProbeResult& out) {
    out.d3d12Path = modulePath("d3d12.dll");
    out.d3d12Version = moduleVersion("d3d12.dll");
    out.d3d12corePath = modulePath("d3d12core.dll");
    out.d3d12coreVersion = moduleVersion("d3d12core.dll");
    out.dxgiPath = modulePath("dxgi.dll");
    out.dxgiVersion = moduleVersion("dxgi.dll");
    out.compilerPath = modulePath("d3dcompiler_47.dll");
    out.compilerVersion = moduleVersion("d3dcompiler_47.dll");
    out.dxcPath = modulePath("dxcompiler.dll");
    out.dxcVersion = moduleVersion("dxcompiler.dll");
    out.vulkanPath = modulePath("vulkan-1.dll");
    out.wineVersion = wineVersion();

    out.vkd3dConfig = getEnvStr("VKD3D_CONFIG");
    out.vkd3dShaderModel = getEnvStr("VKD3D_SHADER_MODEL");
    out.vkd3dFeatureLevel = getEnvStr("VKD3D_FEATURE_LEVEL");
    out.dxvkConfig = getEnvStr("DXVK_CONFIG");

    logf("d3d12.dll: %s %s", out.d3d12Version.c_str(), out.d3d12Path.c_str());
    logf("d3d12core.dll: %s %s", out.d3d12coreVersion.c_str(), out.d3d12corePath.c_str());
    logf("dxgi.dll: %s %s", out.dxgiVersion.c_str(), out.dxgiPath.c_str());
    logf("d3dcompiler: %s %s", out.compilerVersion.c_str(), out.compilerPath.c_str());
    logf("dxcompiler: %s %s", out.dxcVersion.c_str(), out.dxcPath.c_str());
    logf("vulkan-1.dll: %s", out.vulkanPath.c_str());
    logf("wine version: %s", out.wineVersion.c_str());
    logf("VKD3D_CONFIG='%s' VKD3D_SHADER_MODEL='%s' VKD3D_FEATURE_LEVEL='%s'",
         out.vkd3dConfig.c_str(), out.vkd3dShaderModel.c_str(), out.vkd3dFeatureLevel.c_str());
}

void runD3D12Probe(ID3D12Device* device, ID3D12Device5* device5,
                   ID3D12GraphicsCommandList4* list4,
                   IDXGIAdapter1* adapter, ProbeResult& out) {
    if (adapter) {
        DXGI_ADAPTER_DESC1 d{};
        if (SUCCEEDED(adapter->GetDesc1(&d))) {
            out.adapterName = narrow(d.Description);
            out.vendorId = d.VendorId;
            out.deviceId = d.DeviceId;
            out.dedicatedVram = d.DedicatedVideoMemory;
        }
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5{};
    HRESULT hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &o5, sizeof(o5));
    if (SUCCEEDED(hr)) {
        out.raytracingTier = (int)o5.RaytracingTier;
    } else {
        out.raytracingTier = -1;
        logf("CheckFeatureSupport(OPTIONS5) failed: %s", hrStr(hr).c_str());
    }

    out.device5 = device5 != nullptr;
    out.commandList4 = list4 != nullptr;

    static const uint32_t models[] = {0x68, 0x67, 0x66, 0x65, 0x64, 0x63, 0x62, 0x61,
                                      0x60, 0x51, 0x50, 0x41, 0x40};
    for (uint32_t m : models) {
        D3D12_FEATURE_DATA_SHADER_MODEL sm{};
        sm.HighestShaderModel = (D3D_SHADER_MODEL)m;
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm))) &&
            (uint32_t)sm.HighestShaderModel == m) {
            out.shaderModelMajor = (int)(m >> 4);
            out.shaderModelMinor = (int)(m & 0xF);
            break;
        }
    }

    if (device5 && out.raytracingTier > 0) {
        float verts[9] = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f};
        ComPtr<ID3D12Resource> vb;
        if (createBuffer(device, sizeof(verts), D3D12_HEAP_TYPE_UPLOAD,
                         D3D12_RESOURCE_FLAG_NONE, vb.put())) {
            void* mapped = nullptr;
            if (SUCCEEDED(vb->Map(0, nullptr, &mapped))) {
                memcpy(mapped, verts, sizeof(verts));
                vb->Unmap(0, nullptr);

                D3D12_RAYTRACING_GEOMETRY_DESC geom{};
                geom.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
                geom.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
                geom.Triangles.VertexBuffer.StartAddress = vb->GetGPUVirtualAddress();
                geom.Triangles.VertexBuffer.StrideInBytes = sizeof(float) * 3;
                geom.Triangles.VertexCount = 3;
                geom.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
                geom.Triangles.IndexBuffer = 0;
                geom.Triangles.IndexCount = 0;
                geom.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;

                D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
                inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
                inputs.Flags =
                    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
                inputs.NumDescs = 1;
                inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
                inputs.pGeometryDescs = &geom;

                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
                device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
                out.asPrebuildOk = true;
                out.asResultSize = info.ResultDataMaxSizeInBytes;
                out.asScratchSize = info.ScratchDataSizeInBytes;
                logf("AS prebuild: result=%llu scratch=%llu",
                     (unsigned long long)info.ResultDataMaxSizeInBytes,
                     (unsigned long long)info.ScratchDataSizeInBytes);
            }
        }
    }

    if (adapter) {
        ComPtr<IDXGIAdapter3> adapter3;
        if (SUCCEEDED(adapter->QueryInterface(IID_IDXGIAdapter3, (void**)adapter3.put()))) {
            DXGI_QUERY_VIDEO_MEMORY_INFO mem{};
            if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL,
                                                         &mem))) {
                out.memBudget = mem.Budget;
                out.memUsage = mem.CurrentUsage;
            }
        }
    }

    logf("D3D12 probe: adapter='%s' vendor=0x%04X device=0x%04X tier=%s sm=%d.%d dev5=%d cl4=%d",
         out.adapterName.c_str(), out.vendorId, out.deviceId,
         tierStr(out.raytracingTier).c_str(), out.shaderModelMajor, out.shaderModelMinor,
         (int)out.device5, (int)out.commandList4);
}

// ---------------------------------------------------------------------------
// Underlying Vulkan (winevulkan) probe. Runs last and is SEH-guarded.

struct VkApi {
    HMODULE lib = nullptr;
    PFN_vkGetInstanceProcAddr gipa = nullptr;
    PFN_vkCreateInstance createInstance = nullptr;
    PFN_vkDestroyInstance destroyInstance = nullptr;
    PFN_vkEnumeratePhysicalDevices enumeratePhysicalDevices = nullptr;
    PFN_vkGetPhysicalDeviceProperties getProperties = nullptr;
    PFN_vkGetPhysicalDeviceProperties2 getProperties2 = nullptr;
    PFN_vkEnumerateDeviceExtensionProperties enumerateDeviceExtensions = nullptr;
};

static bool hasExt(const std::vector<VkExtensionProperties>& exts, const char* name) {
    for (const auto& e : exts) {
        if (strcmp(e.extensionName, name) == 0) return true;
    }
    return false;
}

static void vkProbeImpl(ProbeResult& out) {
    VkApi api;
    api.lib = LoadLibraryW(L"vulkan-1.dll");
    if (!api.lib) {
        out.vkError = "vulkan-1.dll not loadable";
        return;
    }
    api.gipa = (PFN_vkGetInstanceProcAddr)GetProcAddress(api.lib, "vkGetInstanceProcAddr");
    if (!api.gipa) {
        out.vkError = "vkGetInstanceProcAddr missing";
        return;
    }

    // Only global commands are guaranteed to be retrievable with a NULL instance.
    api.createInstance = (PFN_vkCreateInstance)api.gipa(nullptr, "vkCreateInstance");
    if (!api.createInstance) {
        out.vkError = "vkCreateInstance missing";
        return;
    }

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "rtprobe";
    app.applicationVersion = 1;
    app.pEngineName = "rtprobe";
    app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult vr = VK_ERROR_INCOMPATIBLE_DRIVER;
    const uint32_t versions[] = {VK_API_VERSION_1_3, VK_API_VERSION_1_1, VK_API_VERSION_1_0};
    for (uint32_t v : versions) {
        app.apiVersion = v;
        vr = api.createInstance(&ci, nullptr, &instance);
        if (vr == VK_SUCCESS) break;
    }
    if (vr != VK_SUCCESS) {
        out.vkError = strf("vkCreateInstance failed (%d)", (int)vr);
        return;
    }

    // Instance-level commands must be resolved with the created instance.
    api.destroyInstance = (PFN_vkDestroyInstance)api.gipa(instance, "vkDestroyInstance");
    api.enumeratePhysicalDevices =
        (PFN_vkEnumeratePhysicalDevices)api.gipa(instance, "vkEnumeratePhysicalDevices");
    api.getProperties =
        (PFN_vkGetPhysicalDeviceProperties)api.gipa(instance, "vkGetPhysicalDeviceProperties");
    api.getProperties2 =
        (PFN_vkGetPhysicalDeviceProperties2)api.gipa(instance, "vkGetPhysicalDeviceProperties2");
    api.enumerateDeviceExtensions =
        (PFN_vkEnumerateDeviceExtensionProperties)api.gipa(instance,
                                                           "vkEnumerateDeviceExtensionProperties");
    if (!api.destroyInstance || !api.enumeratePhysicalDevices || !api.getProperties ||
        !api.enumerateDeviceExtensions) {
        std::string missing;
        if (!api.destroyInstance) missing += " vkDestroyInstance";
        if (!api.enumeratePhysicalDevices) missing += " vkEnumeratePhysicalDevices";
        if (!api.getProperties) missing += " vkGetPhysicalDeviceProperties";
        if (!api.enumerateDeviceExtensions) missing += " vkEnumerateDeviceExtensionProperties";
        out.vkError = "required Vulkan entry points missing:" + missing;
        api.destroyInstance ? api.destroyInstance(instance, nullptr) : (void)0;
        return;
    }

    uint32_t count = 0;
    vr = api.enumeratePhysicalDevices(instance, &count, nullptr);
    if (vr != VK_SUCCESS || count == 0) {
        out.vkError = strf("vkEnumeratePhysicalDevices failed (%d), count=%u", (int)vr, count);
        api.destroyInstance(instance, nullptr);
        return;
    }
    std::vector<VkPhysicalDevice> devices(count);
    vr = api.enumeratePhysicalDevices(instance, &count, devices.data());
    if (vr != VK_SUCCESS) {
        out.vkError = strf("vkEnumeratePhysicalDevices(list) failed (%d)", (int)vr);
        api.destroyInstance(instance, nullptr);
        return;
    }

    VkPhysicalDevice phys = devices[0];
    VkPhysicalDeviceProperties props{};
    api.getProperties(phys, &props);
    out.vkDeviceName = props.deviceName;
    out.vkApiVersion = vkVersionStr(props.apiVersion);
    out.vkDeviceApiVersion = out.vkApiVersion;
    out.vkTimestamps = props.limits.timestampComputeAndGraphics != 0;
    out.vkTimestampPeriod = props.limits.timestampPeriod;

    VkPhysicalDeviceDriverProperties driverProps{};
    driverProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
    VkPhysicalDeviceProperties2 props2{};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props2.pNext = &driverProps;
    if (api.getProperties2) {
        api.getProperties2(phys, &props2);
        if (driverProps.driverName[0]) out.vkDriverName = driverProps.driverName;
        if (driverProps.driverInfo[0]) out.vkDriverInfo = driverProps.driverInfo;
    }

    uint32_t extCount = 0;
    api.enumerateDeviceExtensions(phys, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> exts(extCount);
    if (extCount) {
        api.enumerateDeviceExtensions(phys, nullptr, &extCount, exts.data());
        exts.resize(extCount);
    }

    struct ExtCheck {
        const char* name;
        bool* flag;
    };
    ExtCheck checks[] = {
        {"VK_KHR_ray_query", &out.vkRayQuery},
        {"VK_KHR_ray_tracing_pipeline", &out.vkRayPipeline},
        {"VK_KHR_acceleration_structure", &out.vkAccelStruct},
        {"VK_KHR_deferred_host_operations", &out.vkDeferredOps},
        {"VK_KHR_buffer_device_address", &out.vkBufferDeviceAddress},
    };
    std::string found;
    for (const auto& c : checks) {
        *c.flag = hasExt(exts, c.name);
        if (*c.flag) {
            if (!found.empty()) found += ",";
            found += c.name;
        }
    }
    out.vkExtensions = found;
    out.vkAvailable = true;

    logf("Vulkan: device='%s' api=%s driver='%s' info='%s' ts=%d period=%.1f",
         out.vkDeviceName.c_str(), out.vkApiVersion.c_str(), out.vkDriverName.c_str(),
         out.vkDriverInfo.c_str(), (int)out.vkTimestamps, out.vkTimestampPeriod);
    logf("Vulkan RT extensions: %s", found.c_str());

    api.destroyInstance(instance, nullptr);
}

static void vkProbeThunk(void* p) { vkProbeImpl(*(ProbeResult*)p); }

void runVulkanProbe(ProbeResult& out) {
    long code = sehGuardVoid(vkProbeThunk, &out);
    if (code != 0) {
        out.vkAvailable = false;
        out.vkError = strf("Vulkan probe crashed with SEH code 0x%08lX", (unsigned long)code);
        logf("%s", out.vkError.c_str());
    } else if (!out.vkAvailable && out.vkError.empty()) {
        out.vkError = "no Vulkan physical device";
        logf("%s", out.vkError.c_str());
    }
}

std::string buildVerdict(const ProbeResult& p) {
    std::string v;
    if (p.raytracingTier <= 0) {
        v = "DXR is NOT supported by this D3D12 stack (RaytracingTier=0). ";
        if (p.vkAvailable) {
            if (!p.vkRayPipeline) {
                v += "Underlying Vulkan driver does not expose VK_KHR_ray_tracing_pipeline, "
                     "which vkd3d-proton needs for DXR. ";
            }
            if (!p.vkAccelStruct) {
                v += "VK_KHR_acceleration_structure is also missing. ";
            }
            if (p.vkRayQuery) {
                v += "VK_KHR_ray_query is present (inline ray queries only). ";
            }
        } else {
            v += "Underlying Vulkan probe unavailable: " + p.vkError + ". ";
        }
        v += "Software path tracing via D3D12 compute is the supported test path.";
    } else if (p.raytracingTier == 0xA) {
        v = "DXR tier 1.0 reported; ray tracing pipelines may be usable. "
            "End-to-end RTPSO verification needs a DXIL (SM 6.3+) compiler, which is absent here.";
    } else if (p.raytracingTier >= 0xB) {
        v = "DXR tier 1.1+ reported; ray tracing pipelines may be usable. "
            "End-to-end RTPSO verification needs a DXIL (SM 6.5+) compiler, which is absent here.";
    } else {
        v = "Raytracing support could not be determined (feature query failed).";
    }
    return v;
}

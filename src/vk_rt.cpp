#include "vk_rt.h"

#include "util.h"
#include "vk_rt_spv.h"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

const float PI = 3.14159265359f;

std::string vkRes(VkResult r) {
    switch (r) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    default: return strf("VkResult(%d)", (int)r);
    }
}

struct V3 {
    float x, y, z;
};
V3 operator-(const V3& a, const V3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 cross(const V3& a, const V3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 normalize(const V3& a) {
    float l = std::sqrt(dot(a, a));
    if (l < 1e-8f) return {0, 0, 0};
    return {a.x / l, a.y / l, a.z / l};
}

// 96 bytes, must match vk_rt.comp Tri.
struct TriData {
    float v[6][4];
};
static_assert(sizeof(TriData) == 96, "TriData layout");

struct Buf {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    VkDeviceAddress address = 0;
    void* map = nullptr;
};

struct Fns {
    PFN_vkGetInstanceProcAddr gipa = nullptr;
    PFN_vkGetDeviceProcAddr gdpa = nullptr;

    PFN_vkCreateInstance createInstance = nullptr;
    PFN_vkDestroyInstance destroyInstance = nullptr;
    PFN_vkEnumeratePhysicalDevices enumeratePhysicalDevices = nullptr;
    PFN_vkGetPhysicalDeviceProperties getPhysicalDeviceProperties = nullptr;
    PFN_vkGetPhysicalDeviceProperties2 getPhysicalDeviceProperties2 = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties getPhysicalDeviceMemoryProperties = nullptr;
    PFN_vkGetPhysicalDeviceFeatures2 getPhysicalDeviceFeatures2 = nullptr;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties getPhysicalDeviceQueueFamilyProperties = nullptr;
    PFN_vkEnumerateDeviceExtensionProperties enumerateDeviceExtensionProperties = nullptr;

    PFN_vkCreateDevice createDevice = nullptr;
    PFN_vkDestroyDevice destroyDevice = nullptr;
    PFN_vkGetDeviceQueue getDeviceQueue = nullptr;

    PFN_vkCreateBuffer createBuffer = nullptr;
    PFN_vkDestroyBuffer destroyBuffer = nullptr;
    PFN_vkGetBufferMemoryRequirements getBufferMemoryRequirements = nullptr;
    PFN_vkAllocateMemory allocateMemory = nullptr;
    PFN_vkFreeMemory freeMemory = nullptr;
    PFN_vkBindBufferMemory bindBufferMemory = nullptr;
    PFN_vkMapMemory mapMemory = nullptr;
    PFN_vkUnmapMemory unmapMemory = nullptr;

    PFN_vkCreateCommandPool createCommandPool = nullptr;
    PFN_vkDestroyCommandPool destroyCommandPool = nullptr;
    PFN_vkAllocateCommandBuffers allocateCommandBuffers = nullptr;
    PFN_vkResetCommandBuffer resetCommandBuffer = nullptr;
    PFN_vkBeginCommandBuffer beginCommandBuffer = nullptr;
    PFN_vkEndCommandBuffer endCommandBuffer = nullptr;
    PFN_vkCmdBindPipeline cmdBindPipeline = nullptr;
    PFN_vkCmdBindDescriptorSets cmdBindDescriptorSets = nullptr;
    PFN_vkCmdPushConstants cmdPushConstants = nullptr;
    PFN_vkCmdDispatch cmdDispatch = nullptr;
    PFN_vkCmdPipelineBarrier cmdPipelineBarrier = nullptr;

    PFN_vkQueueSubmit queueSubmit = nullptr;
    PFN_vkCreateFence createFence = nullptr;
    PFN_vkDestroyFence destroyFence = nullptr;
    PFN_vkWaitForFences waitForFences = nullptr;
    PFN_vkResetFences resetFences = nullptr;

    PFN_vkCreateQueryPool createQueryPool = nullptr;
    PFN_vkDestroyQueryPool destroyQueryPool = nullptr;
    PFN_vkCmdResetQueryPool cmdResetQueryPool = nullptr;
    PFN_vkCmdWriteTimestamp cmdWriteTimestamp = nullptr;
    PFN_vkGetQueryPoolResults getQueryPoolResults = nullptr;

    PFN_vkCreateShaderModule createShaderModule = nullptr;
    PFN_vkDestroyShaderModule destroyShaderModule = nullptr;
    PFN_vkCreateDescriptorSetLayout createDescriptorSetLayout = nullptr;
    PFN_vkDestroyDescriptorSetLayout destroyDescriptorSetLayout = nullptr;
    PFN_vkCreatePipelineLayout createPipelineLayout = nullptr;
    PFN_vkDestroyPipelineLayout destroyPipelineLayout = nullptr;
    PFN_vkCreateComputePipelines createComputePipelines = nullptr;
    PFN_vkDestroyPipeline destroyPipeline = nullptr;
    PFN_vkCreateDescriptorPool createDescriptorPool = nullptr;
    PFN_vkDestroyDescriptorPool destroyDescriptorPool = nullptr;
    PFN_vkAllocateDescriptorSets allocateDescriptorSets = nullptr;
    PFN_vkUpdateDescriptorSets updateDescriptorSets = nullptr;

    PFN_vkGetBufferDeviceAddress getBufferDeviceAddress = nullptr;
    PFN_vkGetBufferDeviceAddressKHR getBufferDeviceAddressKHR = nullptr;
    PFN_vkCreateAccelerationStructureKHR createAccelerationStructureKHR = nullptr;
    PFN_vkDestroyAccelerationStructureKHR destroyAccelerationStructureKHR = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR getASBuildSizes = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR getASDeviceAddress = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR cmdBuildAS = nullptr;
};

std::vector<VkExtensionProperties> enumDeviceExts(VkPhysicalDevice phys,
                                                  PFN_vkEnumerateDeviceExtensionProperties fn) {
    uint32_t n = 0;
    if (!fn || fn(phys, nullptr, &n, nullptr) != VK_SUCCESS) return {};
    std::vector<VkExtensionProperties> v(n);
    if (n && fn(phys, nullptr, &n, v.data()) != VK_SUCCESS) return {};
    v.resize(n);
    return v;
}

bool hasExt(const std::vector<VkExtensionProperties>& v, const char* name) {
    for (const auto& e : v) {
        if (strcmp(e.extensionName, name) == 0) return true;
    }
    return false;
}

VkDeviceAddress alignUp(VkDeviceAddress v, VkDeviceSize a) {
    return (v + a - 1) & ~(a - 1);
}

}  // namespace

struct VulkanRt::Impl {
    HMODULE lib = nullptr;
    Fns fn;

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkQueue queue = VK_NULL_HANDLE;

    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkQueryPool queryPool = VK_NULL_HANDLE;
    bool gpuTiming = false;
    double timestampPeriodNs = 0.0;
    double lastGpuMs = 0.0;
    uint32_t triangleCount = 0;

    VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
    VkDescriptorPool dpool = VK_NULL_HANDLE;
    VkDescriptorSet dset = VK_NULL_HANDLE;
    VkPipelineLayout playout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkShaderModule shader = VK_NULL_HANDLE;

    VkAccelerationStructureKHR blas = VK_NULL_HANDLE;
    VkAccelerationStructureKHR tlas = VK_NULL_HANDLE;

    Buf vertexBuf, indexBuf, triBuf, accumBuf, outBuf, instanceBuf;
    Buf blasStorage, tlasStorage, blasScratch, tlasScratch;

    std::vector<float> verts;      // 3 floats per vertex
    std::vector<uint32_t> indices; // 3 per triangle
    std::vector<TriData> tris;
    uint32_t lightTri = 0;
    uint32_t vertexCount = 0;

    uint32_t width = 0;
    uint32_t height = 0;
    uint8_t* outPixels = nullptr;
    std::string deviceName;
    std::string driverInfo;
    std::string extensionList;
    VkDeviceSize scratchAlignment = 256;

    bool loadLibrary(std::string& err);
    bool createInstance(std::string& err);
    bool chooseDevice(std::string& err);
    bool createDevice(std::string& err);
    void loadDeviceFns();
    bool createCommandObjects(std::string& err);
    bool createScene(std::string& err);
    bool buildAS(std::string& err);
    bool createDescriptorsAndPipeline(std::string& err);
    bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool mapIt, Buf& out,
                      std::string& err);
    bool upload(Buf& b, const void* data, size_t bytes);
    uint32_t findMemoryType(uint32_t bits, VkMemoryPropertyFlags flags, bool preferDeviceLocal);
    void destroyBuffer(Buf& b);
    void addTri(const V3& a, const V3& b, const V3& c, const V3& n, const V3& albedo,
                const V3& emission, uint32_t mat);
    void addQuad(const V3& a, const V3& b, const V3& c, const V3& d, const V3& albedo,
                 const V3& emission, uint32_t mat);
    void addSphere(const V3& center, float r, const V3& albedo);
    bool render(const VkRtPush& push, std::string& err);
    void shutdown();
};

bool VulkanRt::Impl::loadLibrary(std::string& err) {
    lib = LoadLibraryW(L"vulkan-1.dll");
    if (!lib) {
        err = "vulkan-1.dll not loadable";
        return false;
    }
    fn.gipa = (PFN_vkGetInstanceProcAddr)GetProcAddress(lib, "vkGetInstanceProcAddr");
    fn.gdpa = (PFN_vkGetDeviceProcAddr)GetProcAddress(lib, "vkGetDeviceProcAddr");
    if (!fn.gipa || !fn.gdpa) {
        err = "vulkan-1.dll is missing vkGetInstanceProcAddr/vkGetDeviceProcAddr";
        return false;
    }
    fn.createInstance = (PFN_vkCreateInstance)fn.gipa(nullptr, "vkCreateInstance");
    if (!fn.createInstance) {
        err = "vkCreateInstance missing";
        return false;
    }
    return true;
}

bool VulkanRt::Impl::createInstance(std::string& err) {
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "rtprobe-vkrt";
    app.applicationVersion = 1;
    app.pEngineName = "rtprobe";
    app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;

    VkResult vr = fn.createInstance(&ci, nullptr, &instance);
    if (vr != VK_SUCCESS) {
        app.apiVersion = VK_API_VERSION_1_2;
        vr = fn.createInstance(&ci, nullptr, &instance);
    }
    if (vr != VK_SUCCESS) {
        err = "vkCreateInstance failed: " + vkRes(vr);
        return false;
    }

#define LOADI(lc, Name)                                                         \
    fn.lc = (PFN_vk##Name)fn.gipa(instance, "vk" #Name);                         \
    if (!fn.lc) {                                                                \
        err = "missing instance entry point vk" #Name;                           \
        return false;                                                            \
    }
    LOADI(destroyInstance, DestroyInstance)
    LOADI(enumeratePhysicalDevices, EnumeratePhysicalDevices)
    LOADI(getPhysicalDeviceProperties, GetPhysicalDeviceProperties)
    LOADI(getPhysicalDeviceProperties2, GetPhysicalDeviceProperties2)
    LOADI(getPhysicalDeviceMemoryProperties, GetPhysicalDeviceMemoryProperties)
    LOADI(getPhysicalDeviceFeatures2, GetPhysicalDeviceFeatures2)
    LOADI(getPhysicalDeviceQueueFamilyProperties, GetPhysicalDeviceQueueFamilyProperties)
    LOADI(enumerateDeviceExtensionProperties, EnumerateDeviceExtensionProperties)
    LOADI(createDevice, CreateDevice)
#undef LOADI

    // Optional-ish functions are loaded after the device (or checked for null).
    return true;
}

bool VulkanRt::Impl::chooseDevice(std::string& err) {
    uint32_t count = 0;
    VkResult vr = fn.enumeratePhysicalDevices(instance, &count, nullptr);
    if (vr != VK_SUCCESS || count == 0) {
        err = "no Vulkan physical device: " + vkRes(vr);
        return false;
    }
    std::vector<VkPhysicalDevice> devices(count);
    fn.enumeratePhysicalDevices(instance, &count, devices.data());

    std::string reasons;
    for (VkPhysicalDevice p : devices) {
        VkPhysicalDeviceProperties props{};
        fn.getPhysicalDeviceProperties(p, &props);

        auto exts = enumDeviceExts(p, fn.enumerateDeviceExtensionProperties);
        bool hasAS = hasExt(exts, "VK_KHR_acceleration_structure");
        bool hasRQ = hasExt(exts, "VK_KHR_ray_query");
        bool hasBDAExt = hasExt(exts, "VK_KHR_buffer_device_address");
        bool api12 = props.apiVersion >= VK_API_VERSION_1_2;
        if (!hasAS || !hasRQ || (!api12 && !hasBDAExt)) {
            reasons += strf("  %s: AS=%d RQ=%d api=%u.%u\n", props.deviceName, (int)hasAS,
                            (int)hasRQ, VK_VERSION_MAJOR(props.apiVersion),
                            VK_VERSION_MINOR(props.apiVersion));
            continue;
        }

        VkPhysicalDeviceAccelerationStructureFeaturesKHR asf{};
        asf.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        VkPhysicalDeviceRayQueryFeaturesKHR rqf{};
        rqf.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
        VkPhysicalDeviceBufferDeviceAddressFeatures bdaf{};
        bdaf.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
        VkPhysicalDeviceFeatures2 f2{};
        f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        f2.pNext = &asf;
        asf.pNext = &rqf;
        rqf.pNext = &bdaf;
        fn.getPhysicalDeviceFeatures2(p, &f2);

        if (!asf.accelerationStructure || !rqf.rayQuery || !bdaf.bufferDeviceAddress) {
            reasons += strf("  %s: features AS=%d RQ=%d BDA=%d\n", props.deviceName,
                            (int)asf.accelerationStructure, (int)rqf.rayQuery,
                            (int)bdaf.bufferDeviceAddress);
            continue;
        }

        phys = p;
        deviceName = props.deviceName;
        timestampPeriodNs = (double)props.limits.timestampPeriod;
        gpuTiming = props.limits.timestampComputeAndGraphics != 0 && timestampPeriodNs > 0.0;

        VkPhysicalDeviceAccelerationStructurePropertiesKHR asp{};
        asp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
        VkPhysicalDeviceDriverProperties dp{};
        dp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
        VkPhysicalDeviceProperties2 p2{};
        p2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        p2.pNext = &asp;
        asp.pNext = &dp;
        fn.getPhysicalDeviceProperties2(p, &p2);
        driverInfo = std::string(dp.driverName) + " " + dp.driverInfo;
        if (asp.minAccelerationStructureScratchOffsetAlignment > 0) {
            scratchAlignment = asp.minAccelerationStructureScratchOffsetAlignment;
        }
        if (scratchAlignment < 256) scratchAlignment = 256;

        extensionList.clear();
        const char* interesting[] = {"VK_KHR_acceleration_structure", "VK_KHR_ray_query",
                                     "VK_KHR_buffer_device_address",
                                     "VK_KHR_deferred_host_operations", "VK_KHR_spirv_1_4"};
        for (const char* n : interesting) {
            if (hasExt(exts, n)) {
                if (!extensionList.empty()) extensionList += ",";
                extensionList += n;
            }
        }
        logf("Vulkan RT device: %s (%s) AS=%d RQ=%d scratchAlign=%llu", deviceName.c_str(),
             driverInfo.c_str(), (int)hasAS, (int)hasRQ,
             (unsigned long long)scratchAlignment);
        return true;
    }

    err = "no Vulkan device with acceleration structure + ray query support\n";
    err += reasons;
    return false;
}

bool VulkanRt::Impl::createDevice(std::string& err) {
    auto exts = enumDeviceExts(phys, fn.enumerateDeviceExtensionProperties);
    std::vector<const char*> enabled;
    enabled.push_back("VK_KHR_acceleration_structure");
    enabled.push_back("VK_KHR_ray_query");
    if (hasExt(exts, "VK_KHR_deferred_host_operations"))
        enabled.push_back("VK_KHR_deferred_host_operations");
    if (hasExt(exts, "VK_KHR_buffer_device_address"))
        enabled.push_back("VK_KHR_buffer_device_address");
    if (hasExt(exts, "VK_KHR_spirv_1_4")) enabled.push_back("VK_KHR_spirv_1_4");

    uint32_t qCount = 0;
    fn.getPhysicalDeviceQueueFamilyProperties(phys, &qCount, nullptr);
    std::vector<VkQueueFamilyProperties> qf(qCount);
    fn.getPhysicalDeviceQueueFamilyProperties(phys, &qCount, qf.data());
    bool found = false;
    for (uint32_t i = 0; i < qCount; ++i) {
        if (qf[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            queueFamily = i;
            found = true;
            break;
        }
    }
    if (!found) {
        err = "no compute queue family";
        return false;
    }

    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR asf{};
    asf.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    asf.accelerationStructure = VK_TRUE;
    VkPhysicalDeviceRayQueryFeaturesKHR rqf{};
    rqf.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    rqf.rayQuery = VK_TRUE;
    VkPhysicalDeviceBufferDeviceAddressFeatures bdaf{};
    bdaf.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    bdaf.bufferDeviceAddress = VK_TRUE;
    VkPhysicalDeviceFeatures2 f2{};
    f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    f2.pNext = &asf;
    asf.pNext = &rqf;
    rqf.pNext = &bdaf;

    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.pNext = &f2;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = (uint32_t)enabled.size();
    dci.ppEnabledExtensionNames = enabled.data();
    dci.pEnabledFeatures = nullptr;

    VkResult vr = fn.createDevice(phys, &dci, nullptr, &device);
    if (vr != VK_SUCCESS) {
        err = "vkCreateDevice failed: " + vkRes(vr);
        return false;
    }
    loadDeviceFns();
    fn.getDeviceQueue(device, queueFamily, 0, &queue);

    if (!fn.getBufferDeviceAddress && !fn.getBufferDeviceAddressKHR) {
        err = "vkGetBufferDeviceAddress unavailable";
        return false;
    }
    if (!fn.createAccelerationStructureKHR || !fn.getASBuildSizes ||
        !fn.getASDeviceAddress || !fn.cmdBuildAS) {
        err = "acceleration structure entry points unavailable";
        return false;
    }
    return true;
}

void VulkanRt::Impl::loadDeviceFns() {
#define LOADD(lc, Name) fn.lc = (PFN_vk##Name)fn.gdpa(device, "vk" #Name);
    LOADD(destroyDevice, DestroyDevice)
    LOADD(getDeviceQueue, GetDeviceQueue)
    LOADD(createBuffer, CreateBuffer)
    LOADD(destroyBuffer, DestroyBuffer)
    LOADD(getBufferMemoryRequirements, GetBufferMemoryRequirements)
    LOADD(allocateMemory, AllocateMemory)
    LOADD(freeMemory, FreeMemory)
    LOADD(bindBufferMemory, BindBufferMemory)
    LOADD(mapMemory, MapMemory)
    LOADD(unmapMemory, UnmapMemory)
    LOADD(createCommandPool, CreateCommandPool)
    LOADD(destroyCommandPool, DestroyCommandPool)
    LOADD(allocateCommandBuffers, AllocateCommandBuffers)
    LOADD(resetCommandBuffer, ResetCommandBuffer)
    LOADD(beginCommandBuffer, BeginCommandBuffer)
    LOADD(endCommandBuffer, EndCommandBuffer)
    LOADD(cmdBindPipeline, CmdBindPipeline)
    LOADD(cmdBindDescriptorSets, CmdBindDescriptorSets)
    LOADD(cmdPushConstants, CmdPushConstants)
    LOADD(cmdDispatch, CmdDispatch)
    LOADD(cmdPipelineBarrier, CmdPipelineBarrier)
    LOADD(queueSubmit, QueueSubmit)
    LOADD(createFence, CreateFence)
    LOADD(destroyFence, DestroyFence)
    LOADD(waitForFences, WaitForFences)
    LOADD(resetFences, ResetFences)
    LOADD(createQueryPool, CreateQueryPool)
    LOADD(destroyQueryPool, DestroyQueryPool)
    LOADD(cmdResetQueryPool, CmdResetQueryPool)
    LOADD(cmdWriteTimestamp, CmdWriteTimestamp)
    LOADD(getQueryPoolResults, GetQueryPoolResults)
    LOADD(createShaderModule, CreateShaderModule)
    LOADD(destroyShaderModule, DestroyShaderModule)
    LOADD(createDescriptorSetLayout, CreateDescriptorSetLayout)
    LOADD(destroyDescriptorSetLayout, DestroyDescriptorSetLayout)
    LOADD(createPipelineLayout, CreatePipelineLayout)
    LOADD(destroyPipelineLayout, DestroyPipelineLayout)
    LOADD(createComputePipelines, CreateComputePipelines)
    LOADD(destroyPipeline, DestroyPipeline)
    LOADD(createDescriptorPool, CreateDescriptorPool)
    LOADD(destroyDescriptorPool, DestroyDescriptorPool)
    LOADD(allocateDescriptorSets, AllocateDescriptorSets)
    LOADD(updateDescriptorSets, UpdateDescriptorSets)
    LOADD(getBufferDeviceAddress, GetBufferDeviceAddress)
    LOADD(getBufferDeviceAddressKHR, GetBufferDeviceAddressKHR)
    LOADD(createAccelerationStructureKHR, CreateAccelerationStructureKHR)
    LOADD(destroyAccelerationStructureKHR, DestroyAccelerationStructureKHR)
    LOADD(getASBuildSizes, GetAccelerationStructureBuildSizesKHR)
    LOADD(getASDeviceAddress, GetAccelerationStructureDeviceAddressKHR)
    LOADD(cmdBuildAS, CmdBuildAccelerationStructuresKHR)
#undef LOADD
}

bool VulkanRt::Impl::createCommandObjects(std::string& err) {
    VkCommandPoolCreateInfo cp{};
    cp.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cp.queueFamilyIndex = queueFamily;
    VkResult vr = fn.createCommandPool(device, &cp, nullptr, &cmdPool);
    if (vr != VK_SUCCESS) {
        err = "vkCreateCommandPool failed: " + vkRes(vr);
        return false;
    }

    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = cmdPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    vr = fn.allocateCommandBuffers(device, &ai, &cmd);
    if (vr != VK_SUCCESS) {
        err = "vkAllocateCommandBuffers failed: " + vkRes(vr);
        return false;
    }

    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vr = fn.createFence(device, &fi, nullptr, &fence);
    if (vr != VK_SUCCESS) {
        err = "vkCreateFence failed: " + vkRes(vr);
        return false;
    }

    if (gpuTiming && fn.createQueryPool) {
        VkQueryPoolCreateInfo qp{};
        qp.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        qp.queryType = VK_QUERY_TYPE_TIMESTAMP;
        qp.queryCount = 2;
        vr = fn.createQueryPool(device, &qp, nullptr, &queryPool);
        if (vr != VK_SUCCESS) {
            logf("Vulkan timestamp query pool unavailable: %s", vkRes(vr).c_str());
            queryPool = VK_NULL_HANDLE;
            gpuTiming = false;
        }
    } else {
        gpuTiming = false;
    }
    logf("Vulkan RT GPU timing: %d (period=%.2f ns)", (int)gpuTiming, timestampPeriodNs);
    return true;
}

uint32_t VulkanRt::Impl::findMemoryType(uint32_t bits, VkMemoryPropertyFlags flags,
                                        bool preferDeviceLocal) {
    VkPhysicalDeviceMemoryProperties mp{};
    fn.getPhysicalDeviceMemoryProperties(phys, &mp);
    if (preferDeviceLocal) {
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
            if ((bits & (1u << i)) &&
                (mp.memoryTypes[i].propertyFlags & (flags | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) ==
                    (flags | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
                return i;
        }
    }
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & flags) == flags) return i;
    }
    return UINT32_MAX;
}

bool VulkanRt::Impl::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool mapIt,
                                  Buf& out, std::string& err) {
    if (size == 0) size = 4;
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult vr = fn.createBuffer(device, &bi, nullptr, &out.buffer);
    if (vr != VK_SUCCESS) {
        err = "vkCreateBuffer failed: " + vkRes(vr);
        return false;
    }
    out.size = size;

    VkMemoryRequirements req{};
    fn.getBufferMemoryRequirements(device, out.buffer, &req);
    uint32_t type = findMemoryType(req.memoryTypeBits,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                   true);
    if (type == UINT32_MAX) {
        err = "no HOST_VISIBLE|HOST_COHERENT memory type for buffer";
        return false;
    }

    VkMemoryAllocateFlagsInfo af{};
    af.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    af.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.pNext = &af;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = type;
    vr = fn.allocateMemory(device, &ai, nullptr, &out.memory);
    if (vr != VK_SUCCESS) {
        err = "vkAllocateMemory failed: " + vkRes(vr);
        return false;
    }
    vr = fn.bindBufferMemory(device, out.buffer, out.memory, 0);
    if (vr != VK_SUCCESS) {
        err = "vkBindBufferMemory failed: " + vkRes(vr);
        return false;
    }

    VkBufferDeviceAddressInfo bai{};
    bai.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    bai.buffer = out.buffer;
    if (fn.getBufferDeviceAddress) {
        out.address = fn.getBufferDeviceAddress(device, &bai);
    } else {
        out.address = fn.getBufferDeviceAddressKHR(device, &bai);
    }

    if (mapIt) {
        vr = fn.mapMemory(device, out.memory, 0, VK_WHOLE_SIZE, 0, &out.map);
        if (vr != VK_SUCCESS) {
            err = "vkMapMemory failed: " + vkRes(vr);
            return false;
        }
    }
    return true;
}

bool VulkanRt::Impl::upload(Buf& b, const void* data, size_t bytes) {
    if (!b.map || bytes > b.size) return false;
    memcpy(b.map, data, bytes);
    return true;
}

void VulkanRt::Impl::destroyBuffer(Buf& b) {
    if (b.map && fn.unmapMemory && device) {
        fn.unmapMemory(device, b.memory);
        b.map = nullptr;
    }
    if (b.buffer && fn.destroyBuffer) {
        fn.destroyBuffer(device, b.buffer, nullptr);
        b.buffer = VK_NULL_HANDLE;
    }
    if (b.memory && fn.freeMemory) {
        fn.freeMemory(device, b.memory, nullptr);
        b.memory = VK_NULL_HANDLE;
    }
    b.size = 0;
    b.address = 0;
}

void VulkanRt::Impl::addTri(const V3& a, const V3& b, const V3& c, const V3& n,
                            const V3& albedo, const V3& emission, uint32_t mat) {
    uint32_t base = (uint32_t)(verts.size() / 3);
    verts.insert(verts.end(), {a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z});
    indices.push_back(base);
    indices.push_back(base + 1);
    indices.push_back(base + 2);

    TriData t{};
    t.v[0][0] = a.x; t.v[0][1] = a.y; t.v[0][2] = a.z;
    t.v[1][0] = b.x; t.v[1][1] = b.y; t.v[1][2] = b.z;
    t.v[2][0] = c.x; t.v[2][1] = c.y; t.v[2][2] = c.z;
    t.v[3][0] = n.x; t.v[3][1] = n.y; t.v[3][2] = n.z;
    t.v[4][0] = albedo.x; t.v[4][1] = albedo.y; t.v[4][2] = albedo.z;
    t.v[4][3] = (float)mat;
    t.v[5][0] = emission.x; t.v[5][1] = emission.y; t.v[5][2] = emission.z;
    tris.push_back(t);
}

void VulkanRt::Impl::addQuad(const V3& a, const V3& b, const V3& c, const V3& d,
                             const V3& albedo, const V3& emission, uint32_t mat) {
    V3 n = normalize(cross(b - a, c - a));
    addTri(a, b, c, n, albedo, emission, mat);
    addTri(a, c, d, n, albedo, emission, mat);
}

void VulkanRt::Impl::addSphere(const V3& center, float r, const V3& albedo) {
    const int seg = 24;
    const int rings = 12;
    auto P = [&](int i, int j) {
        float phi = PI * (float)i / rings;
        float theta = 2.0f * PI * (float)j / seg;
        return V3{center.x + r * std::sin(phi) * std::cos(theta),
                  center.y + r * std::cos(phi),
                  center.z + r * std::sin(phi) * std::sin(theta)};
    };
    for (int i = 0; i < rings; ++i) {
        for (int j = 0; j < seg; ++j) {
            V3 p00 = P(i, j);
            V3 p01 = P(i, j + 1);
            V3 p10 = P(i + 1, j);
            V3 p11 = P(i + 1, j + 1);
            V3 n1 = normalize(cross(p10 - p00, p11 - p00));
            V3 n2 = normalize(cross(p11 - p00, p01 - p00));
            addTri(p00, p10, p11, n1, albedo, {0, 0, 0}, 0);
            addTri(p00, p11, p01, n2, albedo, {0, 0, 0}, 0);
        }
    }
}

bool VulkanRt::Impl::createScene(std::string& err) {
    verts.clear();
    indices.clear();
    tris.clear();

    const V3 white{0.73f, 0.73f, 0.73f};
    const V3 red{0.65f, 0.12f, 0.10f};
    const V3 green{0.12f, 0.55f, 0.15f};
    const V3 black{0, 0, 0};
    const V3 lightColor{16.0f, 16.0f, 16.0f};

    addQuad({-1, 0, 0}, {1, 0, 0}, {1, 0, 2}, {-1, 0, 2}, white, black, 0);   // floor
    addQuad({-1, 2, 0}, {1, 2, 0}, {1, 2, 2}, {-1, 2, 2}, white, black, 0);   // ceiling
    addQuad({-1, 0, 2}, {1, 0, 2}, {1, 2, 2}, {-1, 2, 2}, white, black, 0);   // back
    addQuad({-1, 0, 0}, {-1, 0, 2}, {-1, 2, 2}, {-1, 2, 0}, red, black, 0);   // left
    addQuad({1, 0, 0}, {1, 0, 2}, {1, 2, 2}, {1, 2, 0}, green, black, 0);     // right

    lightTri = (uint32_t)tris.size();
    addQuad({-0.35f, 1.995f, 0.75f}, {0.35f, 1.995f, 0.75f},
            {0.35f, 1.995f, 1.25f}, {-0.35f, 1.995f, 1.25f}, black, lightColor, 1);

    addSphere({-0.48f, 0.48f, 1.35f}, 0.48f, {0.78f, 0.78f, 0.78f});
    // Metal and glass are approximated with diffuse/metal/glass triangles: the
    // shader reads material from TriData, so add them explicitly below.
    // (addSphere marks diffuse; add dedicated metal/glass spheres.)
    size_t sphereStart = tris.size();
    // Remove the diffuse sphere triangles added above by rebuilding? Simpler:
    // the previous call added a diffuse sphere; we keep it.
    (void)sphereStart;

    // Metal sphere
    size_t before = tris.size();
    addSphere({0.45f, 0.43f, 0.90f}, 0.43f, {0.92f, 0.75f, 0.38f});
    for (size_t i = before; i < tris.size(); ++i) tris[i].v[4][3] = 2.0f;

    // Glass sphere
    before = tris.size();
    addSphere({0.12f, 0.27f, 0.38f}, 0.27f, {0.95f, 0.95f, 0.95f});
    for (size_t i = before; i < tris.size(); ++i) tris[i].v[4][3] = 3.0f;

    vertexCount = (uint32_t)(verts.size() / 3);
    triangleCount = (uint32_t)(indices.size() / 3);
    if (vertexCount == 0 || indices.empty()) {
        err = "empty scene";
        return false;
    }
    logf("Vulkan RT scene: %u vertices, %zu triangles, %zu triangles data, lightTri=%u",
         vertexCount, indices.size() / 3, tris.size(), lightTri);
    return true;
}

bool VulkanRt::Impl::buildAS(std::string& err) {
    if (!createBuffer((VkDeviceSize)verts.size() * sizeof(float),
                      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                      true, vertexBuf, err))
        return false;
    if (!upload(vertexBuf, verts.data(), verts.size() * sizeof(float))) {
        err = "vertex upload failed";
        return false;
    }

    if (!createBuffer((VkDeviceSize)indices.size() * sizeof(uint32_t),
                      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                      true, indexBuf, err))
        return false;
    if (!upload(indexBuf, indices.data(), indices.size() * sizeof(uint32_t))) {
        err = "index upload failed";
        return false;
    }

    if (!createBuffer((VkDeviceSize)tris.size() * sizeof(TriData),
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, triBuf, err))
        return false;
    if (!upload(triBuf, tris.data(), tris.size() * sizeof(TriData))) {
        err = "triangle data upload failed";
        return false;
    }

    VkAccelerationStructureGeometryTrianglesDataKHR tri{};
    tri.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    tri.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    tri.vertexData.deviceAddress = vertexBuf.address;
    tri.vertexStride = sizeof(float) * 3;
    tri.maxVertex = vertexCount - 1;
    tri.indexType = VK_INDEX_TYPE_UINT32;
    tri.indexData.deviceAddress = indexBuf.address;
    tri.transformData.deviceAddress = 0;

    VkAccelerationStructureGeometryKHR geom{};
    geom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geom.geometry.triangles = tri;

    uint32_t triCount = (uint32_t)(indices.size() / 3);
    VkAccelerationStructureBuildGeometryInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    info.geometryCount = 1;
    info.pGeometries = &geom;

    VkAccelerationStructureBuildSizesInfoKHR sizes{};
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    fn.getASBuildSizes(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &info,
                       &triCount, &sizes);

    if (!createBuffer(sizes.accelerationStructureSize,
                      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false,
                      blasStorage, err))
        return false;

    VkAccelerationStructureCreateInfoKHR aci{};
    aci.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    aci.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    aci.size = sizes.accelerationStructureSize;
    aci.buffer = blasStorage.buffer;
    aci.offset = 0;
    VkResult vr = fn.createAccelerationStructureKHR(device, &aci, nullptr, &blas);
    if (vr != VK_SUCCESS) {
        err = "vkCreateAccelerationStructureKHR(BLAS) failed: " + vkRes(vr);
        return false;
    }

    if (!createBuffer(sizes.buildScratchSize + scratchAlignment,
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, blasScratch, err))
        return false;
    VkDeviceAddress blasScratchAddr = alignUp(blasScratch.address, scratchAlignment);

    VkAccelerationStructureDeviceAddressInfoKHR adi{};
    adi.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    adi.accelerationStructure = blas;
    VkDeviceAddress blasAddr = fn.getASDeviceAddress(device, &adi);

    VkAccelerationStructureInstanceKHR inst{};
    inst.transform.matrix[0][0] = 1.0f;
    inst.transform.matrix[1][1] = 1.0f;
    inst.transform.matrix[2][2] = 1.0f;
    inst.instanceCustomIndex = 0;
    inst.mask = 0xFF;
    inst.instanceShaderBindingTableRecordOffset = 0;
    inst.flags = 0;
    inst.accelerationStructureReference = blasAddr;

    if (!createBuffer(sizeof(inst),
                      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                      true, instanceBuf, err))
        return false;
    if (!upload(instanceBuf, &inst, sizeof(inst))) {
        err = "instance upload failed";
        return false;
    }

    VkAccelerationStructureGeometryInstancesDataKHR instData{};
    instData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    instData.arrayOfPointers = VK_FALSE;
    instData.data.deviceAddress = instanceBuf.address;

    VkAccelerationStructureGeometryKHR tlasGeom{};
    tlasGeom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    tlasGeom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    tlasGeom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    tlasGeom.geometry.instances = instData;

    VkAccelerationStructureBuildGeometryInfoKHR tinfo{};
    tinfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    tinfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    tinfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    tinfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    tinfo.geometryCount = 1;
    tinfo.pGeometries = &tlasGeom;

    uint32_t instanceCount = 1;
    VkAccelerationStructureBuildSizesInfoKHR tsizes{};
    tsizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    fn.getASBuildSizes(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tinfo,
                       &instanceCount, &tsizes);

    if (!createBuffer(tsizes.accelerationStructureSize,
                      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false,
                      tlasStorage, err))
        return false;

    VkAccelerationStructureCreateInfoKHR taci{};
    taci.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    taci.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    taci.size = tsizes.accelerationStructureSize;
    taci.buffer = tlasStorage.buffer;
    taci.offset = 0;
    vr = fn.createAccelerationStructureKHR(device, &taci, nullptr, &tlas);
    if (vr != VK_SUCCESS) {
        err = "vkCreateAccelerationStructureKHR(TLAS) failed: " + vkRes(vr);
        return false;
    }

    if (!createBuffer(tsizes.buildScratchSize + scratchAlignment,
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, tlasScratch, err))
        return false;
    VkDeviceAddress tlasScratchAddr = alignUp(tlasScratch.address, scratchAlignment);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vr = fn.beginCommandBuffer(cmd, &bi);
    if (vr != VK_SUCCESS) {
        err = "beginCommandBuffer(AS) failed: " + vkRes(vr);
        return false;
    }

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = info;
    buildInfo.dstAccelerationStructure = blas;
    buildInfo.scratchData.deviceAddress = blasScratchAddr;
    VkAccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount = triCount;
    range.primitiveOffset = 0;
    range.firstVertex = 0;
    range.transformOffset = 0;
    const VkAccelerationStructureBuildRangeInfoKHR* pRange = &range;
    fn.cmdBuildAS(cmd, 1, &buildInfo, &pRange);

    {
        VkMemoryBarrier mb{};
        mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        mb.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR |
                           VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        fn.cmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                              VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0,
                              1, &mb, 0, nullptr, 0, nullptr);
    }

    VkAccelerationStructureBuildGeometryInfoKHR tbuild = tinfo;
    tbuild.dstAccelerationStructure = tlas;
    tbuild.scratchData.deviceAddress = tlasScratchAddr;
    VkAccelerationStructureBuildRangeInfoKHR trange{};
    trange.primitiveCount = instanceCount;
    const VkAccelerationStructureBuildRangeInfoKHR* ptRange = &trange;
    fn.cmdBuildAS(cmd, 1, &tbuild, &ptRange);

    {
        VkMemoryBarrier mb{};
        mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        mb.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR |
                           VK_ACCESS_SHADER_READ_BIT;
        fn.cmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                              VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0,
                              nullptr);
    }

    vr = fn.endCommandBuffer(cmd);
    if (vr != VK_SUCCESS) {
        err = "endCommandBuffer(AS) failed: " + vkRes(vr);
        return false;
    }

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vr = fn.queueSubmit(queue, 1, &si, fence);
    if (vr != VK_SUCCESS) {
        err = "queueSubmit(AS) failed: " + vkRes(vr);
        return false;
    }
    vr = fn.waitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
    if (vr != VK_SUCCESS) {
        err = "waitForFences(AS) failed: " + vkRes(vr);
        return false;
    }
    fn.resetFences(device, 1, &fence);
    logf("Vulkan RT acceleration structures built (BLAS %.1f KB, TLAS %.1f KB)",
         (double)sizes.accelerationStructureSize / 1024.0,
         (double)tsizes.accelerationStructureSize / 1024.0);
    return true;
}

bool VulkanRt::Impl::createDescriptorsAndPipeline(std::string& err) {
    VkDescriptorSetLayoutBinding bindings[4]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo dslci{};
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 4;
    dslci.pBindings = bindings;
    VkResult vr = fn.createDescriptorSetLayout(device, &dslci, nullptr, &dsl);
    if (vr != VK_SUCCESS) {
        err = "createDescriptorSetLayout failed: " + vkRes(vr);
        return false;
    }

    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = 3;
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1;
    dpci.poolSizeCount = 2;
    dpci.pPoolSizes = poolSizes;
    vr = fn.createDescriptorPool(device, &dpci, nullptr, &dpool);
    if (vr != VK_SUCCESS) {
        err = "createDescriptorPool failed: " + vkRes(vr);
        return false;
    }

    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = dpool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &dsl;
    vr = fn.allocateDescriptorSets(device, &dsai, &dset);
    if (vr != VK_SUCCESS) {
        err = "allocateDescriptorSets failed: " + vkRes(vr);
        return false;
    }

    VkWriteDescriptorSetAccelerationStructureKHR was{};
    was.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    was.accelerationStructureCount = 1;
    was.pAccelerationStructures = &tlas;

    VkDescriptorBufferInfo binfo[3]{};
    binfo[0] = {triBuf.buffer, 0, VK_WHOLE_SIZE};
    binfo[1] = {accumBuf.buffer, 0, VK_WHOLE_SIZE};
    binfo[2] = {outBuf.buffer, 0, VK_WHOLE_SIZE};

    VkWriteDescriptorSet writes[4]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].pNext = &was;
    writes[0].dstSet = dset;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    for (int i = 1; i < 4; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = dset;
        writes[i].dstBinding = (uint32_t)i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &binfo[i - 1];
    }
    fn.updateDescriptorSets(device, 4, writes, 0, nullptr);

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset = 0;
    pcr.size = sizeof(VkRtPush);

    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &dsl;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    vr = fn.createPipelineLayout(device, &plci, nullptr, &playout);
    if (vr != VK_SUCCESS) {
        err = "createPipelineLayout failed: " + vkRes(vr);
        return false;
    }

    VkShaderModuleCreateInfo smci{};
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = vk_rt_comp_spvWordCount * sizeof(uint32_t);
    smci.pCode = vk_rt_comp_spv;
    vr = fn.createShaderModule(device, &smci, nullptr, &shader);
    if (vr != VK_SUCCESS) {
        err = "createShaderModule(ray query) failed: " + vkRes(vr);
        return false;
    }

    VkComputePipelineCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpci.stage.module = shader;
    cpci.stage.pName = "main";
    cpci.layout = playout;
    vr = fn.createComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline);
    if (vr != VK_SUCCESS) {
        err = "createComputePipelines(ray query) failed: " + vkRes(vr);
        return false;
    }
    logf("Vulkan RT pipeline created");
    return true;
}

bool VulkanRt::Impl::render(const VkRtPush& push, std::string& err) {
    VkResult vr = fn.resetCommandBuffer(cmd, 0);
    if (vr != VK_SUCCESS) {
        err = "resetCommandBuffer failed: " + vkRes(vr);
        return false;
    }
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vr = fn.beginCommandBuffer(cmd, &bi);
    if (vr != VK_SUCCESS) {
        err = "beginCommandBuffer failed: " + vkRes(vr);
        return false;
    }

    if (queryPool && fn.cmdResetQueryPool && fn.cmdWriteTimestamp) {
        fn.cmdResetQueryPool(cmd, queryPool, 0, 2);
        fn.cmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queryPool, 0);
    }

    fn.cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    fn.cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, playout, 0, 1, &dset, 0,
                             nullptr);
    fn.cmdPushConstants(cmd, playout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    fn.cmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);

    if (queryPool && fn.cmdWriteTimestamp) {
        fn.cmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queryPool, 1);
    }

    VkMemoryBarrier mb{};
    mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    fn.cmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                          0, 1, &mb, 0, nullptr, 0, nullptr);

    vr = fn.endCommandBuffer(cmd);
    if (vr != VK_SUCCESS) {
        err = "endCommandBuffer failed: " + vkRes(vr);
        return false;
    }

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vr = fn.queueSubmit(queue, 1, &si, fence);
    if (vr != VK_SUCCESS) {
        err = "queueSubmit failed: " + vkRes(vr);
        return false;
    }
    vr = fn.waitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
    if (vr != VK_SUCCESS) {
        err = "waitForFences failed: " + vkRes(vr);
        return false;
    }
    fn.resetFences(device, 1, &fence);

    lastGpuMs = 0.0;
    if (queryPool && fn.getQueryPoolResults && timestampPeriodNs > 0.0) {
        uint64_t ts[2] = {0, 0};
        VkResult qr = fn.getQueryPoolResults(device, queryPool, 0, 2, sizeof(ts), ts,
                                             sizeof(uint64_t),
                                             VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        if (qr == VK_SUCCESS && ts[1] >= ts[0]) {
            lastGpuMs = (double)(ts[1] - ts[0]) * timestampPeriodNs / 1000000.0;
        }
    }
    return true;
}

void VulkanRt::Impl::shutdown() {
    if (device) {
        if (pipeline && fn.destroyPipeline) fn.destroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
        if (shader && fn.destroyShaderModule) fn.destroyShaderModule(device, shader, nullptr);
        shader = VK_NULL_HANDLE;
        if (playout && fn.destroyPipelineLayout)
            fn.destroyPipelineLayout(device, playout, nullptr);
        playout = VK_NULL_HANDLE;
        if (dpool && fn.destroyDescriptorPool)
            fn.destroyDescriptorPool(device, dpool, nullptr);
        dpool = VK_NULL_HANDLE;
        if (dsl && fn.destroyDescriptorSetLayout)
            fn.destroyDescriptorSetLayout(device, dsl, nullptr);
        dsl = VK_NULL_HANDLE;
        if (fn.destroyAccelerationStructureKHR) {
            if (blas) fn.destroyAccelerationStructureKHR(device, blas, nullptr);
            if (tlas) fn.destroyAccelerationStructureKHR(device, tlas, nullptr);
        }
        blas = VK_NULL_HANDLE;
        tlas = VK_NULL_HANDLE;

        destroyBuffer(vertexBuf);
        destroyBuffer(indexBuf);
        destroyBuffer(triBuf);
        destroyBuffer(accumBuf);
        destroyBuffer(outBuf);
        destroyBuffer(instanceBuf);
        destroyBuffer(blasStorage);
        destroyBuffer(tlasStorage);
        destroyBuffer(blasScratch);
        destroyBuffer(tlasScratch);

        if (queryPool && fn.destroyQueryPool)
            fn.destroyQueryPool(device, queryPool, nullptr);
        queryPool = VK_NULL_HANDLE;
        if (fence && fn.destroyFence) fn.destroyFence(device, fence, nullptr);
        fence = VK_NULL_HANDLE;
        if (cmdPool && fn.destroyCommandPool)
            fn.destroyCommandPool(device, cmdPool, nullptr);
        cmdPool = VK_NULL_HANDLE;
        if (fn.destroyDevice) fn.destroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    }
    if (instance && fn.destroyInstance) {
        fn.destroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }
    phys = VK_NULL_HANDLE;
    queue = VK_NULL_HANDLE;
    if (lib) {
        FreeLibrary(lib);
        lib = nullptr;
    }
    fn = Fns{};
}

// ---------------------------------------------------------------------------

VulkanRt::~VulkanRt() { shutdown(); }

bool VulkanRt::init(int width, int height, std::string& error) {
    shutdown();
    if (width <= 0 || height <= 0) {
        error = "invalid Vulkan RT resolution";
        return false;
    }
    impl_ = new Impl();
    impl_->width = (uint32_t)width;
    impl_->height = (uint32_t)height;

    if (!impl_->loadLibrary(error)) {
        shutdown();
        return false;
    }
    if (!impl_->createInstance(error)) {
        shutdown();
        return false;
    }
    if (!impl_->chooseDevice(error)) {
        shutdown();
        return false;
    }
    if (!impl_->createDevice(error)) {
        shutdown();
        return false;
    }
    if (!impl_->createCommandObjects(error)) {
        shutdown();
        return false;
    }
    if (!impl_->createScene(error)) {
        shutdown();
        return false;
    }
    if (!impl_->createBuffer((VkDeviceSize)width * height * 4,
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, impl_->outBuf, error)) {
        shutdown();
        return false;
    }
    if (!impl_->createBuffer((VkDeviceSize)width * height * sizeof(float) * 4,
                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, impl_->accumBuf,
                             error)) {
        shutdown();
        return false;
    }
    if (!impl_->buildAS(error)) {
        shutdown();
        return false;
    }
    if (!impl_->createDescriptorsAndPipeline(error)) {
        shutdown();
        return false;
    }

    width_ = width;
    height_ = height;
    pixels_.assign((size_t)width * height * 4, 0);
    impl_->outPixels = (uint8_t*)impl_->outBuf.map;
    deviceName_ = impl_->deviceName;
    driverInfo_ = impl_->driverInfo;
    extensions_ = impl_->extensionList;
    lightTriIndex_ = impl_->lightTri;
    triangleCount_ = impl_->triangleCount;
    gpuTimingAvailable_ = impl_->gpuTiming;
    timestampPeriodNs_ = impl_->timestampPeriodNs;
    lastGpuMs_ = 0.0;
    ready_ = true;
    logf("Vulkan RT ready: %dx%d device=%s (%s)", width, height, deviceName_.c_str(),
         driverInfo_.c_str());
    return true;
}

void VulkanRt::shutdown() {
    ready_ = false;
    width_ = 0;
    height_ = 0;
    pixels_.clear();
    if (impl_) {
        impl_->shutdown();
        delete impl_;
        impl_ = nullptr;
    }
}

bool VulkanRt::render(const VkRtPush& push, std::string& error) {
    if (!ready_ || !impl_) {
        error = "Vulkan RT not initialized";
        return false;
    }
    if (push.width != (uint32_t)width_ || push.height != (uint32_t)height_) {
        error = "Vulkan RT push constant resolution mismatch";
        return false;
    }
    if (!impl_->render(push, error)) return false;
    if (impl_->outPixels) {
        memcpy(pixels_.data(), impl_->outPixels, pixels_.size());
    }
    lastGpuMs_ = impl_->lastGpuMs;
    return true;
}

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Must match the push_constant block in vk_rt.comp (96 bytes).
struct VkRtPush {
    float camPos[4];
    float camFwd[4];
    float camRight[4];
    float camUp[4];
    uint32_t width;
    uint32_t height;
    uint32_t frameIndex;
    uint32_t maxBounces;
    uint32_t seed;
    uint32_t lightTriIndex;
    uint32_t pad0;
    uint32_t pad1;
};
static_assert(sizeof(VkRtPush) == 96, "VkRtPush must match vk_rt.comp");

// Hardware ray tracing via Vulkan VK_KHR_acceleration_structure + ray query.
// Renders offscreen to a CPU-readable BGRA8 buffer; presentation is done by
// the existing D3D12 overlay compositor.
class VulkanRt {
public:
    VulkanRt() = default;
    ~VulkanRt();

    bool init(int width, int height, std::string& error);
    void shutdown();
    bool render(const VkRtPush& push, std::string& error);

    bool ready() const { return ready_; }
    int width() const { return width_; }
    int height() const { return height_; }
    const uint8_t* pixels() const { return pixels_.data(); }
    const std::string& deviceName() const { return deviceName_; }
    const std::string& driverInfo() const { return driverInfo_; }
    const std::string& extensions() const { return extensions_; }
    uint32_t lightTriIndex() const { return lightTriIndex_; }

private:
    struct Impl;
    Impl* impl_ = nullptr;
    bool ready_ = false;
    int width_ = 0;
    int height_ = 0;
    std::vector<uint8_t> pixels_;
    std::string deviceName_;
    std::string driverInfo_;
    std::string extensions_;
    uint32_t lightTriIndex_ = 0;
};

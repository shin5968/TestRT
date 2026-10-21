#pragma once

#include "bench.h"
#include "d3d12_ctx.h"
#include "dxr_test.h"
#include "overlay.h"
#include "probe.h"
#include "renderer.h"
#include "scene.h"
#include "vk_rt.h"

#include <string>
#include <vector>

struct AppOptions {
    bool probeOnly = false;
    bool bench = false;
    bool benchHw = false;
    bool dxrTest = false;
    bool vulkanRt = false;
    bool vsync = false;
    bool borderless = false;
    bool debug = false;
    int seconds = 0;        // fixed-time benchmark; 0 = fixed frame count
    int frames = 600;
    int width = 1280;
    int height = 720;
};

class App {
public:
    int run(HINSTANCE hInst, const AppOptions& opts);

private:
    enum class Mode { Demo, Bench, HwRt };
    enum class BenchPhase { Warmup, Measure };

    static LRESULT CALLBACK wndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT wndProc(HWND, UINT, WPARAM, LPARAM);

    bool createWindow(HINSTANCE hInst);
    bool initGraphics();
    void collectProbe();
    void writeReport(const std::string& mode);
    void renderLoop();
    void shutdown();

    void startBenchmark();
    void finishBenchmark();
    void resetAccumulation();
    bool enterHwRt();
    void leaveHwRt();
    int runHwRtBenchmark();
    FrameCB makeFrameCB(bool accumulate) const;
    VkRtPush makeVkPush() const;
    std::vector<std::string> buildOverlayLines() const;
    void refreshOverlay();

    void applyResizeIfNeeded();
    void onKeyDown(WPARAM key);
    void onMouseMove(int x, int y);
    void onMouseWheel(int delta);
    void toggleOverlay();
    void requestScreenshot();

    static double nowMs();

    AppOptions opts_;
    HINSTANCE hInst_ = nullptr;
    HWND hwnd_ = nullptr;

    D3D12Api api_;
    D3D12Context ctx_;
    Renderer renderer_;
    Overlay overlay_;
    Scene scene_;
    ProbeResult probe_;
    VulkanRt vkRt_;
    uint32_t vkSampleIndex_ = 0;
    int vkWidth_ = 0;
    int vkHeight_ = 0;
    std::string vkError_;

    bool running_ = false;
    bool showOverlay_ = true;
    bool overlayDirty_ = true;
    bool wantScreenshot_ = false;
    bool pendingResize_ = false;
    int pendingW_ = 0;
    int pendingH_ = 0;

    Mode mode_ = Mode::Demo;
    BenchPhase benchPhase_ = BenchPhase::Warmup;
    uint32_t sampleIndex_ = 0;
    uint32_t maxBounces_ = 4;
    uint32_t benchFrame_ = 0;
    uint32_t benchMaxFrames_ = 0;
    uint32_t warmupFrames_ = 0;
    double benchWarmupSeconds_ = 10.0;
    double benchStartMs_ = 0.0;
    double measureStartMs_ = 0.0;
    std::vector<double> benchCpuMs_;
    BenchTiming benchTiming_;
    BenchResult benchResult_;
    bool benchRan_ = false;

    bool hwBenchRan_ = false;
    BenchResult hwBenchResult_;
    std::string hwBenchDevice_;
    std::string hwBenchDriver_;
    std::string hwBenchExtensions_;
    uint32_t hwBenchTriangles_ = 0;
    double hwBenchTimestampPeriodNs_ = 0.0;
    double hwBenchWarmupSeconds_ = 3.0;

    DxrTestResult dxrResult_;

    float yaw_ = 0.0f;
    float pitch_ = 0.10f;
    float distance_ = 3.2f;
    bool dragging_ = false;
    int lastMouseX_ = 0;
    int lastMouseY_ = 0;

    std::vector<double> frameMsHistory_;
    double lastOverlayRefreshMs_ = 0.0;
    double lastFrameMs_ = 0.0;
};

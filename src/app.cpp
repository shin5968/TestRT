#include "app.h"

#include "report.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace {

struct V3 {
    float x, y, z;
};
V3 operator+(const V3& a, const V3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(const V3& a, const V3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(const V3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
V3 cross(const V3& a, const V3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 normalize(const V3& a) {
    float l = std::sqrt(dot(a, a));
    if (l < 1e-8f) return {0, 0, 0};
    return {a.x / l, a.y / l, a.z / l};
}

void set4(float* d, const V3& v) {
    d[0] = v.x;
    d[1] = v.y;
    d[2] = v.z;
    d[3] = 0.0f;
}

std::string shortStr(const std::string& s, size_t n) {
    if (s.size() <= n) return s;
    return s.substr(0, n - 1) + "~";
}

}  // namespace

double App::nowMs() {
    static LARGE_INTEGER freq{};
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)freq.QuadPart;
}

int App::run(HINSTANCE hInst, const AppOptions& opts) {
    opts_ = opts;
    hInst_ = hInst;

    logf("options: probeOnly=%d bench=%d seconds=%d frames=%d size=%dx%d vsync=%d borderless=%d",
         (int)opts_.probeOnly, (int)opts_.bench, opts_.seconds, opts_.frames,
         opts_.width, opts_.height, (int)opts_.vsync, (int)opts_.borderless);

    if (opts_.probeOnly) {
        if (!api_.load()) {
            logf("probe-only: failed to load D3D12");
            writeReport("probe");
            api_.unload();
            return 2;
        }
        if (!ctx_.initDevice(&api_)) {
            writeReport("probe");
            api_.unload();
            return 2;
        }
        collectProbe();
        writeReport("probe");
        ctx_.shutdown();
        api_.unload();
        return 0;
    }

    if (!createWindow(hInst_)) {
        logf("failed to create window");
        return 1;
    }
    if (!initGraphics()) {
        MessageBoxW(hwnd_, L"rtprobe failed to initialize graphics.\nSee rtprobe.log and rtprobe-report.json.",
                    L"rtprobe error", MB_OK | MB_ICONERROR);
        writeReport("init-failed");
        shutdown();
        return 2;
    }

    if (opts_.vulkanRt) enterHwRt();
    if (opts_.bench && mode_ != Mode::HwRt) startBenchmark();

    running_ = true;
    renderLoop();
    writeReport(benchRan_ ? "interactive-bench" : "interactive");
    shutdown();
    return 0;
}

bool App::createWindow(HINSTANCE hInst) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &App::wndProcStatic;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"rtprobeWnd";
    if (!RegisterClassExW(&wc)) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            logf("RegisterClassExW failed: %lu", (unsigned long)err);
            return false;
        }
    }

    DWORD style = opts_.borderless ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    RECT r{0, 0, opts_.width, opts_.height};
    AdjustWindowRect(&r, style, FALSE);
    int ww = r.right - r.left;
    int wh = r.bottom - r.top;
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    if (opts_.borderless) {
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);
        x = (sw - ww) / 2;
        y = (sh - wh) / 2;
    }

    hwnd_ = CreateWindowExW(0, L"rtprobeWnd", L"rtprobe - Winlator RT test",
                            style, x, y, ww, wh, nullptr, nullptr, hInst, this);
    if (!hwnd_) {
        logf("CreateWindowExW failed: %lu", (unsigned long)GetLastError());
        return false;
    }
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, (LONG_PTR)this);
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);

    RECT client{};
    if (GetClientRect(hwnd_, &client)) {
        pendingW_ = client.right;
        pendingH_ = client.bottom;
        if (pendingW_ > 0 && pendingH_ > 0 &&
            (pendingW_ != opts_.width || pendingH_ != opts_.height)) {
            pendingResize_ = true;
        }
        logf("window created: %dx%d (client, requested %dx%d)", pendingW_, pendingH_,
             opts_.width, opts_.height);
    }
    return true;
}

void App::shutdown() {
    ctx_.waitForGpu();
    renderer_.destroy();
    ctx_.shutdown();
    api_.unload();
    if (hwnd_) {
        hwnd_ = nullptr;
    }
}

bool App::initGraphics() {
    if (!api_.load()) {
        logf("initGraphics: D3D12 load failed");
        return false;
    }
    if (!ctx_.initDevice(&api_)) {
        logf("initGraphics: device init failed");
        return false;
    }
    collectProbe();

    scene_.build();

    if (!renderer_.initDevice(ctx_, &api_, scene_)) {
        logf("initGraphics: renderer device resources failed");
        return false;
    }

    if (!ctx_.createSwapchain(hwnd_, opts_.width, opts_.height)) {
        logf("initGraphics: swapchain creation failed");
        return false;
    }
    ctx_.vsync = opts_.vsync;

    if (!renderer_.resize(ctx_, ctx_.width, ctx_.height)) {
        logf("initGraphics: renderer resize failed");
        return false;
    }
    if (!overlay_.resize(ctx_.width, ctx_.height)) {
        logf("initGraphics: overlay resize failed");
        return false;
    }

    refreshOverlay();
    return true;
}

void App::collectProbe() {
    probe_ = ProbeResult{};
    runD3D12Probe(ctx_.device.get(), ctx_.device5.get(), ctx_.list4.get(),
                  ctx_.adapter.get(), probe_);
    probe_.featureLevel = featureLevelStr(ctx_.featureLevel);
    probeModules(probe_);
    runVulkanProbe(probe_);
    probe_.verdict = buildVerdict(probe_);
    logf("verdict: %s", probe_.verdict.c_str());
}

void App::writeReport(const std::string& mode) {
    ReportData rd;
    rd.probe = probe_;
    rd.sceneHash = scene_.hash;
    rd.primCount = scene_.prims.size();
    rd.nodeCount = scene_.nodes.size();
    rd.sceneBuildMs = scene_.buildMs;
    rd.shaderHash = renderer_.shaderHash();
    rd.shaderCompileMs = renderer_.shaderCompileMs();
    rd.mode = mode;
    rd.benchRan = benchRan_;
    rd.bench = benchResult_;
    writeReportJSON(outPath("rtprobe-report.json"), rd);
}

void App::renderLoop() {
    double lastOverlay = 0.0;
    while (running_) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running_ = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running_) break;

        applyResizeIfNeeded();

        if (mode_ == Mode::Bench && benchPhase_ == BenchPhase::Warmup) {
            double elapsed = nowMs() - benchStartMs_;
            if (elapsed >= benchWarmupSeconds_ * 1000.0 && warmupFrames_ >= 30) {
                benchPhase_ = BenchPhase::Measure;
                benchFrame_ = 0;
                benchCpuMs_.clear();
                renderer_.benchResetCounter(ctx_);
                measureStartMs_ = nowMs();
                logf("bench: warmup complete (%.1fs, %u frames)", elapsed / 1000.0, warmupFrames_);
                refreshOverlay();
            }
        }

        double t0 = nowMs();
        if (!ctx_.beginFrame()) {
            logf("beginFrame failed; stopping");
            running_ = false;
            break;
        }

        bool hwrt = false;
        if (mode_ == Mode::HwRt) {
            if (vkRt_.ready()) {
                VkRtPush vp = makeVkPush();
                std::string verr;
                if (vkRt_.render(vp, verr)) {
                    hwrt = true;
                } else {
                    vkError_ = verr;
                    logf("Vulkan RT render failed: %s", verr.c_str());
                    mode_ = Mode::Demo;
                    overlayDirty_ = true;
                }
            } else {
                mode_ = Mode::Demo;
                overlayDirty_ = true;
            }
        }

        bool measuring = (!hwrt && mode_ == Mode::Bench &&
                          benchPhase_ == BenchPhase::Measure);
        if (measuring) renderer_.benchBeginFrame(ctx_, benchFrame_);

        bool overlayDirty = overlayDirty_;
        bool shot = wantScreenshot_;
        bool ok = false;

        if (hwrt) {
            auto lines = buildOverlayLines();
            overlay_.render(lines, vkRt_.pixels(), vkRt_.width(), vkRt_.height());
            overlayDirty_ = true;
            FrameCB cb = makeFrameCB(false);
            ok = renderer_.renderFrame(ctx_, cb, overlay_.pixels(), true, true, shot,
                                       /*skipCompute=*/true);
        } else {
            bool accumulate = (mode_ == Mode::Demo);
            FrameCB cb = makeFrameCB(accumulate);
            if (measuring) {
                cb.frameIndex = 0;
                cb.accumulate = 0;
                cb.seed = benchFrame_ + 1;
            }
            ok = renderer_.renderFrame(ctx_, cb,
                                       overlayDirty ? overlay_.pixels() : nullptr,
                                       overlayDirty, showOverlay_, shot);
        }
        overlayDirty_ = false;
        if (!ok) {
            logf("renderFrame failed; stopping");
            running_ = false;
            break;
        }

        if (measuring) renderer_.benchEndFrame(ctx_, benchFrame_);
        ctx_.endFrame();

        {
            static bool firstPresentLogged = false;
            if (!firstPresentLogged) {
                firstPresentLogged = true;
                logf("first frame submitted/presented: backbuffer=%u frameCounter=%llu",
                     ctx_.backbufferIndex, (unsigned long long)ctx_.frameCounter);
            }
        }

        double t1 = nowMs();
        lastFrameMs_ = t1 - t0;
        frameMsHistory_.push_back(lastFrameMs_);
        if (frameMsHistory_.size() > 60) frameMsHistory_.erase(frameMsHistory_.begin());

        if (wantScreenshot_) {
            ctx_.waitForGpu();
            renderer_.saveScreenshotBMP(outPath("rtprobe-screenshot.bmp"));
            wantScreenshot_ = false;
        }

        if (hwrt) {
            ++vkSampleIndex_;
        } else if (mode_ == Mode::Demo) {
            ++sampleIndex_;
        } else if (benchPhase_ == BenchPhase::Warmup) {
            ++warmupFrames_;
        } else {
            benchCpuMs_.push_back(lastFrameMs_);
            ++benchFrame_;

            bool done = false;
            if (opts_.seconds > 0) {
                double elapsed = nowMs() - measureStartMs_;
                if (elapsed >= opts_.seconds * 1000.0) done = true;
            }
            if (benchFrame_ >= benchMaxFrames_) done = true;
            if (done) finishBenchmark();
        }

        double now = nowMs();
        if (mode_ != Mode::HwRt && now - lastOverlay >= 250.0) {
            lastOverlay = now;
            refreshOverlay();
        }
    }
}

FrameCB App::makeFrameCB(bool accumulate) const {
    FrameCB cb{};
    const V3 target{0.0f, 1.0f, 1.0f};
    float cp = std::cos(pitch_);
    float sp = std::sin(pitch_);
    V3 offset{cp * std::sin(yaw_), sp, -cp * std::cos(yaw_)};
    V3 pos = target + offset * distance_;
    V3 fwd = normalize(target - pos);
    V3 worldUp{0.0f, 1.0f, 0.0f};
    V3 right = normalize(cross(worldUp, fwd));
    V3 up = cross(fwd, right);

    set4(cb.camPos, pos);
    set4(cb.camFwd, fwd);
    set4(cb.camRight, right);
    set4(cb.camUp, up);
    cb.width = (uint32_t)ctx_.width;
    cb.height = (uint32_t)ctx_.height;
    cb.frameIndex = sampleIndex_;
    cb.maxBounces = maxBounces_;
    cb.seed = 12345u;
    cb.primCount = (uint32_t)scene_.prims.size();
    cb.lightIndex = scene_.lightIndex;
    cb.accumulate = accumulate ? 1u : 0u;
    for (int i = 0; i < 4; ++i) cb.lightColor[i] = 0.0f;
    cb.tanHalfFov = std::tan(50.0f * 3.14159265358979f / 360.0f);
    for (int i = 0; i < 3; ++i) cb.pad[i] = 0.0f;
    return cb;
}

VkRtPush App::makeVkPush() const {
    VkRtPush p{};
    const V3 target{0.0f, 1.0f, 1.0f};
    float cp = std::cos(pitch_);
    float sp = std::sin(pitch_);
    V3 offset{cp * std::sin(yaw_), sp, -cp * std::cos(yaw_)};
    V3 pos = target + offset * distance_;
    V3 fwd = normalize(target - pos);
    V3 worldUp{0.0f, 1.0f, 0.0f};
    V3 right = normalize(cross(worldUp, fwd));
    V3 up = cross(fwd, right);

    set4(p.camPos, pos);
    set4(p.camFwd, fwd);
    set4(p.camRight, right);
    set4(p.camUp, up);
    p.width = (uint32_t)vkWidth_;
    p.height = (uint32_t)vkHeight_;
    p.frameIndex = vkSampleIndex_;
    p.maxBounces = maxBounces_;
    p.seed = 12345u;
    p.lightTriIndex = vkRt_.lightTriIndex();
    p.pad0 = p.pad1 = 0;
    return p;
}

void App::applyResizeIfNeeded() {
    if (!pendingResize_) return;
    pendingResize_ = false;
    if (mode_ == Mode::Bench) {
        logf("resize ignored during benchmark");
        return;
    }
    int w = pendingW_;
    int h = pendingH_;
    if (w <= 0 || h <= 0) return;
    if ((UINT)w == (UINT)ctx_.width && (UINT)h == (UINT)ctx_.height) return;

    if (!ctx_.resizeSwapchain(w, h)) return;
    if (!renderer_.resize(ctx_, w, h)) return;
    if (!overlay_.resize(w, h)) return;
    sampleIndex_ = 0;
    overlayDirty_ = true;
    refreshOverlay();
    logf("resized to %dx%d", w, h);
}

void App::resetAccumulation() {
    sampleIndex_ = 0;
    vkSampleIndex_ = 0;
}

bool App::enterHwRt() {
    if (mode_ == Mode::HwRt) return true;
    if (!vkRt_.ready()) {
        int w = std::max(160, ctx_.width);
        int h = std::max(120, ctx_.height);
        std::string err;
        logf("initializing Vulkan ray query RT (%dx%d)...", w, h);
        if (!vkRt_.init(w, h, err)) {
            vkError_ = err;
            logf("Vulkan RT init failed: %s", err.c_str());
            overlayDirty_ = true;
            refreshOverlay();
            return false;
        }
        vkWidth_ = w;
        vkHeight_ = h;
        vkSampleIndex_ = 0;
        vkError_.clear();
        logf("Vulkan RT enabled: device='%s' driver='%s' ext=%s",
             vkRt_.deviceName().c_str(), vkRt_.driverInfo().c_str(),
             vkRt_.extensions().c_str());
    }
    mode_ = Mode::HwRt;
    vkSampleIndex_ = 0;
    overlayDirty_ = true;
    refreshOverlay();
    return true;
}

void App::leaveHwRt() {
    mode_ = Mode::Demo;
    sampleIndex_ = 0;
    overlayDirty_ = true;
    refreshOverlay();
}

void App::startBenchmark() {
    if (mode_ == Mode::Bench) return;
    logf("benchmark requested");
    mode_ = Mode::Bench;
    benchPhase_ = BenchPhase::Warmup;
    benchFrame_ = 0;
    benchCpuMs_.clear();
    benchTiming_ = BenchTiming{};
    benchRan_ = false;
    benchWarmupSeconds_ = opts_.bench ? 10.0 : 3.0;
    if (opts_.seconds > 0) {
        benchMaxFrames_ = (uint32_t)std::min(2000, std::max(120, opts_.seconds * 120));
    } else {
        benchMaxFrames_ = (uint32_t)std::min(2000, std::max(30, opts_.frames));
    }
    renderer_.benchInit(ctx_, benchMaxFrames_);
    warmupFrames_ = 0;
    benchStartMs_ = nowMs();
    refreshOverlay();
}

void App::finishBenchmark() {
    logf("benchmark: measuring done after %u frames", benchFrame_);
    renderer_.benchFinish(ctx_, benchFrame_, benchTiming_);
    benchResult_ = makeBenchResult((uint32_t)ctx_.width, (uint32_t)ctx_.height, 1,
                                   maxBounces_, benchCpuMs_, benchTiming_.gpuMs,
                                   benchTiming_.gpuTimingAvailable,
                                   benchTiming_.timestampFrequency,
                                   benchWarmupSeconds_, benchTiming_.rayCount);
    benchRan_ = true;
    writeReport("bench");

    logf("bench result: cpu mean=%.2fms p95=%.2fms, gpu mean=%.2fms p95=%.2fms, rays/s=%.0f",
         benchResult_.cpu.mean, benchResult_.cpu.p95, benchResult_.gpu.mean,
         benchResult_.gpu.p95, benchResult_.totalRaysPerSec);

    // Grab one final frame for the screenshot (not timed).
    if (ctx_.beginFrame()) {
        FrameCB cb = makeFrameCB(false);
        renderer_.renderFrame(ctx_, cb, nullptr, false, showOverlay_, true);
        ctx_.endFrame();
        ctx_.waitForGpu();
        renderer_.saveScreenshotBMP(outPath("rtprobe-screenshot.bmp"));
    }

    mode_ = Mode::Demo;
    sampleIndex_ = 0;
    overlayDirty_ = true;
    refreshOverlay();

    if (opts_.bench) running_ = false;
}

std::vector<std::string> App::buildOverlayLines() const {
    std::vector<std::string> lines;
    lines.push_back("rtprobe - Winlator RT test");
    lines.push_back("GPU: " + shortStr(probe_.adapterName, 44));
    lines.push_back(strf("D3D12 FL %s   RaytracingTier: %s", probe_.featureLevel.c_str(),
                         tierStr(probe_.raytracingTier).c_str()));
    lines.push_back(strf("Wine %s   vkd3d %s", shortStr(probe_.wineVersion, 16).c_str(),
                         shortStr(probe_.d3d12coreVersion, 16).c_str()));
    lines.push_back("Vulkan: " + shortStr(probe_.vkDeviceName, 28) + " / " +
                    shortStr(probe_.vkDriverName, 16));
    lines.push_back("driver: " + shortStr(probe_.vkDriverInfo, 44));
    lines.push_back(strf("RT ext: ray_query=%s rt_pipeline=%s AS=%s dxr_prereq=%s",
                         probe_.vkRayQuery ? "YES" : "no",
                         probe_.vkRayPipeline ? "YES" : "no",
                         probe_.vkAccelStruct ? "YES" : "no",
                         (probe_.vkRayPipeline && probe_.vkAccelStruct && probe_.vkDeferredOps)
                             ? "YES" : "NO"));
    lines.push_back(strf("Scene: %zu prims / %zu BVH nodes", scene_.prims.size(),
                         scene_.nodes.size()));

    if (mode_ == Mode::Bench) {
        if (benchPhase_ == BenchPhase::Warmup) {
            double el = (nowMs() - benchStartMs_) / 1000.0;
            lines.push_back(strf("Mode: BENCH warmup %.1f / %.0fs (%u frames)", el,
                                 benchWarmupSeconds_, warmupFrames_));
        } else {
            lines.push_back(strf("Mode: BENCH measuring %u / %u frames", benchFrame_,
                                 benchMaxFrames_));
        }
    } else if (mode_ == Mode::HwRt) {
        lines.push_back("Mode: HW RT (Vulkan ray query)");
        lines.push_back("RT device: " + shortStr(vkRt_.deviceName(), 34));
        lines.push_back("RT driver: " + shortStr(vkRt_.driverInfo(), 42));
        lines.push_back(strf("RT res %dx%d   samples %u   bounces %u   lightTri %u",
                             vkWidth_, vkHeight_, vkSampleIndex_, maxBounces_,
                             vkRt_.lightTriIndex()));
        lines.push_back(strf("Frame: %.2f ms (%.1f FPS, Vk readback)", lastFrameMs_,
                             lastFrameMs_ > 0.0 ? 1000.0 / lastFrameMs_ : 0.0));
    } else {
        lines.push_back(strf("Mode: DEMO (software path tracer)   samples: %u   bounces: %u",
                             sampleIndex_, maxBounces_));
        lines.push_back(strf("Frame: %.2f ms   (%.1f FPS)", lastFrameMs_,
                             lastFrameMs_ > 0.0 ? 1000.0 / lastFrameMs_ : 0.0));
        if (vkRt_.ready()) lines.push_back("HW RT ready: press 4");
    }

    if (!vkError_.empty()) {
        std::string e = vkError_;
        for (char& c : e) {
            if (c == '\n' || c == '\r') c = ' ';
        }
        lines.push_back("Vulkan RT: " + shortStr(e, 50));
    }

    if (benchRan_) {
        lines.push_back(strf("Bench: cpu p50 %.2f ms / gpu p50 %.2f ms", benchResult_.cpu.median,
                             benchResult_.gpu.median));
        lines.push_back(strf("Bench: %.1f M primary rays/s", benchResult_.primaryRaysPerSec / 1e6));
    } else {
        lines.push_back("Verdict: " + shortStr(probe_.verdict, 52));
    }

    lines.push_back("");
    lines.push_back("1/2/3 bounces   R reset   B benchmark   4 HW RT");
    lines.push_back("F1 overlay   F2 screenshot   Esc quit");
    return lines;
}

void App::refreshOverlay() {
    if (!overlay_.pixels()) return;
    auto lines = buildOverlayLines();
    overlay_.render(lines);
    overlayDirty_ = true;
    lastOverlayRefreshMs_ = nowMs();
}

void App::toggleOverlay() {
    showOverlay_ = !showOverlay_;
    logf("overlay: %d", (int)showOverlay_);
}

void App::requestScreenshot() {
    wantScreenshot_ = true;
    logf("screenshot requested");
}

void App::onKeyDown(WPARAM key) {
    switch (key) {
    case VK_ESCAPE:
        running_ = false;
        PostQuitMessage(0);
        break;
    case VK_F1:
        toggleOverlay();
        break;
    case VK_F2:
    case 'S':
        requestScreenshot();
        break;
    case 'B':
        if (mode_ == Mode::Demo) startBenchmark();
        break;
    case 'R':
        resetAccumulation();
        break;
    case 'V':
        ctx_.vsync = !ctx_.vsync;
        logf("vsync: %d", (int)ctx_.vsync);
        break;
    case '4':
        if (mode_ == Mode::HwRt)
            leaveHwRt();
        else
            enterHwRt();
        break;
    case '1':
        maxBounces_ = 2;
        resetAccumulation();
        overlayDirty_ = true;
        break;
    case '2':
        maxBounces_ = 4;
        resetAccumulation();
        overlayDirty_ = true;
        break;
    case '3':
        maxBounces_ = 6;
        resetAccumulation();
        overlayDirty_ = true;
        break;
    default:
        break;
    }
}

void App::onMouseMove(int x, int y) {
    if (!dragging_) {
        lastMouseX_ = x;
        lastMouseY_ = y;
        return;
    }
    int dx = x - lastMouseX_;
    int dy = y - lastMouseY_;
    lastMouseX_ = x;
    lastMouseY_ = y;
    yaw_ += (float)dx * 0.006f;
    pitch_ -= (float)dy * 0.006f;
    pitch_ = std::max(-1.2f, std::min(1.2f, pitch_));
    resetAccumulation();
}

void App::onMouseWheel(int delta) {
    float factor = std::pow(0.9f, (float)delta / 120.0f);
    distance_ *= factor;
    distance_ = std::max(1.5f, std::min(8.0f, distance_));
    resetAccumulation();
}

LRESULT CALLBACK App::wndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    App* self = (App*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    return self->wndProc(hwnd, msg, wp, lp);
}

LRESULT App::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) {
            pendingW_ = LOWORD(lp);
            pendingH_ = HIWORD(lp);
            if (pendingW_ > 0 && pendingH_ > 0) pendingResize_ = true;
        }
        return 0;
    case WM_KEYDOWN:
        onKeyDown(wp);
        return 0;
    case WM_LBUTTONDOWN:
        dragging_ = true;
        lastMouseX_ = GET_X_LPARAM(lp);
        lastMouseY_ = GET_Y_LPARAM(lp);
        SetCapture(hwnd);
        return 0;
    case WM_LBUTTONUP:
        dragging_ = false;
        ReleaseCapture();
        return 0;
    case WM_MOUSEMOVE:
        onMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_MOUSEWHEEL:
        onMouseWheel(GET_WHEEL_DELTA_WPARAM(wp));
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        running_ = false;
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

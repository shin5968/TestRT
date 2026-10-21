#include "d3d12_ctx.h"

D3D12Context::~D3D12Context() { shutdown(); }

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Context::srvCpu(UINT i) const {
    D3D12_CPU_DESCRIPTOR_HANDLE h = srvHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += (SIZE_T)srvSize * i;
    return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE D3D12Context::srvGpu(UINT i) const {
    D3D12_GPU_DESCRIPTOR_HANDLE h = srvHeap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += (UINT64)srvSize * i;
    return h;
}

bool D3D12Context::initDevice(D3D12Api* inApi) {
    api = inApi;

    HRESULT hr = api->createDXGIFactory1(IID_IDXGIFactory2, (void**)factory.put());
    if (FAILED(hr)) {
        logf("CreateDXGIFactory1 failed: %s", hrStr(hr).c_str());
        return false;
    }

    ComPtr<IDXGIAdapter1> chosen;
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> a;
        if (factory->EnumAdapters1(i, a.put()) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 d{};
        if (FAILED(a->GetDesc1(&d))) continue;
        logf("adapter[%u]: %s vendor=0x%04X device=0x%04X flags=0x%X vram=%lluMB",
             i, narrow(d.Description).c_str(), d.VendorId, d.DeviceId, d.Flags,
             (unsigned long long)(d.DedicatedVideoMemory / (1024 * 1024)));
        if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        if (!chosen) {
            chosen = std::move(a);
            adapterDesc = d;
        }
    }
    if (!chosen) {
        logf("no hardware DXGI adapter found");
        return false;
    }
    adapter = std::move(chosen);

    static const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    for (D3D_FEATURE_LEVEL fl : levels) {
        hr = api->createDevice(adapter.get(), fl, IID_ID3D12Device, (void**)device.put());
        if (SUCCEEDED(hr)) {
            featureLevel = fl;
            break;
        }
        logf("D3D12CreateDevice(%s) failed: %s", featureLevelStr(fl).c_str(), hrStr(hr).c_str());
    }
    if (!device) {
        logf("failed to create D3D12 device at any feature level");
        return false;
    }
    logf("D3D12 device created, feature level %s", featureLevelStr(featureLevel).c_str());

    device->QueryInterface(IID_ID3D12Device5, (void**)device5.put());
    if (!device5) logf("ID3D12Device5 unavailable");

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qd.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    hr = device->CreateCommandQueue(&qd, IID_ID3D12CommandQueue, (void**)queue.put());
    if (FAILED(hr)) {
        logf("CreateCommandQueue failed: %s", hrStr(hr).c_str());
        return false;
    }

    for (int i = 0; i < kFrames; ++i) {
        hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            IID_ID3D12CommandAllocator, (void**)allocators[i].put());
        if (FAILED(hr)) {
            logf("CreateCommandAllocator failed: %s", hrStr(hr).c_str());
            return false;
        }
    }

    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators[0].get(),
                                   nullptr, IID_ID3D12GraphicsCommandList, (void**)list.put());
    if (FAILED(hr)) {
        logf("CreateCommandList failed: %s", hrStr(hr).c_str());
        return false;
    }
    list->QueryInterface(IID_ID3D12GraphicsCommandList4, (void**)list4.put());
    list->Close();

    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_ID3D12Fence, (void**)fence.put());
    if (FAILED(hr)) {
        logf("CreateFence failed: %s", hrStr(hr).c_str());
        return false;
    }
    fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent) {
        logf("CreateEvent failed: %lu", (unsigned long)GetLastError());
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.NumDescriptors = kSrvSlots;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = device->CreateDescriptorHeap(&srvDesc, IID_ID3D12DescriptorHeap, (void**)srvHeap.put());
    if (FAILED(hr)) {
        logf("CreateDescriptorHeap(SRV) failed: %s", hrStr(hr).c_str());
        return false;
    }
    srvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    if (SUCCEEDED(queue->GetTimestampFrequency(&timestampFreq_))) {
        logf("timestamp frequency: %llu Hz", (unsigned long long)timestampFreq_);
    } else {
        timestampFreq_ = 0;
        logf("timestamp frequency unavailable");
    }
    return true;
}

bool D3D12Context::createSwapchain(HWND hwnd, int w, int h) {
    if (!queue || !factory) return false;
    width = w;
    height = h;
    vsync = false;

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = (UINT)w;
    desc.Height = (UINT)h;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Stereo = FALSE;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 3;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    desc.Flags = 0;

    ComPtr<IDXGISwapChain1> swap1;
    HRESULT hr = factory->CreateSwapChainForHwnd(queue.get(), hwnd, &desc, nullptr, nullptr, swap1.put());
    if (FAILED(hr)) {
        logf("CreateSwapChainForHwnd failed: %s", hrStr(hr).c_str());
        return false;
    }
    hr = swap1->QueryInterface(IID_IDXGISwapChain3, (void**)swapchain.put());
    if (FAILED(hr)) {
        logf("IDXGISwapChain3 QI failed: %s", hrStr(hr).c_str());
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = kMaxBackbuffers;
    rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    hr = device->CreateDescriptorHeap(&rtvDesc, IID_ID3D12DescriptorHeap, (void**)rtvHeap.put());
    if (FAILED(hr)) {
        logf("CreateDescriptorHeap(RTV) failed: %s", hrStr(hr).c_str());
        return false;
    }
    rtvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    for (UINT i = 0; i < 3; ++i) {
        hr = swapchain->GetBuffer(i, IID_ID3D12Resource, (void**)backbuffers[i].put());
        if (FAILED(hr)) {
            logf("GetBuffer(%u) failed: %s", i, hrStr(hr).c_str());
            return false;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE h = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += (SIZE_T)rtvSize * i;
        device->CreateRenderTargetView(backbuffers[i].get(), nullptr, h);
    }
    backbufferCount = 3;
    hasSwapchain = true;
    logf("swapchain created: %dx%d", w, h);
    return true;
}

bool D3D12Context::resizeSwapchain(int w, int h) {
    if (!hasSwapchain) return false;
    if (w <= 0 || h <= 0) return false;
    if ((UINT)w == (UINT)width && (UINT)h == (UINT)height) return true;

    waitForGpu();
    for (auto& bb : backbuffers) bb.reset();

    HRESULT hr = swapchain->ResizeBuffers(0, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        logf("ResizeBuffers failed: %s", hrStr(hr).c_str());
        return false;
    }
    width = w;
    height = h;
    for (UINT i = 0; i < backbufferCount; ++i) {
        hr = swapchain->GetBuffer(i, IID_ID3D12Resource, (void**)backbuffers[i].put());
        if (FAILED(hr)) {
            logf("GetBuffer(%u) after resize failed: %s", i, hrStr(hr).c_str());
            return false;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE hnd = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        hnd.ptr += (SIZE_T)rtvSize * i;
        device->CreateRenderTargetView(backbuffers[i].get(), nullptr, hnd);
    }
    logf("swapchain resized: %dx%d", w, h);
    return true;
}

void D3D12Context::shutdown() {
    waitForGpu();
    hasSwapchain = false;
    swapchain.reset();
    for (auto& bb : backbuffers) bb.reset();
    rtvHeap.reset();
    srvHeap.reset();
    list4.reset();
    list.reset();
    for (auto& a : allocators) a.reset();
    if (fenceEvent) {
        CloseHandle(fenceEvent);
        fenceEvent = nullptr;
    }
    fence.reset();
    queue.reset();
    device5.reset();
    device.reset();
    adapter.reset();
    factory.reset();
}

bool D3D12Context::waitFence(UINT64 value, DWORD timeoutMs) {
    if (!fence) return true;
    if (fence->GetCompletedValue() >= value) return true;
    HRESULT hr = fence->SetEventOnCompletion(value, fenceEvent);
    if (FAILED(hr)) {
        logf("SetEventOnCompletion failed: %s", hrStr(hr).c_str());
        return false;
    }
    DWORD r = WaitForSingleObject(fenceEvent, timeoutMs);
    if (r != WAIT_OBJECT_0) {
        logf("GPU fence timeout after %lu ms (value=%llu, completed=%llu)",
             (unsigned long)timeoutMs, (unsigned long long)value,
             (unsigned long long)fence->GetCompletedValue());
        return false;
    }
    return true;
}

void D3D12Context::waitForGpu() {
    if (!queue || !fence) return;
    UINT64 v = ++fenceValue;
    if (FAILED(queue->Signal(fence.get(), v))) return;
    waitFence(v);
}

bool D3D12Context::beginFrame() {
    if (!list || !hasSwapchain) return false;

    if (frameFence[frameIndex] != 0) {
        if (!waitFence(frameFence[frameIndex], 8000)) return false;
    }

    HRESULT hr = allocators[frameIndex]->Reset();
    if (FAILED(hr)) {
        logf("allocator Reset failed: %s", hrStr(hr).c_str());
        return false;
    }
    hr = list->Reset(allocators[frameIndex].get(), nullptr);
    if (FAILED(hr)) {
        logf("list Reset failed: %s", hrStr(hr).c_str());
        return false;
    }

    backbufferIndex = swapchain->GetCurrentBackBufferIndex();
    if (backbufferIndex >= backbufferCount) backbufferIndex = 0;
    transition(list.get(), backbuffers[backbufferIndex].get(),
               D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    return true;
}

bool D3D12Context::endFrame() {
    if (!list || !hasSwapchain) return false;
    transition(list.get(), backbuffers[backbufferIndex].get(),
               D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

    HRESULT hr = list->Close();
    if (FAILED(hr)) {
        logf("list Close failed: %s", hrStr(hr).c_str());
        return false;
    }
    ID3D12CommandList* lists[] = {list.get()};
    queue->ExecuteCommandLists(1, lists);

    hr = swapchain->Present(vsync ? 1 : 0, 0);
    if (FAILED(hr)) {
        logf("Present failed: %s", hrStr(hr).c_str());
    }

    UINT64 v = ++fenceValue;
    queue->Signal(fence.get(), v);
    frameFence[frameIndex] = v;
    frameIndex = (frameIndex + 1) % kFrames;
    ++frameCounter;
    return true;
}

#pragma once

#include "util.h"

class D3D12Context {
public:
    static constexpr int kFrames = 3;

    D3D12Context() = default;
    ~D3D12Context();

    bool initDevice(D3D12Api* api);
    bool createSwapchain(HWND hwnd, int w, int h);
    bool resizeSwapchain(int w, int h);
    void shutdown();

    bool beginFrame();          // resets allocator/list, transitions backbuffer to RT
    bool endFrame();            // closes, executes, presents, signals fence
    void waitForGpu();
    bool waitFence(UINT64 value, DWORD timeoutMs);
    UINT64 timestampFrequency() const { return timestampFreq_; }

    D3D12Api* api = nullptr;
    ComPtr<IDXGIFactory2> factory;
    ComPtr<IDXGIAdapter1> adapter;
    DXGI_ADAPTER_DESC1 adapterDesc{};
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12Device5> device5;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12GraphicsCommandList4> list4;

    ComPtr<ID3D12CommandAllocator> allocators[kFrames];
    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent = nullptr;
    UINT64 fenceValue = 0;
    UINT64 frameFence[kFrames] = {0, 0, 0};
    int frameIndex = 0;
    UINT64 frameCounter = 0;

    ComPtr<IDXGISwapChain3> swapchain;
    static constexpr int kMaxBackbuffers = 3;
    ComPtr<ID3D12Resource> backbuffers[kMaxBackbuffers];
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    UINT rtvSize = 0;
    UINT backbufferCount = 0;
    UINT backbufferIndex = 0;

    ComPtr<ID3D12DescriptorHeap> srvHeap;   // shader visible
    UINT srvSize = 0;
    static constexpr UINT kSrvSlots = 8;
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpu(UINT i) const;
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu(UINT i) const;

    int width = 0;
    int height = 0;
    bool vsync = false;
    bool hasSwapchain = false;
    UINT64 timestampFreq_ = 0;

    bool waitFence(UINT64 value) { return waitFence(value, 8000); }
};

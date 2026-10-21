#pragma once

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>

#include <cstdint>
#include <cstdarg>
#include <string>
#include <vector>

template <class T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    ComPtr& operator=(ComPtr&& o) noexcept {
        if (this != &o) {
            reset();
            p_ = o.p_;
            o.p_ = nullptr;
        }
        return *this;
    }
    void reset() {
        if (p_) {
            p_->Release();
            p_ = nullptr;
        }
    }
    T* get() const { return p_; }
    T** put() {
        reset();
        return &p_;
    }
    T* operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
    void attach(T* p) {
        reset();
        p_ = p;
    }
    T* detach() {
        T* t = p_;
        p_ = nullptr;
        return t;
    }

private:
    T* p_ = nullptr;
};

// ---- logging -------------------------------------------------------------
void logInit();
void logShutdown();
void logf(const char* fmt, ...);
const std::vector<std::string>& logLines();

// ---- strings / paths -----------------------------------------------------
std::string strf(const char* fmt, ...);
std::string narrow(const wchar_t* w);
std::wstring widen(const std::string& s);
std::string exeDir();
std::string outPath(const std::string& name);
std::string hrStr(HRESULT hr);
std::string getEnvStr(const char* name);
std::string modulePath(const std::string& dll);
std::string moduleVersion(const std::string& dll);
std::string fileNameOf(const std::string& path);

uint64_t fnv1a(const void* data, size_t len, uint64_t h = 1469598103934665603ull);
uint64_t fnv1aStr(const std::string& s, uint64_t h = 1469598103934665603ull);

// ---- D3D12 helpers -------------------------------------------------------
struct D3D12Api {
    HMODULE d3d12 = nullptr;
    HMODULE dxgi = nullptr;
    HMODULE compiler = nullptr;

    decltype(&D3D12CreateDevice) createDevice = nullptr;
    decltype(&D3D12SerializeRootSignature) serializeRootSignature = nullptr;
    decltype(&D3D12SerializeVersionedRootSignature) serializeVersionedRootSignature = nullptr;
    decltype(&D3D12GetDebugInterface) getDebugInterface = nullptr;
    decltype(&CreateDXGIFactory1) createDXGIFactory1 = nullptr;
    HRESULT(WINAPI* d3dCompile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*,
                                ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT,
                                ID3DBlob**, ID3DBlob**) = nullptr;
    std::string compilerDll;

    bool load();
    void unload();
};

bool createBuffer(ID3D12Device* device, UINT64 size, D3D12_HEAP_TYPE heapType,
                  D3D12_RESOURCE_FLAGS flags, ID3D12Resource** out,
                  D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON);
void transition(ID3D12GraphicsCommandList* list, ID3D12Resource* res,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);

std::string featureLevelStr(D3D_FEATURE_LEVEL fl);

// Utility for SEH-guarded probe calls with C++ objects in the caller:
// caller keeps only POD locals and calls this.
long sehGuardVoid(void (*fn)(void*), void* user);

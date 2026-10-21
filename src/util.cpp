#include "util.h"

#include <cstdio>
#include <cstring>
#include <cwchar>

static FILE* g_log = nullptr;
static std::vector<std::string> g_lines;
static std::string g_dir;

std::string strf(const char* fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return std::string(buf);
}

std::string narrow(const wchar_t* w) {
    if (!w) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s((size_t)n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 1) return {};
    std::wstring w((size_t)n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}

std::string exeDir() {
    if (!g_dir.empty()) return g_dir;
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    if (n == 0 || n >= sizeof(buf) / sizeof(buf[0])) return ".";
    std::wstring p(buf, n);
    size_t slash = p.find_last_of(L"\\/");
    if (slash != std::wstring::npos) p.resize(slash);
    g_dir = narrow(p.c_str());
    if (g_dir.empty()) g_dir = ".";
    return g_dir;
}

void logInit() {
    std::string path = outPath("rtprobe.log");
    g_log = fopen(path.c_str(), "w");
    logf("rtprobe starting");
    logf("log file: %s", path.c_str());
}

void logShutdown() {
    if (g_log) {
        fflush(g_log);
        fclose(g_log);
        g_log = nullptr;
    }
}

static std::string timestamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    return strf("%02u:%02u:%02u.%03u", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

void logf(const char* fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    std::string line = timestamp() + " " + buf;
    g_lines.push_back(line);
    if (g_lines.size() > 2000) g_lines.erase(g_lines.begin(), g_lines.begin() + 500);
    if (g_log) {
        fputs(line.c_str(), g_log);
        fputc('\n', g_log);
        fflush(g_log);
    }
    OutputDebugStringA(line.c_str());
    OutputDebugStringA("\n");
}

const std::vector<std::string>& logLines() { return g_lines; }

std::string outPath(const std::string& name) {
    static std::string dir;
    if (dir.empty()) {
        std::string exe = exeDir();
        std::string probe = exe + "\\rtprobe-write-test.tmp";
        FILE* f = fopen(probe.c_str(), "wb");
        if (f) {
            fclose(f);
            remove(probe.c_str());
            dir = exe;
        } else {
            wchar_t tmp[MAX_PATH];
            DWORD n = GetTempPathW(MAX_PATH, tmp);
            if (n > 0 && n < MAX_PATH) {
                std::wstring w(tmp);
                if (!w.empty() && (w.back() == L'\\' || w.back() == L'/')) w.pop_back();
                dir = narrow(w.c_str());
            }
            if (dir.empty()) dir = ".";
        }
    }
    return dir + "\\" + name;
}

std::string hrStr(HRESULT hr) {
    char hex[32];
    snprintf(hex, sizeof(hex), "0x%08lX", (unsigned long)hr);
    std::string s(hex);
    switch (hr) {
    case S_OK: s += " (S_OK)"; break;
    case E_FAIL: s += " (E_FAIL)"; break;
    case E_INVALIDARG: s += " (E_INVALIDARG)"; break;
    case E_OUTOFMEMORY: s += " (E_OUTOFMEMORY)"; break;
    case E_NOTIMPL: s += " (E_NOTIMPL)"; break;
    case DXGI_ERROR_UNSUPPORTED: s += " (DXGI_ERROR_UNSUPPORTED)"; break;
    case DXGI_ERROR_DEVICE_REMOVED: s += " (DXGI_ERROR_DEVICE_REMOVED)"; break;
    default: break;
    }
    return s;
}

std::string getEnvStr(const char* name) {
    wchar_t buf[2048];
    std::wstring w = widen(name);
    DWORD n = GetEnvironmentVariableW(w.c_str(), buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    if (n == 0 || n >= sizeof(buf) / sizeof(buf[0])) return {};
    return narrow(std::wstring(buf, n).c_str());
}

std::string modulePath(const std::string& dll) {
    std::wstring wd = widen(dll);
    HMODULE mod = GetModuleHandleW(wd.c_str());
    if (!mod) mod = LoadLibraryW(wd.c_str());
    if (!mod) return {};
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(mod, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    if (n == 0) return {};
    return narrow(std::wstring(buf, n).c_str());
}

std::string moduleVersion(const std::string& dll) {
    std::wstring wd = widen(dll);
    HMODULE mod = GetModuleHandleW(wd.c_str());
    if (!mod) mod = LoadLibraryW(wd.c_str());
    if (!mod) return {};
    wchar_t path[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(mod, path, (DWORD)(sizeof(path) / sizeof(path[0])));
    if (n == 0) return {};
    DWORD dummy = 0;
    DWORD size = GetFileVersionInfoSizeW(path, &dummy);
    if (size == 0) return {};
    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(path, 0, size, data.data())) return {};
    VS_FIXEDFILEINFO* info = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(data.data(), L"\\", (LPVOID*)&info, &len) || !info) return {};
    return strf("%u.%u.%u.%u",
                (unsigned)(info->dwFileVersionMS >> 16),
                (unsigned)(info->dwFileVersionMS & 0xFFFF),
                (unsigned)(info->dwFileVersionLS >> 16),
                (unsigned)(info->dwFileVersionLS & 0xFFFF));
}

std::string fileNameOf(const std::string& path) {
    size_t p = path.find_last_of("\\/");
    return p == std::string::npos ? path : path.substr(p + 1);
}

uint64_t fnv1a(const void* data, size_t len, uint64_t h) {
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < len; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

uint64_t fnv1aStr(const std::string& s, uint64_t h) {
    return fnv1a(s.data(), s.size(), h);
}

// ---------------------------------------------------------------------------

bool createBuffer(ID3D12Device* device, UINT64 size, D3D12_HEAP_TYPE heapType,
                  D3D12_RESOURCE_FLAGS flags, ID3D12Resource** out,
                  D3D12_RESOURCE_STATES initialState) {
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = heapType;
    hp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    hp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    hp.CreationNodeMask = 1;
    hp.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Alignment = 0;
    d.Width = size;
    d.Height = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.Format = DXGI_FORMAT_UNKNOWN;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    d.Flags = flags;

    HRESULT hr = device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d,
                                                 initialState, nullptr,
                                                 IID_ID3D12Resource, (void**)out);
    if (FAILED(hr)) {
        logf("CreateCommittedResource(buffer %llu) failed: %s",
             (unsigned long long)size, hrStr(hr).c_str());
        return false;
    }
    return true;
}

void transition(ID3D12GraphicsCommandList* list, ID3D12Resource* res,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    if (before == after || !res) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    b.Transition.pResource = res;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    list->ResourceBarrier(1, &b);
}

std::string featureLevelStr(D3D_FEATURE_LEVEL fl) {
    switch (fl) {
    case D3D_FEATURE_LEVEL_12_1: return "12_1";
    case D3D_FEATURE_LEVEL_12_0: return "12_0";
    case D3D_FEATURE_LEVEL_11_1: return "11_1";
    case D3D_FEATURE_LEVEL_11_0: return "11_0";
    case D3D_FEATURE_LEVEL_10_1: return "10_1";
    case D3D_FEATURE_LEVEL_10_0: return "10_0";
    default: return strf("0x%X", (unsigned)fl);
    }
}

long sehGuardVoid(void (*fn)(void*), void* user) {
    __try {
        fn(user);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return (long)GetExceptionCode();
    }
}

bool D3D12Api::load() {
    d3d12 = LoadLibraryW(L"d3d12.dll");
    if (!d3d12) {
        logf("LoadLibrary(d3d12.dll) failed: %lu", (unsigned long)GetLastError());
        return false;
    }
    createDevice = (decltype(&D3D12CreateDevice))GetProcAddress(d3d12, "D3D12CreateDevice");
    serializeRootSignature =
        (decltype(&D3D12SerializeRootSignature))GetProcAddress(d3d12, "D3D12SerializeRootSignature");
    serializeVersionedRootSignature =
        (decltype(&D3D12SerializeVersionedRootSignature))GetProcAddress(
            d3d12, "D3D12SerializeVersionedRootSignature");
    getDebugInterface =
        (decltype(&D3D12GetDebugInterface))GetProcAddress(d3d12, "D3D12GetDebugInterface");

    dxgi = LoadLibraryW(L"dxgi.dll");
    if (dxgi) {
        createDXGIFactory1 = (decltype(&CreateDXGIFactory1))GetProcAddress(dxgi, "CreateDXGIFactory1");
    }

    const wchar_t* candidates[] = {L"d3dcompiler_47.dll", L"d3dcompiler_46.dll", L"d3dcompiler_43.dll"};
    for (const wchar_t* c : candidates) {
        compiler = LoadLibraryW(c);
        if (compiler) {
            d3dCompile = (decltype(d3dCompile))GetProcAddress(compiler, "D3DCompile");
            compilerDll = narrow(c);
            if (d3dCompile) break;
            FreeLibrary(compiler);
            compiler = nullptr;
            compilerDll.clear();
        }
    }
    if (!d3dCompile) logf("D3DCompile not available (d3dcompiler_47/46/43 missing)");

    if (!createDevice) logf("D3D12CreateDevice not found");
    if (!createDXGIFactory1) logf("CreateDXGIFactory1 not found");
    return createDevice != nullptr && createDXGIFactory1 != nullptr;
}

void D3D12Api::unload() {
    if (compiler) FreeLibrary(compiler);
    if (dxgi) FreeLibrary(dxgi);
    if (d3d12) FreeLibrary(d3d12);
    compiler = dxgi = d3d12 = nullptr;
}

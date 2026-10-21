#pragma once

#include "d3d12_ctx.h"

#include <cstdint>
#include <string>

struct DxrTestResult {
    bool attempted = false;
    bool asBuilt = false;
    bool stateObjectCreated = false;
    bool dispatched = false;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t total = 0;
    uint32_t hits = 0;
    std::string stage;
    std::string error;
};

// Builds a one-triangle BLAS/TLAS, creates a DXR RTPSO from the embedded DXIL,
// dispatches a 64x64 ray grid, and counts hits. Requires vkd3d-proton DXR.
bool runDxrTest(D3D12Context& ctx, D3D12Api* api, DxrTestResult& out);

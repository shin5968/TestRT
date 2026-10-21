#pragma once

#include "bench.h"
#include "probe.h"
#include "scene.h"

#include <string>

struct ReportData {
    ProbeResult probe;
    uint64_t sceneHash = 0;
    size_t primCount = 0;
    size_t nodeCount = 0;
    double sceneBuildMs = 0.0;
    std::string shaderHash;
    double shaderCompileMs = 0.0;
    std::string mode;
    bool benchRan = false;
    BenchResult bench;
};

bool writeReportJSON(const std::string& path, const ReportData& data);

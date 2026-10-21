#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct BenchStats {
    double mean = 0.0;
    double median = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double stdev = 0.0;
};

struct BenchResult {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t samplesPerFrame = 1;
    uint32_t bounces = 0;
    uint32_t frames = 0;
    double warmupSeconds = 0.0;
    double measureSeconds = 0.0;
    bool gpuTiming = false;
    uint64_t timestampFrequency = 0;
    BenchStats cpu;
    BenchStats gpu;
    std::vector<double> cpuMs;
    std::vector<double> gpuMs;
    uint64_t totalRayCount = 0;
    double primaryRaysPerSec = 0.0;
    double totalRaysPerSec = 0.0;
};

BenchStats computeStats(const std::vector<double>& samples);
double percentile(std::vector<double> sorted, double p);
BenchResult makeBenchResult(uint32_t w, uint32_t h, uint32_t spp, uint32_t bounces,
                            const std::vector<double>& cpuMs,
                            const std::vector<double>& gpuMs,
                            bool gpuTiming, uint64_t timestampFreq,
                            double warmupSeconds, uint64_t totalRayCount);

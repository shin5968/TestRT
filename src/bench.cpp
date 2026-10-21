#include "bench.h"

#include <algorithm>
#include <cmath>

double percentile(std::vector<double> sorted, double p) {
    if (sorted.empty()) return 0.0;
    if (p <= 0.0) return sorted.front();
    if (p >= 100.0) return sorted.back();
    double rank = (p / 100.0) * (double)(sorted.size() - 1);
    size_t lo = (size_t)std::floor(rank);
    size_t hi = (size_t)std::ceil(rank);
    if (lo == hi) return sorted[lo];
    double t = rank - (double)lo;
    return sorted[lo] * (1.0 - t) + sorted[hi] * t;
}

BenchStats computeStats(const std::vector<double>& samples) {
    BenchStats s;
    if (samples.empty()) return s;
    std::vector<double> sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    double sum = 0.0;
    for (double v : samples) sum += v;
    s.mean = sum / (double)samples.size();
    s.median = percentile(sorted, 50.0);
    s.p95 = percentile(sorted, 95.0);
    s.p99 = percentile(sorted, 99.0);
    double var = 0.0;
    for (double v : samples) {
        double d = v - s.mean;
        var += d * d;
    }
    s.stdev = samples.size() > 1 ? std::sqrt(var / (double)(samples.size() - 1)) : 0.0;
    return s;
}

BenchResult makeBenchResult(uint32_t w, uint32_t h, uint32_t spp, uint32_t bounces,
                            const std::vector<double>& cpuMs,
                            const std::vector<double>& gpuMs,
                            bool gpuTiming, uint64_t timestampFreq,
                            double warmupSeconds, uint64_t totalRayCount) {
    BenchResult r;
    r.width = w;
    r.height = h;
    r.samplesPerFrame = spp;
    r.bounces = bounces;
    r.frames = (uint32_t)cpuMs.size();
    r.warmupSeconds = warmupSeconds;
    r.gpuTiming = gpuTiming;
    r.timestampFrequency = timestampFreq;
    r.cpu = computeStats(cpuMs);
    r.gpu = computeStats(gpuMs);
    r.cpuMs = cpuMs;
    r.gpuMs = gpuMs;
    r.totalRayCount = totalRayCount;

    double totalMs = 0.0;
    for (double v : cpuMs) totalMs += v;
    r.measureSeconds = totalMs / 1000.0;

    if (r.measureSeconds > 0.0) {
        double primary = (double)w * (double)h * (double)spp * (double)cpuMs.size();
        r.primaryRaysPerSec = primary / r.measureSeconds;
    }
    if (r.measureSeconds > 0.0 && totalRayCount > 0) {
        r.totalRaysPerSec = (double)totalRayCount / r.measureSeconds;
    }
    return r;
}

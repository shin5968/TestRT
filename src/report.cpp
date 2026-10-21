#include "report.h"

#include "util.h"

#include <cstdio>
#include <ctime>

namespace {

std::string esc(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (c < 0x20) {
                o += strf("\\u%04X", c);
            } else {
                o += (char)c;
            }
        }
    }
    return o;
}

std::string q(const std::string& s) { return "\"" + esc(s) + "\""; }

std::string boolStr(bool v) { return v ? "true" : "false"; }

std::string statsJson(const BenchStats& s) {
    return strf("{\"mean_ms\": %.3f, \"median_ms\": %.3f, \"p95_ms\": %.3f, "
                "\"p99_ms\": %.3f, \"stdev_ms\": %.3f}",
                s.mean, s.median, s.p95, s.p99, s.stdev);
}

}  // namespace

bool writeReportJSON(const std::string& path, const ReportData& d) {
    const ProbeResult& p = d.probe;

    time_t now = time(nullptr);
    struct tm tmv {};
    localtime_s(&tmv, &now);
    char stamp[64];
    strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%S", &tmv);

    std::string j;
    j += "{\n";
    j += "  \"schema\": 1,\n";
    j += "  \"app\": \"rtprobe\",\n";
    j += "  \"generated\": " + q(stamp) + ",\n";
    j += "  \"mode\": " + q(d.mode) + ",\n";

    j += "  \"probe\": {\n";
    j += "    \"adapter\": " + q(p.adapterName) + ",\n";
    j += strf("    \"vendor_id\": %u,\n", p.vendorId);
    j += strf("    \"device_id\": %u,\n", p.deviceId);
    j += strf("    \"dedicated_vram_mb\": %llu,\n",
              (unsigned long long)(p.dedicatedVram / (1024 * 1024)));
    j += "    \"feature_level\": " + q(p.featureLevel) + ",\n";
    j += strf("    \"raytracing_tier\": %d,\n", p.raytracingTier);
    j += "    \"raytracing_tier_text\": " + q(tierStr(p.raytracingTier)) + ",\n";
    j += strf("    \"shader_model_major\": %d,\n", p.shaderModelMajor);
    j += strf("    \"shader_model_minor\": %d,\n", p.shaderModelMinor);
    j += "    \"device5\": " + boolStr(p.device5) + ",\n";
    j += "    \"command_list4\": " + boolStr(p.commandList4) + ",\n";
    j += "    \"as_prebuild_ok\": " + boolStr(p.asPrebuildOk) + ",\n";
    j += strf("    \"as_result_bytes\": %llu,\n", (unsigned long long)p.asResultSize);
    j += strf("    \"as_scratch_bytes\": %llu,\n", (unsigned long long)p.asScratchSize);
    j += strf("    \"video_mem_budget_mb\": %llu,\n",
              (unsigned long long)(p.memBudget / (1024 * 1024)));
    j += strf("    \"video_mem_usage_mb\": %llu,\n",
              (unsigned long long)(p.memUsage / (1024 * 1024)));
    j += "    \"modules\": {\n";
    j += "      \"d3d12\": {\"path\": " + q(p.d3d12Path) + ", \"version\": " + q(p.d3d12Version) + "},\n";
    j += "      \"d3d12core\": {\"path\": " + q(p.d3d12corePath) + ", \"version\": " +
         q(p.d3d12coreVersion) + "},\n";
    j += "      \"dxgi\": {\"path\": " + q(p.dxgiPath) + ", \"version\": " + q(p.dxgiVersion) + "},\n";
    j += "      \"d3dcompiler\": {\"path\": " + q(p.compilerPath) + ", \"version\": " +
         q(p.compilerVersion) + "},\n";
    j += "      \"dxcompiler\": {\"path\": " + q(p.dxcPath) + ", \"version\": " + q(p.dxcVersion) + "},\n";
    j += "      \"vulkan\": {\"path\": " + q(p.vulkanPath) + "}\n";
    j += "    },\n";
    j += "    \"wine_version\": " + q(p.wineVersion) + ",\n";
    j += "    \"env\": {\n";
    j += "      \"VKD3D_CONFIG\": " + q(p.vkd3dConfig) + ",\n";
    j += "      \"VKD3D_SHADER_MODEL\": " + q(p.vkd3dShaderModel) + ",\n";
    j += "      \"VKD3D_FEATURE_LEVEL\": " + q(p.vkd3dFeatureLevel) + ",\n";
    j += "      \"DXVK_CONFIG\": " + q(p.dxvkConfig) + "\n";
    j += "    },\n";
    j += "    \"vulkan\": {\n";
    j += "      \"available\": " + boolStr(p.vkAvailable) + ",\n";
    j += "      \"error\": " + q(p.vkError) + ",\n";
    j += "      \"instance_api\": " + q(p.vkApiVersion) + ",\n";
    j += "      \"device_name\": " + q(p.vkDeviceName) + ",\n";
    j += "      \"driver_name\": " + q(p.vkDriverName) + ",\n";
    j += "      \"driver_info\": " + q(p.vkDriverInfo) + ",\n";
    j += "      \"extensions_found\": " + q(p.vkExtensions) + ",\n";
    j += "      \"ray_query\": " + boolStr(p.vkRayQuery) + ",\n";
    j += "      \"ray_tracing_pipeline\": " + boolStr(p.vkRayPipeline) + ",\n";
    j += "      \"acceleration_structure\": " + boolStr(p.vkAccelStruct) + ",\n";
    j += "      \"deferred_host_operations\": " + boolStr(p.vkDeferredOps) + ",\n";
    j += "      \"buffer_device_address\": " + boolStr(p.vkBufferDeviceAddress) + ",\n";
    j += "      \"dxr_prerequisites\": " +
         boolStr(p.vkRayPipeline && p.vkAccelStruct && p.vkDeferredOps) + ",\n";
    j += "      \"timestamps\": " + boolStr(p.vkTimestamps) + ",\n";
    j += strf("      \"timestamp_period_ns\": %.3f\n", p.vkTimestampPeriod);
    j += "    }\n";
    j += "  },\n";

    j += "  \"scene\": {\n";
    j += strf("    \"hash\": \"%016llX\",\n", (unsigned long long)d.sceneHash);
    j += strf("    \"primitives\": %zu,\n", d.primCount);
    j += strf("    \"bvh_nodes\": %zu,\n", d.nodeCount);
    j += strf("    \"build_ms\": %.1f\n", d.sceneBuildMs);
    j += "  },\n";

    j += "  \"renderer\": {\n";
    j += "    \"shader_hash\": " + q(d.shaderHash) + ",\n";
    j += strf("    \"shader_compile_ms\": %.1f\n", d.shaderCompileMs);
    j += "  },\n";

    if (d.benchRan) {
        const BenchResult& b = d.bench;
        j += "  \"benchmark\": {\n";
        j += strf("    \"width\": %u,\n", b.width);
        j += strf("    \"height\": %u,\n", b.height);
        j += strf("    \"samples_per_frame\": %u,\n", b.samplesPerFrame);
        j += strf("    \"bounces\": %u,\n", b.bounces);
        j += strf("    \"frames\": %u,\n", b.frames);
        j += strf("    \"warmup_seconds\": %.1f,\n", b.warmupSeconds);
        j += strf("    \"measure_seconds\": %.3f,\n", b.measureSeconds);
        j += "    \"gpu_timing\": " + boolStr(b.gpuTiming) + ",\n";
        j += strf("    \"timestamp_frequency\": %llu,\n",
                  (unsigned long long)b.timestampFrequency);
        j += "    \"cpu_frame\": " + statsJson(b.cpu) + ",\n";
        j += "    \"gpu_frame\": " + statsJson(b.gpu) + ",\n";
        j += strf("    \"total_ray_count\": %llu,\n", (unsigned long long)b.totalRayCount);
        j += strf("    \"primary_rays_per_sec\": %.0f,\n", b.primaryRaysPerSec);
        j += strf("    \"total_rays_per_sec\": %.0f,\n", b.totalRaysPerSec);
        j += "    \"cpu_frame_ms\": [";
        for (size_t i = 0; i < b.cpuMs.size(); ++i) {
            if (i) j += ",";
            j += strf("%.3f", b.cpuMs[i]);
        }
        j += "],\n";
        j += "    \"gpu_frame_ms\": [";
        for (size_t i = 0; i < b.gpuMs.size(); ++i) {
            if (i) j += ",";
            j += strf("%.3f", b.gpuMs[i]);
        }
        j += "]\n";
        j += "  },\n";
    } else {
        j += "  \"benchmark\": null,\n";
    }

    if (d.hwBenchRan) {
        const BenchResult& b = d.hwBench;
        j += "  \"hardware_rt_benchmark\": {\n";
        j += strf("    \"width\": %u,\n", b.width);
        j += strf("    \"height\": %u,\n", b.height);
        j += strf("    \"samples_per_frame\": %u,\n", b.samplesPerFrame);
        j += strf("    \"bounces\": %u,\n", b.bounces);
        j += strf("    \"frames\": %u,\n", b.frames);
        j += strf("    \"warmup_seconds\": %.1f,\n", b.warmupSeconds);
        j += strf("    \"measure_seconds\": %.3f,\n", b.measureSeconds);
        j += "    \"gpu_timing\": " + boolStr(b.gpuTiming) + ",\n";
        j += strf("    \"timestamp_period_ns\": %.3f,\n", d.hwTimestampPeriodNs);
        j += "    \"cpu_frame\": " + statsJson(b.cpu) + ",\n";
        j += "    \"gpu_frame\": " + statsJson(b.gpu) + ",\n";
        j += strf("    \"primary_rays_per_sec\": %.0f,\n", b.primaryRaysPerSec);
        j += "    \"device\": " + q(d.hwDevice) + ",\n";
        j += "    \"driver\": " + q(d.hwDriver) + ",\n";
        j += "    \"extensions\": " + q(d.hwExtensions) + ",\n";
        j += strf("    \"triangles\": %u,\n", d.hwTriangles);
        j += "    \"cpu_frame_ms\": [";
        for (size_t i = 0; i < b.cpuMs.size(); ++i) {
            if (i) j += ",";
            j += strf("%.3f", b.cpuMs[i]);
        }
        j += "],\n";
        j += "    \"gpu_frame_ms\": [";
        for (size_t i = 0; i < b.gpuMs.size(); ++i) {
            if (i) j += ",";
            j += strf("%.3f", b.gpuMs[i]);
        }
        j += "]\n";
        j += "  },\n";
    } else {
        j += "  \"hardware_rt_benchmark\": null,\n";
        if (!d.hwError.empty()) {
            j += "  \"hardware_rt_error\": " + q(d.hwError) + ",\n";
        }
    }

    j += "  \"verdict\": " + q(p.verdict) + "\n";
    j += "}\n";

    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        logf("failed to open report: %s", path.c_str());
        return false;
    }
    fwrite(j.data(), 1, j.size(), f);
    fclose(f);
    logf("report written: %s", path.c_str());
    return true;
}

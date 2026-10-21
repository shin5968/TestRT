#include "app.h"
#include "util.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace {

std::vector<std::string> splitArgs(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    bool inQuote = false;
    for (char c : s) {
        if (c == '"') {
            inQuote = !inQuote;
        } else if (!inQuote && (c == ' ' || c == '\t')) {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

int toInt(const std::string& s, int def) {
    try {
        return std::stoi(s);
    } catch (...) {
        return def;
    }
}

AppOptions parseArgs(const std::string& cmdline) {
    AppOptions o;
    auto args = splitArgs(cmdline);
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto next = [&](int def) { return (i + 1 < args.size()) ? toInt(args[++i], def) : def; };
        if (a == "--probe-only") {
            o.probeOnly = true;
        } else if (a == "--bench") {
            o.bench = true;
        } else if (a == "--vulkan-rt" || a == "--hwrt") {
            o.vulkanRt = true;
        } else if (a == "--seconds") {
            o.seconds = next(o.seconds);
        } else if (a == "--frames") {
            o.frames = next(o.frames);
        } else if (a == "--warmup") {
            // accepted for compatibility; warmup is fixed by mode
            (void)next(10);
        } else if (a == "--width" || a == "--render-width") {
            o.width = next(o.width);
        } else if (a == "--height" || a == "--render-height") {
            o.height = next(o.height);
        } else if (a == "--vsync") {
            o.vsync = true;
        } else if (a == "--borderless") {
            o.borderless = true;
        } else if (a == "--debug") {
            o.debug = true;
        } else {
            logf("unknown argument: %s", a.c_str());
        }
    }
    if (o.width < 160) o.width = 160;
    if (o.height < 120) o.height = 120;
    if (o.frames < 1) o.frames = 1;
    return o;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR lpCmdLine, int) {
    logInit();
    std::string cmd = lpCmdLine ? narrow(lpCmdLine) : std::string();
    logf("cmdline: %s", cmd.c_str());

    AppOptions opts = parseArgs(cmd);
    App app;
    int rc = app.run(hInstance, opts);

    logf("exit code %d", rc);
    logShutdown();
    return rc;
}

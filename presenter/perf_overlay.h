/*
 * Optional host performance overlay.
 *
 * The Wayland presenter draws it into a dedicated transparent overlay
 * subsurface above the app, so it stays visible with zero-copy app surfaces.
 * Other presenters draw it directly into the composed ARGB frame.
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace droidlet {

class PerfOverlay {
public:
    void init(const char* presenterName, int width, int height);

    bool enabled() const { return mEnabled; }
    bool hasData() const { return mLastPresentCount > 0; }

    void onPresent(bool presented, double composeMs, double presentMs,
                   uint64_t latched, uint64_t dropped, int layerCount,
                   int configuredIntervalMs);
    void onSharedFrames(uint64_t totalSwaps);

    void draw(std::vector<uint32_t>& frame, int width, int height) const;
    void drawOverlay(std::vector<uint32_t>& frame, int width, int height) const;

private:
    struct Stats {
        double fps = 0.0;
        double appFps = 0.0;
        double frameMs = 0.0;
        double p50Ms = 0.0;
        double p95Ms = 0.0;
        double maxMs = 0.0;
        double composeMs = 0.0;
        double presentMs = 0.0;
        uint64_t latched = 0;
        uint64_t dropped = 0;
        uint64_t late = 0;
        double rssMb = 0.0;
        double pssMb = 0.0;
        double peakMb = 0.0;
        double cpuPercent = 0.0;
        double vramMb = 0.0;
        int layerCount = 0;
        int intervalMs = 16;
        std::string renderer = "UNKNOWN";
        std::string presenter = "UNKNOWN";
        bool hwui = false;
        bool zeroCopy = false;
        uint64_t swaps = 0;
    };

    void drawInternal(std::vector<uint32_t>& frame, int width, int height,
                      bool alphaBlend) const;
    void refreshSlowStats(bool force);
    void probeRenderer();
    static double readProcessMemoryMb(const char* key, const char* path);
    static double readProcessCpuPercent();
    static double readVramMb();

    bool mEnabled = false;
    int mWidth = 0;
    int mHeight = 0;
    std::string mPresenterName;
    std::string mRenderer;
    Stats mStats;

    std::deque<double> mIntervals;
    uint64_t mLastPresentCount = 0;
    uint64_t mLastSharedCount = 0;
    std::chrono::steady_clock::time_point mLastSharedTime;
    bool mHasSharedTime = false;
    uint64_t mLateFrames = 0;
    uint64_t mLastLatched = 0;
    uint64_t mLastDropped = 0;
    int mLastLayerCount = 0;
    double mLastComposeMs = 0.0;
    double mLastPresentMs = 0.0;
    std::chrono::steady_clock::time_point mLastPresentTime;
    std::chrono::steady_clock::time_point mLastSlowRefresh;

    static constexpr size_t kMaxIntervals = 120;
    static constexpr int kScale = 2;
};

}  // namespace droidlet

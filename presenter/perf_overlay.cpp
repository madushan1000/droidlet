#include "perf_overlay.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <unistd.h>

namespace droidlet {
namespace {

constexpr uint32_t kPanelColor = 0xC0101010u;
constexpr uint32_t kTextColor = 0xFFFFFFFFu;
constexpr uint32_t kLabelColor = 0xFFFFD040u;

constexpr int kGlyphW = 5;
constexpr int kGlyphH = 7;
constexpr int kGlyphAdvance = 6;
constexpr int kLineAdvance = 9;

// 5x7 bitmap glyphs, one string of 35 '0'/'1' characters per glyph.
const char* glyphRows(char input) {
    char c = input;
    if (c >= 'a' && c <= 'z') {
        c = static_cast<char>(c - 'a' + 'A');
    }
    switch (c) {
        case 'A': return "01110100011000111111100011000110001";
        case 'B': return "11110100011000111110100011000111110";
        case 'C': return "01110100011000010000100001000101110";
        case 'D': return "11110100011000110001100011000111110";
        case 'E': return "11111100001000011110100001000011111";
        case 'F': return "11111100001000011110100001000010000";
        case 'G': return "01110100011000010111100011000101110";
        case 'H': return "10001100011000111111100011000110001";
        case 'I': return "11111001000010000100001000010011111";
        case 'J': return "00111000100001000010100101001001100";
        case 'K': return "10001100101010011000101001001010001";
        case 'L': return "10000100001000010000100001000011111";
        case 'M': return "10001110111010110101100011000110001";
        case 'N': return "10001110011010110011100011000110001";
        case 'O': return "01110100011000110001100011000101110";
        case 'P': return "11110100011000111110100001000010000";
        case 'Q': return "01110100011000110001101011001001101";
        case 'R': return "11110100011000111110101001001010001";
        case 'S': return "01111100001000001110000010000111110";
        case 'T': return "11111001000010000100001000010000100";
        case 'U': return "10001100011000110001100011000101110";
        case 'V': return "10001100011000110001100010101000100";
        case 'W': return "10001100011000110101101011010101010";
        case 'X': return "10001100010101000100010101000110001";
        case 'Y': return "10001100010101000100001000010000100";
        case 'Z': return "11111000010001000100010001000011111";
        case '0': return "01110100011001110101110011000101110";
        case '1': return "00100011000010000100001000010001110";
        case '2': return "01110100010000100010001000100011111";
        case '3': return "11111000100010000010000110001001110";
        case '4': return "00010001100101010010111110001000010";
        case '5': return "11111100001111000001000011000101110";
        case '6': return "00110010001000011110100011000101110";
        case '7': return "11111000010001000100010000100001000";
        case '8': return "01110100011000101110100011000101110";
        case '9': return "01110100011000101111000010001001100";
        case ' ': return "00000000000000000000000000000000000";
        case ':': return "00000001000010000000001000010000000";
        case '.': return "00000000000000000000000000010000100";
        case ',': return "00000000000000000000001000010001000";
        case '-': return "00000000000000011111000000000000000";
        case '_': return "00000000000000000000000000000011111";
        case '/': return "00001000100001000100010001000010000";
        case '%': return "11001110100001000100010000101110011";
        case '(': return "00010001000100001000010000010000010";
        case ')': return "01000001000001000010000100010001000";
        case '[': return "00110001000010000100001000010000110";
        case ']': return "01100001000010000100001000010001100";
        case '+': return "00000001000010011111001000010000000";
        case '=': return "00000111110000000000111110000000000";
        case '?': return "01110100010000100010001000000000100";
        case '*': return "00000101010011101010100000000000000";
        case '#': return "01010111110101011111010100101000000";
        case '<': return "00010001000100010000010000010000010";
        case '>': return "01000001000001000001000100010001000";
        case '!': return "00100001000010000100001000000000100";
        default: return "01110100010000100010001000000000100";
    }
}

bool envEnabled(const char* name) {
    return std::getenv(name) != nullptr;
}

void blendPixel(uint32_t& dst, uint32_t src, bool alphaBlend) {
    const uint32_t a = src >> 24;
    if (a == 0) {
        return;
    }
    if (a == 0xFF) {
        dst = src;
        return;
    }
    if (alphaBlend) {
        const uint32_t dstA = dst >> 24;
        const uint32_t dstScale = dstA * (255 - a) / 255;
        const uint32_t outA = a + dstScale;
        const uint32_t dr = (dst >> 16) & 0xFF;
        const uint32_t dg = (dst >> 8) & 0xFF;
        const uint32_t db = dst & 0xFF;
        const uint32_t sr = (src >> 16) & 0xFF;
        const uint32_t sg = (src >> 8) & 0xFF;
        const uint32_t sb = src & 0xFF;
        const uint32_t denom = outA == 0 ? 1 : outA;
        const uint32_t r = (sr * a + dr * dstScale) / denom;
        const uint32_t g = (sg * a + dg * dstScale) / denom;
        const uint32_t b = (sb * a + db * dstScale) / denom;
        dst = (outA << 24) | (r << 16) | (g << 8) | b;
        return;
    }
    const uint32_t inv = 255 - a;
    const uint32_t dr = (dst >> 16) & 0xFF;
    const uint32_t dg = (dst >> 8) & 0xFF;
    const uint32_t db = dst & 0xFF;
    const uint32_t sr = (src >> 16) & 0xFF;
    const uint32_t sg = (src >> 8) & 0xFF;
    const uint32_t sb = src & 0xFF;
    const uint32_t r = (sr * a + dr * inv) / 255;
    const uint32_t g = (sg * a + dg * inv) / 255;
    const uint32_t b = (sb * a + db * inv) / 255;
    dst = 0xFF000000u | (r << 16) | (g << 8) | b;
}

void drawGlyph(std::vector<uint32_t>& frame, int width, int height, int x, int y,
               const char* rows, uint32_t color, int scale, bool alphaBlend) {
    for (int row = 0; row < kGlyphH; ++row) {
        for (int col = 0; col < kGlyphW; ++col) {
            if (rows[row * kGlyphW + col] != '1') {
                continue;
            }
            for (int sy = 0; sy < scale; ++sy) {
                const int py = y + row * scale + sy;
                if (py < 0 || py >= height) {
                    continue;
                }
                for (int sx = 0; sx < scale; ++sx) {
                    const int px = x + col * scale + sx;
                    if (px < 0 || px >= width) {
                        continue;
                    }
                    blendPixel(frame[static_cast<size_t>(py) * width + px], color, alphaBlend);
                }
            }
        }
    }
}

void drawText(std::vector<uint32_t>& frame, int width, int height, int x, int y,
              const std::string& text, uint32_t color, int scale, bool alphaBlend) {
    int cursor = x;
    for (char c : text) {
        drawGlyph(frame, width, height, cursor, y, glyphRows(c), color, scale, alphaBlend);
        cursor += kGlyphAdvance * scale;
    }
}

int textWidth(const std::string& text, int scale) {
    return static_cast<int>(text.size()) * kGlyphAdvance * scale;
}

}  // namespace

void PerfOverlay::init(const char* presenterName, int width, int height) {
    mEnabled = envEnabled("DROIDLET_PERF_OVERLAY");
    mWidth = width;
    mHeight = height;
    mPresenterName = presenterName != nullptr ? presenterName : "unknown";
    mRenderer = "PROBING";
    mLastPresentTime = std::chrono::steady_clock::now();
    mLastSlowRefresh = mLastPresentTime;
    if (mEnabled) {
        probeRenderer();
        refreshSlowStats(true);
    }
}

void PerfOverlay::onPresent(bool presented, double composeMs, double presentMs,
                            uint64_t latched, uint64_t dropped, int layerCount,
                            int configuredIntervalMs) {
    if (!mEnabled || !presented) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (mLastPresentCount != 0) {
        const double intervalMs =
                std::chrono::duration<double, std::milli>(now - mLastPresentTime).count();
        if (intervalMs > 0.0) {
            mIntervals.push_back(intervalMs);
            while (mIntervals.size() > kMaxIntervals) {
                mIntervals.pop_front();
            }
            const double lateThreshold = std::max(1.0, configuredIntervalMs * 1.5);
            if (intervalMs > lateThreshold) {
                ++mLateFrames;
            }
            mStats.frameMs = intervalMs;
        }
    }

    mLastPresentTime = now;
    ++mLastPresentCount;
    mLastComposeMs = composeMs;
    mLastPresentMs = presentMs;
    mLastLatched = latched;
    mLastDropped = dropped;
    mLastLayerCount = layerCount;
    mStats.intervalMs = configuredIntervalMs;

    if (!mIntervals.empty()) {
        std::vector<double> sorted(mIntervals.begin(), mIntervals.end());
        std::sort(sorted.begin(), sorted.end());
        double sum = 0.0;
        for (double value : sorted) {
            sum += value;
        }
        mStats.fps = 1000.0 * static_cast<double>(sorted.size()) / sum;
        mStats.p50Ms = sorted[sorted.size() / 2];
        mStats.p95Ms = sorted[static_cast<size_t>(sorted.size() * 0.95)];
        mStats.maxMs = sorted.back();
    }

    if (now - mLastSlowRefresh >= std::chrono::milliseconds(500)) {
        refreshSlowStats(false);
    }
}

void PerfOverlay::onSharedFrames(uint64_t totalSwaps) {
    if (!mEnabled) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (mHasSharedTime && totalSwaps > mLastSharedCount) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                now - mLastSharedTime).count();
        if (elapsed > 0) {
            mStats.appFps = static_cast<double>(totalSwaps - mLastSharedCount) * 1e9 /
                    static_cast<double>(elapsed);
        }
        mLastSharedCount = totalSwaps;
        mLastSharedTime = now;
    } else if (!mHasSharedTime) {
        mLastSharedCount = totalSwaps;
        mLastSharedTime = now;
        mHasSharedTime = true;
    }
    mStats.zeroCopy = true;
    mStats.swaps = totalSwaps;
    if (mHasSharedTime && now - mLastSharedTime > std::chrono::seconds(1)) {
        // The shared app surface stopped swapping.  Keep the composer's
        // presented FPS in mStats.fps and report the app as idle separately.
        mStats.appFps = 0.0;
    }
}

void PerfOverlay::refreshSlowStats(bool force) {
    (void)force;
    mStats.composeMs = mLastComposeMs;
    mStats.presentMs = mLastPresentMs;
    mStats.latched = mLastLatched;
    mStats.dropped = mLastDropped;
    mStats.late = mLateFrames;
    mStats.layerCount = mLastLayerCount;
    mStats.presenter = mPresenterName;
    mStats.renderer = mRenderer;
    mStats.hwui = true;
    mStats.rssMb = readProcessMemoryMb("VmRSS:", "/proc/self/status");
    mStats.peakMb = readProcessMemoryMb("VmHWM:", "/proc/self/status");
    mStats.pssMb = readProcessMemoryMb("Pss:", "/proc/self/smaps_rollup");
    mStats.cpuPercent = readProcessCpuPercent();
    mStats.vramMb = readVramMb();
    mLastSlowRefresh = std::chrono::steady_clock::now();
    if (std::getenv("DROIDLET_PERF_OVERLAY_DEBUG") != nullptr) {
        std::fprintf(stderr,
                "perf-overlay: fps=%.1f frame=%.1f zeroCopy=%d swaps=%llu "
                "present=%llu latched=%llu layers=%d shared=%llu\n",
                mStats.fps, mStats.frameMs, mStats.zeroCopy ? 1 : 0,
                static_cast<unsigned long long>(mStats.swaps),
                static_cast<unsigned long long>(mLastPresentCount),
                static_cast<unsigned long long>(mLastLatched), mLastLayerCount,
                static_cast<unsigned long long>(mLastSharedCount));
    }
}

double PerfOverlay::readProcessMemoryMb(const char* key, const char* path) {
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        if (line.rfind(key, 0) != 0) {
            continue;
        }
        std::istringstream stream(line.substr(std::strlen(key)));
        double value = 0.0;
        std::string unit;
        stream >> value >> unit;
        if (unit == "kB" || unit == "KB") {
            return value / 1024.0;
        }
        return value / (1024.0 * 1024.0);
    }
    return 0.0;
}

double PerfOverlay::readProcessCpuPercent() {
    static long clockTicks = sysconf(_SC_CLK_TCK);
    static auto lastTime = std::chrono::steady_clock::now();
    static uint64_t lastTicks = 0;

    std::ifstream file("/proc/self/stat");
    std::string line;
    if (!std::getline(file, line)) {
        return 0.0;
    }
    const size_t close = line.rfind(')');
    if (close == std::string::npos) {
        return 0.0;
    }
    std::istringstream stream(line.substr(close + 1));
    std::string state;
    if (!(stream >> state)) {
        return 0.0;
    }
    // Fields after state: ppid, pgrp, session, tty_nr, tpgid, flags,
    // minflt, cminflt, majflt, cmajflt, utime, stime.
    long value = 0;
    long utime = 0;
    long stime = 0;
    for (int i = 0; i < 12; ++i) {
        if (!(stream >> value)) {
            return 0.0;
        }
        if (i == 10) {
            utime = value;
        } else if (i == 11) {
            stime = value;
        }
    }
    const uint64_t ticks = static_cast<uint64_t>(utime + stime);
    const auto now = std::chrono::steady_clock::now();
    const double seconds =
            std::chrono::duration<double>(now - lastTime).count();
    double percent = 0.0;
    if (seconds > 0.0 && lastTicks != 0) {
        percent = (static_cast<double>(ticks - lastTicks) / clockTicks) * 100.0 / seconds;
    }
    lastTicks = ticks;
    lastTime = now;
    return std::clamp(percent, 0.0, 100.0 * 64.0);
}

double PerfOverlay::readVramMb() {
    for (int card = 0; card < 8; ++card) {
        char path[128];
        std::snprintf(path, sizeof(path),
                      "/sys/class/drm/card%d/device/mem_info_vram_used", card);
        std::ifstream file(path);
        long long bytes = 0;
        if (file >> bytes && bytes > 0) {
            return static_cast<double>(bytes) / (1024.0 * 1024.0);
        }
    }
    return 0.0;
}

void PerfOverlay::probeRenderer() {
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        mRenderer = "NO EGL";
        return;
    }

    EGLint major = 0;
    EGLint minor = 0;
    if (!eglInitialize(display, &major, &minor)) {
        mRenderer = "EGL INIT FAILED";
        return;
    }
    eglBindAPI(EGL_OPENGL_ES_API);

    const EGLint configAttrs[] = {
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_NONE,
    };
    EGLConfig config = nullptr;
    EGLint configCount = 0;
    if (!eglChooseConfig(display, configAttrs, &config, 1, &configCount) || configCount == 0) {
        mRenderer = "NO EGL CONFIG";
        eglTerminate(display);
        return;
    }

    const EGLint pbufferAttrs[] = {
            EGL_WIDTH, 1,
            EGL_HEIGHT, 1,
            EGL_NONE,
    };
    EGLSurface surface = eglCreatePbufferSurface(display, config, pbufferAttrs);
    const EGLint contextAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttrs);
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(display, surface, surface, context)) {
        mRenderer = "NO EGL CONTEXT";
        if (context != EGL_NO_CONTEXT) {
            eglDestroyContext(display, context);
        }
        if (surface != EGL_NO_SURFACE) {
            eglDestroySurface(display, surface);
        }
        eglTerminate(display);
        return;
    }

    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    mRenderer = renderer != nullptr ? renderer : "UNKNOWN GL";

    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
}

void PerfOverlay::draw(std::vector<uint32_t>& frame, int width, int height) const {
    drawInternal(frame, width, height, false);
}

void PerfOverlay::drawOverlay(std::vector<uint32_t>& frame, int width, int height) const {
    drawInternal(frame, width, height, true);
}

void PerfOverlay::drawInternal(std::vector<uint32_t>& frame, int width, int height,
                               bool alphaBlend) const {
    if (!mEnabled || width <= 0 || height <= 0 || frame.empty()) {
        return;
    }

    char fps[128];
    char frameTime[128];
    char pacing[128];
    char timing[128];
    char counters[128];
    char memory[160];
    char system[160];
    char backend[128];
    char renderer[160];

    std::snprintf(fps, sizeof(fps), "FPS %.1f  FRAME %.1fMS", mStats.fps, mStats.frameMs);
    std::snprintf(frameTime, sizeof(frameTime), "P50 %.1f  P95 %.1f  MAX %.1f",
                  mStats.p50Ms, mStats.p95Ms, mStats.maxMs);
    std::snprintf(pacing, sizeof(pacing), "INTERVAL %dMS  LATE %llu",
                  mStats.intervalMs, static_cast<unsigned long long>(mStats.late));
    std::snprintf(timing, sizeof(timing), "COMPOSE %.1fMS  PRESENT %.1fMS",
                  mStats.composeMs, mStats.presentMs);
    if (mStats.zeroCopy) {
        std::snprintf(counters, sizeof(counters), "APP FPS %.1f  SWAPS %llu  ZEROCOPY",
                      mStats.appFps, static_cast<unsigned long long>(mStats.swaps));
    } else {
        std::snprintf(counters, sizeof(counters), "LATCH %llu  DROP %llu  LAYERS %d",
                      static_cast<unsigned long long>(mStats.latched),
                      static_cast<unsigned long long>(mStats.dropped), mStats.layerCount);
    }
    std::snprintf(memory, sizeof(memory), "RSS %.0fMB  PSS %.0fMB  PEAK %.0fMB",
                  mStats.rssMb, mStats.pssMb, mStats.peakMb);
    std::snprintf(system, sizeof(system), "CPU %.1f%%  VRAM %.0fMB",
                  mStats.cpuPercent, mStats.vramMb);
    std::snprintf(backend, sizeof(backend), "%s  %s",
                  "HWUI", mStats.presenter.c_str());
    std::snprintf(renderer, sizeof(renderer), "%.72s", mStats.renderer.c_str());

    if (std::getenv("DROIDLET_PERF_OVERLAY_DEBUG") != nullptr) {
        static auto lastLog = std::chrono::steady_clock::now();
        const auto now = std::chrono::steady_clock::now();
        if (now - lastLog >= std::chrono::seconds(1)) {
            lastLog = now;
            std::fprintf(stderr, "perf-overlay-draw: %s | %s\n", fps, counters);
        }
    }

    const std::string lines[] = {fps, frameTime, pacing, timing, counters, memory,
                                 system, backend, renderer};
    const int scale = kScale;
    int maxWidth = 0;
    for (const auto& line : lines) {
        maxWidth = std::max(maxWidth, textWidth(line, scale));
    }
    const int padding = 6 * scale;
    const int lineHeight = kLineAdvance * scale;
    const int panelWidth = maxWidth + padding * 2;
    const int panelHeight = static_cast<int>(std::size(lines)) * lineHeight + padding * 2;
    const int panelX = 8;
    const int panelY = 8;
    const int panelRight = std::min(width, panelX + panelWidth);
    const int panelBottom = std::min(height, panelY + panelHeight);

    for (int y = panelY; y < panelBottom; ++y) {
        for (int x = panelX; x < panelRight; ++x) {
            blendPixel(frame[static_cast<size_t>(y) * width + x], kPanelColor, alphaBlend);
        }
    }

    int y = panelY + padding;
    for (size_t i = 0; i < std::size(lines); ++i) {
        const uint32_t color = (i == 0 || i == 6) ? kLabelColor : kTextColor;
        drawText(frame, width, height, panelX + padding, y, lines[i], color, scale,
                 alphaBlend);
        y += lineHeight;
    }
}

}  // namespace droidlet

/*
 * See input_injector.h.
 *
 * The publisher writes real InputMessage structs into the same socket the app's
 * InputEventReceiver reads, so this is the same wire format a real input
 * dispatcher would produce for one window.
 */

#include "input_injector.h"

#include <cstdio>
#include <cstdlib>

#include "host_composer.h"

#include <android-base/logging.h>
#include <android/input.h>
#include <android_view_InputChannel.h>
#include <input/InputTransport.h>
#include <ui/LogicalDisplayId.h>
#include <ui/Transform.h>
#include <utils/Timers.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace droidlet {
namespace {

using android::InputChannel;
using android::InputPublisher;

std::mutex gMutex;
std::map<int, std::unique_ptr<InputPublisher>> gPublishers;
int gNextChannelId = 1;
int gActiveChannelId = 0;
bool gPointerDown = false;
float gInputOffsetX = 0.0f;
float gInputOffsetY = 0.0f;
float gInputInsetX = 0.0f;
float gInputInsetY = 0.0f;
int gActiveInputLayer = 0;
nsecs_t gDownTime = 0;
uint32_t gNextSequence = 1;
int32_t gNextEventId = 1;

nsecs_t toNanos(int64_t eventTimeMs) {
    if (eventTimeMs > 0) {
        return static_cast<nsecs_t>(eventTimeMs) * 1000000LL;
    }
    return ::systemTime(::SYSTEM_TIME_MONOTONIC);
}

InputPublisher* activePublisherLocked() {
    auto entry = gPublishers.find(gActiveChannelId);
    return entry != gPublishers.end() ? entry->second.get() : nullptr;
}

uint32_t nextSequence() {
    uint32_t value = gNextSequence++;
    if (value == 0) {
        value = gNextSequence++;
    }
    return value;
}

void sendMotionLocked(int action, float x, float y, int source, int actionButton,
                      int buttonState, uint32_t metaState, float hscroll, float vscroll,
                      nsecs_t eventTime) {
    InputPublisher* publisher = activePublisherLocked();
    if (publisher == nullptr) {
        return;
    }
    float offsetX = gInputOffsetX;
    float offsetY = gInputOffsetY;
    float layerX = 0.0f;
    float layerY = 0.0f;
    if (gActiveInputLayer > 0 && hostLayerPosition(gActiveInputLayer, &layerX, &layerY)) {
        offsetX = layerX + gInputInsetX;
        offsetY = layerY + gInputInsetY;
    }
    const float rawX = x;
    const float rawY = y;
    x -= offsetX;
    y -= offsetY;
    if (const char* debug = std::getenv("DROIDLET_INPUT_DEBUG");
            debug != nullptr && debug[0] == '1') {
        std::fprintf(stderr,
                     "input: action=%d raw=%.1f,%.1f offset=%.1f,%.1f -> %.1f,%.1f\n",
                     action, rawX, rawY, offsetX, offsetY, x, y);
    }
    if (action == AMOTION_EVENT_ACTION_DOWN) {
        gDownTime = eventTime;
    }
    const nsecs_t downTime =
            (action == AMOTION_EVENT_ACTION_HOVER_MOVE || action == AMOTION_EVENT_ACTION_SCROLL)
            ? 0
            : gDownTime;

    android::PointerProperties properties;
    properties.clear();
    properties.id = 0;
    properties.toolType = (source == AINPUT_SOURCE_MOUSE)
            ? android::ToolType::MOUSE
            : android::ToolType::FINGER;

    android::PointerCoords coords;
    coords.clear();
    coords.setAxisValue(AMOTION_EVENT_AXIS_X, x);
    coords.setAxisValue(AMOTION_EVENT_AXIS_Y, y);
    if (source == AINPUT_SOURCE_TOUCHSCREEN) {
        coords.setAxisValue(AMOTION_EVENT_AXIS_PRESSURE, 1.0f);
        coords.setAxisValue(AMOTION_EVENT_AXIS_SIZE, 1.0f);
    }
    if (hscroll != 0.0f) {
        coords.setAxisValue(AMOTION_EVENT_AXIS_HSCROLL, hscroll);
    }
    if (vscroll != 0.0f) {
        coords.setAxisValue(AMOTION_EVENT_AXIS_VSCROLL, vscroll);
    }

    const android::ui::Transform identity;
    const android::status_t status = publisher->publishMotionEvent(
            nextSequence(), gNextEventId++, 0 /* deviceId */, source,
            android::ui::LogicalDisplayId::DEFAULT, {} /* hmac */, action, actionButton,
            0 /* flags */, static_cast<int32_t>(metaState), buttonState,
            android::MotionClassification::NONE, identity, 1.0f, 1.0f, x, y, identity,
            downTime, eventTime, 1 /* pointerCount */, &properties, &coords);
    if (status != android::OK) {
        LOG(WARNING) << "input injection: publishMotionEvent failed: " << status;
    }

    if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL) {
        gDownTime = 0;
    }
}

void sendKeyLocked(int action, int keyCode, int scanCode, uint32_t metaState,
                   nsecs_t eventTime) {
    InputPublisher* publisher = activePublisherLocked();
    if (publisher == nullptr || keyCode == 0) {
        return;
    }
    if (action == AKEY_EVENT_ACTION_DOWN) {
        if (gDownTime == 0) {
            gDownTime = eventTime;
        }
    }
    const nsecs_t downTime = gDownTime != 0 ? gDownTime : eventTime;
    const android::status_t status = publisher->publishKeyEvent(
            nextSequence(), gNextEventId++, 0 /* deviceId */, AINPUT_SOURCE_KEYBOARD,
            android::ui::LogicalDisplayId::DEFAULT, {} /* hmac */, action, 0 /* flags */,
            keyCode, scanCode, static_cast<int32_t>(metaState), 0 /* repeatCount */,
            downTime, eventTime);
    if (status != android::OK) {
        LOG(WARNING) << "input injection: publishKeyEvent failed: " << status;
    }
    if (action == AKEY_EVENT_ACTION_UP) {
        gDownTime = 0;
    }
}

struct ScriptEvent {
    int64_t timeMs = 0;
    std::string command;
    std::vector<std::string> args;
};

std::vector<ScriptEvent> gScript;
bool gScriptLoaded = false;
std::atomic<bool> gScriptFinished{false};
bool gScriptStarted = false;
size_t gScriptCursor = 0;
std::chrono::steady_clock::time_point gScriptStart;
int64_t gScriptBaseMs = 0;

void loadScript() {
    if (gScriptLoaded) {
        return;
    }
    gScriptLoaded = true;

    std::string text;
    const char* path = std::getenv("DROIDLET_INPUT_SCRIPT");
    const char* inlineEvents = std::getenv("DROIDLET_INPUT_EVENTS");
    if (path != nullptr && path[0] != '\0') {
        std::ifstream file(path);
        if (!file) {
            LOG(ERROR) << "input script: unable to open " << path;
            return;
        }
        std::string line;
        while (std::getline(file, line)) {
            text += line;
            text += '\n';
        }
        LOG(INFO) << "input script: loaded " << path;
    } else if (inlineEvents != nullptr && inlineEvents[0] != '\0') {
        text = inlineEvents;
        for (char& c : text) {
            if (c == ';') {
                c = '\n';
            }
        }
        LOG(INFO) << "input script: using DROIDLET_INPUT_EVENTS";
    } else {
        return;
    }

    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        const size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        std::istringstream fields(line);
        ScriptEvent event;
        std::string timeText;
        if (!(fields >> timeText >> event.command)) {
            continue;
        }
        char* end = nullptr;
        event.timeMs = std::strtoll(timeText.c_str(), &end, 10);
        if (end == timeText.c_str() || *end != '\0') {
            LOG(ERROR) << "input script: bad timestamp '" << timeText << "'";
            gScript.clear();
            return;
        }
        std::string arg;
        while (fields >> arg) {
            event.args.push_back(arg);
        }
        gScript.push_back(std::move(event));
    }
    LOG(INFO) << "input script: " << gScript.size() << " events";
}

void runScriptEvent(const ScriptEvent& event) {
    const auto args = event.args;
    auto number = [&](size_t index, float fallback) -> float {
        return index < args.size() ? std::strtof(args[index].c_str(), nullptr) : fallback;
    };
    const int64_t time = gScriptBaseMs + event.timeMs;
    if (event.command == "down" || event.command == "tap") {
        injectTouchDown(number(0, 0), number(1, 0), time);
        if (event.command == "tap") {
            injectTouchUp(number(0, 0), number(1, 0), time + 20);
        }
    } else if (event.command == "move") {
        if (gPointerDown) {
            injectTouchMove(number(0, 0), number(1, 0), time);
        } else {
            injectHoverMove(number(0, 0), number(1, 0), time);
        }
    } else if (event.command == "up") {
        injectTouchUp(number(0, 0), number(1, 0), time);
    } else if (event.command == "cancel") {
        injectTouchCancel(number(0, 0), number(1, 0), time);
    } else if (event.command == "hover") {
        injectHoverMove(number(0, 0), number(1, 0), time);
    } else if (event.command == "scroll") {
        injectScroll(number(0, 0), number(1, 0), number(3, 0), number(2, 0), time);
    } else if (event.command == "key" && args.size() >= 2) {
        const bool down = args[0] == "down";
        const int keyCode = static_cast<int>(std::strtol(args[1].c_str(), nullptr, 0));
        const uint32_t meta = args.size() >= 3
                ? static_cast<uint32_t>(std::strtoul(args[2].c_str(), nullptr, 0))
                : 0;
        injectKey(down ? AKEY_EVENT_ACTION_DOWN : AKEY_EVENT_ACTION_UP, keyCode, 0, meta, time);
    } else {
        LOG(WARNING) << "input script: unknown command '" << event.command << "'";
    }
}

}  // namespace

jobject createClientInputChannel(JNIEnv* env) {
    std::lock_guard<std::mutex> lock(gMutex);

    std::unique_ptr<InputChannel> server;
    std::unique_ptr<InputChannel> client;
    const android::status_t status = InputChannel::openInputChannelPair("droidlet", server, client);
    if (status != android::OK) {
        LOG(ERROR) << "unable to open input channel pair: " << status;
        return nullptr;
    }

    const int id = gNextChannelId++;
    gPublishers[id] = std::make_unique<InputPublisher>(std::move(server));
    // Do not switch input to the new channel here.  The launcher decides which
    // window is on top and calls setActiveInputChannel(); a window that is not
    // touchable (tooltip, media overlay) must never steal host input from the
    // window below it.  Creating a tooltip used to leave every later pointer
    // event on the tooltip channel, which made the visible popup look frozen.
    jobject javaClient = android::android_view_InputChannel_createJavaObject(
            env, std::move(client));
    if (javaClient == nullptr) {
        gPublishers.erase(id);
        return nullptr;
    }
    LOG(INFO) << "input channel " << id << " ready";
    return javaClient;
}

bool inputReady() {
    std::lock_guard<std::mutex> lock(gMutex);
    return activePublisherLocked() != nullptr;
}

int lastInputChannelId() {
    std::lock_guard<std::mutex> lock(gMutex);
    return gNextChannelId > 1 ? gNextChannelId - 1 : 0;
}

int activeInputChannelId() {
    std::lock_guard<std::mutex> lock(gMutex);
    return gActiveChannelId;
}

void setActiveInputChannel(int id) {
    std::lock_guard<std::mutex> lock(gMutex);
    if (gPublishers.find(id) != gPublishers.end()) {
        gActiveChannelId = id;
    }
}

void setInputWindowOffset(float x, float y) {
    std::lock_guard<std::mutex> lock(gMutex);
    gInputOffsetX = x;
    gInputOffsetY = y;
}

void setInputWindowInset(float x, float y) {
    std::lock_guard<std::mutex> lock(gMutex);
    gInputInsetX = x;
    gInputInsetY = y;
}

void setActiveInputLayer(int layerId) {
    std::lock_guard<std::mutex> lock(gMutex);
    gActiveInputLayer = layerId;
}

void injectTouchDown(float x, float y, int64_t eventTimeMs, uint32_t metaState) {
    std::lock_guard<std::mutex> lock(gMutex);
    // Always close any previous gesture before a new press.  Some apps only
    // leave their steering state when they see an UP, and a lost/consumed
    // release would otherwise leave them stuck at the last target and ignore
    // every subsequent press.  A stray UP without a DOWN is ignored by normal
    // Android UI code.
    sendMotionLocked(AMOTION_EVENT_ACTION_UP, x, y, AINPUT_SOURCE_TOUCHSCREEN, 0,
                     0, metaState, 0, 0, toNanos(eventTimeMs));
    gPointerDown = true;
    sendMotionLocked(AMOTION_EVENT_ACTION_DOWN, x, y, AINPUT_SOURCE_TOUCHSCREEN, 0,
                     0, metaState, 0, 0, toNanos(eventTimeMs));
    // Follow the press with a move at the same point.  Games that remember a
    // steering target across gestures (Endless Tunnel) only recompute it on a
    // move; without this the ship jumps to the previous gesture's target
    // before the first real drag event arrives.
    sendMotionLocked(AMOTION_EVENT_ACTION_MOVE, x, y, AINPUT_SOURCE_TOUCHSCREEN, 0,
                     0, metaState, 0, 0, toNanos(eventTimeMs));
}

void injectTouchMove(float x, float y, int64_t eventTimeMs, uint32_t metaState) {
    std::lock_guard<std::mutex> lock(gMutex);
    sendMotionLocked(AMOTION_EVENT_ACTION_MOVE, x, y, AINPUT_SOURCE_TOUCHSCREEN, 0,
                     0, metaState, 0, 0, toNanos(eventTimeMs));
}

void injectTouchUp(float x, float y, int64_t eventTimeMs, uint32_t metaState) {
    std::lock_guard<std::mutex> lock(gMutex);
    if (gPointerDown) {
        // Android may coalesce or drop motion events while the app is busy.
        // Apply the release position before ending the gesture so a fast drag
        // cannot leave the app steering toward an older target.
        sendMotionLocked(AMOTION_EVENT_ACTION_MOVE, x, y, AINPUT_SOURCE_TOUCHSCREEN, 0,
                         0, metaState, 0, 0, toNanos(eventTimeMs));
    }
    sendMotionLocked(AMOTION_EVENT_ACTION_UP, x, y, AINPUT_SOURCE_TOUCHSCREEN, 0,
                     0, metaState, 0, 0, toNanos(eventTimeMs));
    gPointerDown = false;
}

void injectTouchCancel(float x, float y, int64_t eventTimeMs, uint32_t metaState) {
    std::lock_guard<std::mutex> lock(gMutex);
    sendMotionLocked(AMOTION_EVENT_ACTION_CANCEL, x, y, AINPUT_SOURCE_TOUCHSCREEN, 0,
                     0, metaState, 0, 0, toNanos(eventTimeMs));
    gPointerDown = false;
}

void injectHoverMove(float x, float y, int64_t eventTimeMs, uint32_t metaState) {
    std::lock_guard<std::mutex> lock(gMutex);
    sendMotionLocked(AMOTION_EVENT_ACTION_HOVER_MOVE, x, y, AINPUT_SOURCE_MOUSE, 0,
                     0, metaState, 0, 0, toNanos(eventTimeMs));
}

void injectScroll(float x, float y, float hscroll, float vscroll, int64_t eventTimeMs,
                  uint32_t metaState) {
    std::lock_guard<std::mutex> lock(gMutex);
    sendMotionLocked(AMOTION_EVENT_ACTION_SCROLL, x, y, AINPUT_SOURCE_MOUSE, 0,
                     0, metaState, hscroll, vscroll, toNanos(eventTimeMs));
}

void injectKey(int action, int keyCode, int scanCode, uint32_t metaState,
               int64_t eventTimeMs) {
    std::lock_guard<std::mutex> lock(gMutex);
    sendKeyLocked(action, keyCode, scanCode, metaState, toNanos(eventTimeMs));
}

void injectFocus(bool hasFocus) {
    std::lock_guard<std::mutex> lock(gMutex);
    InputPublisher* publisher = activePublisherLocked();
    if (publisher == nullptr) {
        return;
    }
    const android::status_t status = publisher->publishFocusEvent(
            nextSequence(), gNextEventId++, hasFocus);
    if (status != android::OK) {
        LOG(WARNING) << "input injection: publishFocusEvent failed: " << status;
    }
}

void injectTouchMode(bool inTouchMode) {
    std::lock_guard<std::mutex> lock(gMutex);
    InputPublisher* publisher = activePublisherLocked();
    if (publisher == nullptr) {
        return;
    }
    const android::status_t status = publisher->publishTouchModeEvent(
            nextSequence(), gNextEventId++, inTouchMode);
    if (status != android::OK) {
        LOG(WARNING) << "input injection: publishTouchModeEvent failed: " << status;
    }
}

void pumpInputResponses() {
    std::lock_guard<std::mutex> lock(gMutex);
    for (auto& entry : gPublishers) {
        for (;;) {
            auto response = entry.second->receiveConsumerResponse();
            if (!response.ok()) {
                break;
            }
            // Finished/timeline responses only acknowledge delivery.
        }
    }
}

bool inputScriptFinished() {
    return gScriptFinished.load(std::memory_order_relaxed);
}

void pumpInputScript() {
    loadScript();
    if (gScript.empty() || !inputReady()) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (!gScriptStarted) {
        gScriptStarted = true;
        gScriptStart = now;
        gScriptBaseMs = ::systemTime(::SYSTEM_TIME_MONOTONIC) / 1000000;
    }
    const int64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - gScriptStart).count();
    while (gScriptCursor < gScript.size() && gScript[gScriptCursor].timeMs <= elapsed) {
        runScriptEvent(gScript[gScriptCursor]);
        ++gScriptCursor;
    }
    if (gScriptCursor == gScript.size()) {
        LOG(INFO) << "input script: finished";
        gScript.clear();
        gScriptFinished.store(true, std::memory_order_relaxed);
    }
}

}  // namespace droidlet

// Goal: Manage bounded RGB effects requested by the firmware.
// Responsibilities: Validate colors and durations and track effect state.
// How it works:
// 1. Requests are checked and colors are converted to RGB565.
// 2. The controller starts an admitted effect during a tick.
// 3. Completion, cancellation, and errors are recorded for callers.
#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace lab {

inline constexpr uint32_t kRgbMinimumDurationMs = 100;
inline constexpr uint32_t kRgbMaximumDurationMs = 30000;

inline bool rgbColorValid(const char* color) {
    if (!color || color[0] != '#' || strlen(color) != 7) return false;
    for (int i = 1; i < 7; ++i) {
        char value = color[i];
        if (!((value >= '0' && value <= '9') ||
              (value >= 'A' && value <= 'F') ||
              (value >= 'a' && value <= 'f'))) return false;
    }
    return true;
}

inline uint16_t rgb565(const char* color) {
    uint8_t rgb[3]{};
    for (int i = 0; i < 3; ++i) {
        const char* digits = color + 1 + i * 2;
        char pair[3] = {digits[0], digits[1], '\0'};
        rgb[i] = static_cast<uint8_t>(strtoul(pair, nullptr, 16));
    }
    return static_cast<uint16_t>(((rgb[0] & 0xF8) << 8) |
                                 ((rgb[1] & 0xFC) << 3) |
                                 (rgb[2] >> 3));
}

class RgbController {
public:
    virtual ~RgbController() = default;
    virtual bool pulseRgb(uint32_t now, bool quiet, uint16_t color, uint32_t durationMs,
                          bool available) = 0;
    virtual bool rgbActive() const = 0;
    virtual bool rgbAvailable() const = 0;
};

class RgbEffect {
public:
    const char* request(const char* color, uint32_t durationMs, bool eligible, bool busy,
                        uint32_t stopEpoch) {
        if (!rgbColorValid(color)) return "invalid_rgb_color";
        if (durationMs < kRgbMinimumDurationMs || durationMs > kRgbMaximumDurationMs)
            return "rgb_duration_invalid";
        if (!eligible) return "rgb_unavailable";
        if (busy || state == Pending || state == Active) return "rgb_busy";
        if (requestId == UINT32_MAX) return "rgb_ids_exhausted";
        ++requestId;
        requestedColor = rgb565(color);
        requestedDurationMs = durationMs;
        requestStopEpoch = stopEpoch;
        lastError = nullptr;
        state = Pending;
        return nullptr;
    }

    bool tick(uint32_t now, RgbController& controller, bool stopped, bool quiet, bool available) {
        if (state == Pending) {
            if (stopped) {
                state = Canceled;
                return true;
            }
            if (!controller.pulseRgb(now, quiet, requestedColor, requestedDurationMs, available)) {
                lastError = "rgb_write_failed";
                state = Failed;
                return true;
            }
            startedAt = now;
            state = Active;
            return true;
        }
        if (state == Active) {
            if (stopped) {
                state = Canceled;
                return true;
            }
            if (!controller.rgbActive()) {
                state = Complete;
                return true;
            }
            if (static_cast<uint32_t>(now - startedAt) >= requestedDurationMs) {
                state = Complete;
                return true;
            }
        }
        return false;
    }

    uint32_t id() const { return requestId; }
    const char* stateName() const {
        switch (state) {
            case Pending: return "queued";
            case Active: return "active";
            case Complete: return "completed";
            case Canceled: return "canceled";
            case Failed: return "failed";
            default: return "idle";
        }
    }
    const char* error() const { return lastError; }

private:
    enum State { Idle, Pending, Active, Complete, Canceled, Failed };
    State state = Idle;
    uint32_t requestId = 0, requestStopEpoch = 0, requestedDurationMs = 0, startedAt = 0;
    uint16_t requestedColor = 0;
    const char* lastError = nullptr;
};

} // namespace lab

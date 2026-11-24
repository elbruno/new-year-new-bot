// Goal: Define the firmware's display brightness contract.
// Responsibilities: Share brightness bounds and queued-update status.
// How it works:
// 1. Callers use the constants to constrain brightness choices.
// 2. Queue results describe whether a request was accepted.
// 3. Status reports the current and requested values and any error.
#pragma once

#include <stdint.h>

namespace lab {

inline constexpr uint8_t kDisplayBrightnessMinimum = 32;
inline constexpr uint8_t kDisplayBrightnessDefault = kDisplayBrightnessMinimum;
inline constexpr uint8_t kDisplayBrightnessMaximum = 192;
inline constexpr uint8_t kDisplayBrightnessStep = 16;

enum class DisplayBrightnessQueueResult { Accepted, Unavailable, Busy, OutOfRange };

struct DisplayBrightnessStatus {
    uint8_t brightness = kDisplayBrightnessDefault;
    uint8_t requestedBrightness = kDisplayBrightnessDefault;
    bool pending = false;
    const char* error = nullptr;
};

}  // namespace lab

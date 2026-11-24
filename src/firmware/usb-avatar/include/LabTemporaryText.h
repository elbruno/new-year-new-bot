// Goal: Show a bounded temporary text message on the robot display.
// Responsibilities: Validate text and track display ownership and cancellation.
// How it works:
// 1. An eligible request queues a validated message.
// 2. Ticks start the display and watch its duration and Stop state.
// 3. Completion or cancellation allows the previous display to be restored.
#pragma once

#include <stdint.h>
#include <string.h>

namespace lab {

inline constexpr uint32_t kTemporaryTextDurationMs = 5000;
inline constexpr size_t kTemporaryTextLimit = 128;

inline bool temporaryTextValid(const char* text) {
    if (!text) return false;
    const size_t length = strnlen(text, kTemporaryTextLimit + 1);
    if (length == 0 || length > kTemporaryTextLimit) return false;
    for (size_t i = 0; i < length; ++i) {
        const unsigned char value = static_cast<unsigned char>(text[i]);
        if (value < 0x20 && value != 0x09) return false;
    }
    return true;
}

class TemporaryText {
public:
    bool active() const {
        return state == Queued || state == Displaying;
    }
    bool resourceBusy() const { return active(); }
    uint32_t id() const { return requestId; }
    const char* text() const { return message; }

    const char* request(const char* value, bool eligible, bool busy, uint32_t stopEpoch) {
        if (!temporaryTextValid(value)) return "invalid_text";
        if (!eligible) return "display_unavailable";
        if (busy || resourceBusy()) return "text_busy";
        if (requestId == UINT32_MAX) return "text_ids_exhausted";
        ++requestId;
        memset(message, 0, sizeof(message));
        strncpy(message, value, kTemporaryTextLimit);
        requestStopEpoch = stopEpoch;
        lastError = nullptr;
        state = Queued;
        return nullptr;
    }

    bool tick(uint32_t now, bool eligible, uint32_t stopEpoch) {
        if (active() && (!eligible || stopEpoch != requestStopEpoch)) cancel();
        if (state == Queued) {
            displayedAt = now;
            state = Displaying;
            return true;
        }
        if (state == Displaying &&
            static_cast<uint32_t>(now - displayedAt) >= kTemporaryTextDurationMs) {
            state = Complete;
            restore = true;
            return true;
        }
        return false;
    }

    bool ownsDisplay() const { return state == Displaying; }
    bool takeRestore() {
        const bool value = restore;
        restore = false;
        return value;
    }

    const char* stateName() const {
        switch (state) {
            case Queued: return "queued";
            case Displaying: return "displaying";
            case Complete: return "completed";
            case Canceled: return "canceled";
            case Failed: return "failed";
            default: return "idle";
        }
    }
    const char* error() const { return lastError; }

    void stop() {
        if (active()) cancel();
    }

private:
    enum State { Idle, Queued, Displaying, Complete, Canceled, Failed };
    State state = Idle;
    char message[kTemporaryTextLimit + 1]{};
    uint32_t requestId = 0, requestStopEpoch = 0, displayedAt = 0;
    const char* lastError = nullptr;
    bool restore = false;

    void cancel() {
        if (state == Displaying) restore = true;
        state = Canceled;
        lastError = nullptr;
    }
};

} // namespace lab

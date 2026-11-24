#pragma once

#include <atomic>
#include <stdint.h>

namespace lab {

constexpr uint32_t kCompanionPresenceExpiryMs = 15000;

class CompanionPresence {
public:
    void update(bool connected, uint32_t now) {
        if (!connected) {
            active.store(false, std::memory_order_release);
            return;
        }
        lastHeartbeat.store(now, std::memory_order_relaxed);
        active.store(true, std::memory_order_release);
    }

    bool present(uint32_t now) const {
        if (!active.load(std::memory_order_acquire)) return false;
        const uint32_t heartbeat = lastHeartbeat.load(std::memory_order_relaxed);
        // The HTTP task can renew after the rendering loop sampled its clock.
        return now - heartbeat < kCompanionPresenceExpiryMs ||
            heartbeat - now < kCompanionPresenceExpiryMs;
    }

private:
    std::atomic<uint32_t> lastHeartbeat{0};
    std::atomic<bool> active{false};
};

}  // namespace lab

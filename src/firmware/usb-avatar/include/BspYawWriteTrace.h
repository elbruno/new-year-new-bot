#pragma once

#include <atomic>
#include <mutex>
#include <stdint.h>

namespace lab {

class BspYawWriteTrace {
public:
    struct Summary {
        unsigned writes = 0, noAck = 0, identical = 0, gapsOver25ms = 0;
        unsigned firstRaw = 0, lastRaw = 0, maxStepRaw = 0, stepAtMaxGapRaw = 0;
        uint32_t firstUs = 0, lastUs = 0, minGapUs = 0, maxGapUs = 0;
        uint32_t maxGapAtUs = 0, priorWriteAtMaxGapUs = 0, maxWriteUs = 0;
    };

    void start(uint32_t nowUs) {
        std::lock_guard<std::mutex> lock(mutex_);
        summary_ = {};
        openedUs_ = nowUs;
        priorWriteUs_ = 0;
        active_.store(true, std::memory_order_release);
    }

    bool active() const { return active_.load(std::memory_order_acquire); }

    void record(uint32_t startedUs, uint32_t durationUs, unsigned raw, bool ack) {
        std::lock_guard<std::mutex> lock(mutex_);
        const uint32_t offsetUs = startedUs - openedUs_;
        if (!active() || offsetUs >= 3000000u) return;
        auto& s = summary_;
        if (s.writes) {
            const uint32_t gap = offsetUs - s.lastUs;
            const unsigned step = raw < s.lastRaw ? s.lastRaw - raw : raw - s.lastRaw;
            if (s.writes == 1 || gap < s.minGapUs) s.minGapUs = gap;
            if (s.writes == 1 || gap > s.maxGapUs) {
                s.maxGapUs = gap;
                s.maxGapAtUs = offsetUs;
                s.priorWriteAtMaxGapUs = priorWriteUs_;
                s.stepAtMaxGapRaw = step;
            }
            if (step > s.maxStepRaw) s.maxStepRaw = step;
            s.identical += step == 0;
            s.gapsOver25ms += gap > 25000u;
        } else {
            s.firstUs = offsetUs;
            s.firstRaw = raw;
        }
        ++s.writes;
        s.noAck += !ack;
        s.lastUs = offsetUs;
        s.lastRaw = raw;
        priorWriteUs_ = durationUs;
        if (durationUs > s.maxWriteUs) s.maxWriteUs = durationUs;
    }

    Summary finish() {
        active_.store(false, std::memory_order_release);
        std::lock_guard<std::mutex> lock(mutex_);
        return summary_;
    }

private:
    std::mutex mutex_;
    std::atomic<bool> active_{false};
    Summary summary_;
    uint32_t openedUs_ = 0, priorWriteUs_ = 0;
};

} // namespace lab

// Goal: Coordinate stopping during manual servo diagnostics.
// Responsibilities: Track Stop phases and their confirmation state.
// How it works:
// 1. A Stop request starts the bounded stop sequence.
// 2. Ticks advance the servo transaction phases.
// 3. The result distinguishes confirmed, canceled, and failed outcomes.
#pragma once

#include "LabManualScs.h"

namespace lab {

enum class ManualStopState { Idle, Pending, Confirmed, CancelledWithoutEffect, Failed };

// Only the bus owner drains an already transmitted packet, then attempts each
// torque-off once. A contaminated channel is never used for another packet.
template <typename Scs, typename Clock>
class ManualStop {
public:
    ManualStop(Scs& bus, Clock& clock) : bus(bus), clock(clock) {}
    bool begin(uint8_t id, int minimum, int maximum, int tolerance, uint16_t holdMs,
               uint64_t settleUs, uint64_t freshUs, uint64_t deadlineUs, bool) {
        const uint64_t now = clock.nowUs();
        if (state_ != ManualStopState::Idle || (id != 1 && id != 2) ||
            minimum < 0 || minimum >= maximum || maximum > 1000 ||
            tolerance < 0 || tolerance >= maximum - minimum || !holdMs || !settleUs || !freshUs ||
            deadlineUs <= now || !clock.stopRequested()) return false;
        deadline = deadlineUs;
        lastNow = now;
        state_ = ManualStopState::Pending;
        return true;
    }
    ManualStopState state() const { return state_; }
    bool rearmClosed() { return state_ == ManualStopState::Idle; }
    void tick() {
        if (state_ != ManualStopState::Pending) return;
        const uint64_t now = clock.nowUs();
        if (now < lastNow || now >= deadline || !clock.stopRequested()) { fail(); return; }
        lastNow = now;
        const auto status = bus.state();
        if (status == ManualScsState::Fault) { fail(); return; }
        if (status != ManualScsState::Idle && status != ManualScsState::Complete &&
            status != ManualScsState::Cancelled) {
            bus.tick();
            return;
        }
        if (phase == Phase::Drain) {
            if (!bus.enterStop() || !bus.safetyDisable(1, deadline - now)) { fail(); return; }
            phase = Phase::Yaw;
        } else if (phase == Phase::Yaw) {
            if (!bus.safetyDisable(2, deadline - now)) { fail(); return; }
            phase = Phase::Pitch;
        } else {
            state_ = ManualStopState::Confirmed;
        }
    }
private:
    enum class Phase { Drain, Yaw, Pitch };
    Scs& bus;
    Clock& clock;
    ManualStopState state_ = ManualStopState::Idle;
    Phase phase = Phase::Drain;
    uint64_t deadline = 0, lastNow = 0;
    void fail() {
        bus.abandon();
        state_ = ManualStopState::Failed;
    }
};

} // namespace lab

// Goal: Coordinate a checked manual servo diagnostic session.
// Responsibilities: Manage session phases, feedback, and motion bounds.
// How it works:
// 1. The session prepares power and reads servo configuration and feedback.
// 2. Admitted operations advance through bounded transaction phases.
// 3. Stop and close operations track the resulting state and faults.
#pragma once

#include <string.h>
#include "LabSingleAxisTest.h"
#include "LabManualStop.h"

namespace lab {

enum class ManualSessionState { Idle, Prepared, Powering, Powered, Enabling, Ready, Moving, Stopping, Stopped, Closing, Closed, Fault };

struct ManualFeedback {
    bool supported = false, stationary = false, feedbackOnly = false;
    Pose position{};
};

inline bool localYawTrial(const SingleAxisTestBounds& value, Pose measured, Pose expected) {
    return value.axis == SingleAxis::Yaw &&
        value.expectedCalibration.yaw == expected.yaw && value.expectedCalibration.pitch == expected.pitch &&
        value.minimum >= measured.yaw - 4 && value.maximum <= measured.yaw + 4 &&
        value.minimum <= measured.yaw && value.maximum >= measured.yaw && value.maximumStep <= 4;
}

inline const char* manualSessionStateName(ManualSessionState state) {
    switch (state) {
        case ManualSessionState::Prepared: return "prepared";
        case ManualSessionState::Powering: return "powering";
        case ManualSessionState::Powered: return "powered";
        case ManualSessionState::Enabling: return "enabling";
        case ManualSessionState::Ready: return "ready";
        case ManualSessionState::Moving: return "moving";
        case ManualSessionState::Stopping: return "stopping";
        case ManualSessionState::Stopped: return "stopped";
        case ManualSessionState::Closing: return "closing";
        case ManualSessionState::Closed: return "closed";
        case ManualSessionState::Fault: return "fault";
        default: return "idle";
    }
}

class ManualSessionControl {
public:
    virtual ~ManualSessionControl() = default;
    virtual bool prepare(const SingleAxisTestBounds&, uint32_t) = 0;
    virtual bool requestPower() = 0;
    virtual bool requestFeedbackPower(Pose) { return false; }
    virtual ManualFeedback feedback() const { return {}; }
    virtual bool requestEnable() = 0;
    virtual bool requestMove(int) = 0;
    virtual bool requestReturn() = 0;
    virtual bool requestClose(bool supported) = 0;
    virtual ManualSessionState state() const = 0;
    virtual ManualStopState stopState() const = 0;
    virtual const char* error() const = 0;
    virtual void tick() = 0;
};

// Called only by the single bus owner. Admission and the independent Stop
// latch belong to the caller; this object does not establish physical clearance.
template <typename Scs, typename Uart, typename Power, typename Calibration>
class ManualSession : public ManualSessionControl {
public:
    ManualSession(Scs& bus, Uart& uart, Power& power, Calibration& calibration, MotionLimits limits)
        : bus(bus), uart(uart), power(power), calibration(calibration), limits(limits), stop(bus, uart) {}

    void setDiagnostic(void (*sink)(const char*, const char*)) { diagnostic = sink; }
    bool prepare(const SingleAxisTestBounds& value, uint32_t travel) override {
        const bool observed = state_ == ManualSessionState::Powered && feedbackOnly && captured.stationary;
        if ((state_ != ManualSessionState::Idle && state_ != ManualSessionState::Closed && !observed) ||
            uart.stopRequested() || singleAxisBoundsError(value) ||
            !limits.valid() || value.expectedCalibration.yaw < 0 || value.expectedCalibration.yaw > 1000 ||
            value.expectedCalibration.pitch < 0 || value.expectedCalibration.pitch > 1000 ||
            value.minimum < (value.axis == SingleAxis::Yaw ? limits.yawMinimum : limits.pitchMinimum) ||
            value.maximum > (value.axis == SingleAxis::Yaw ? limits.yawMaximum : limits.pitchMaximum) ||
            value.maximumStep > limits.step || value.maximumStep <= limits.tolerance ||
            travel < limits.travelMs || travel > UINT16_MAX ||
            static_cast<uint64_t>(travel) + limits.settleMs >= limits.timeoutMs ||
            (state_ == ManualSessionState::Closed && value.axis == bounds.axis)) return false;
        // A feedback-first experiment is a local trial, never full-travel calibration.
        if (observed && !localYawTrial(value, captured.position, bounds.expectedCalibration)) return false;
        if (state_ == ManualSessionState::Closed) {
            if (!stop.rearmClosed() || !bus.rearmClosed()) { fail("manual_rearm_failed"); return false; }
            originKnown = returning = movementStarted = stable = movedOut = false;
        }
        bounds = value;
        travelMs = travel;
        feedbackOnly = false;
        state_ = observed ? ManualSessionState::Powered : ManualSessionState::Prepared;
        return true;
    }
    bool requestFeedbackPower(Pose expected) override {
        if (state_ != ManualSessionState::Idle || uart.stopRequested() || !limits.valid() ||
            expected.yaw < 0 || expected.yaw > 1000 || expected.pitch < 0 || expected.pitch > 1000)
            return false;
        // 0..1000 is the register domain for reading, not permission to move.
        bounds = {false, SingleAxis::Yaw, 0, 1000, 0, expected, limits.freshMs};
        feedbackOnly = true;
        state_ = ManualSessionState::Powering;
        phase = Phase::PowerInit;
        return true;
    }
    ManualFeedback feedback() const override {
        auto result = captured;
        result.supported = true;
        result.feedbackOnly = feedbackOnly;
        return result;
    }
    bool requestPower() override {
        if (state_ != ManualSessionState::Prepared || uart.stopRequested()) return false;
        if (powerReady) { state_ = ManualSessionState::Powered; return true; }
        state_ = ManualSessionState::Powering;
        phase = Phase::PowerInit;
        return true;
    }
    bool requestEnable() override {
        if (state_ != ManualSessionState::Powered || !bounds.reviewed || uart.stopRequested()) return false;
        state_ = ManualSessionState::Enabling;
        if (readWord(selected(), 9, Phase::Minimum)) return true;
        fail("manual_bus_failed");
        return false;
    }
    bool requestMove(int delta) override {
        if (state_ != ManualSessionState::Ready || uart.stopRequested() || movedOut || !delta ||
            delta < -bounds.maximumStep || delta > bounds.maximumStep) return false;
        requestedDelta = delta;
        returning = false;
        state_ = ManualSessionState::Moving;
        if (startSample(AfterSample::Move)) return true;
        fail("manual_bus_failed");
        return false;
    }
    bool requestReturn() override {
        if (state_ != ManualSessionState::Ready || uart.stopRequested() || !originKnown || !movedOut) return false;
        returning = true;
        state_ = ManualSessionState::Moving;
        if (startSample(AfterSample::Move)) return true;
        fail("manual_bus_failed");
        return false;
    }
    bool requestClose(bool supported) override {
        if (!supported || uart.stopRequested() || state_ != ManualSessionState::Ready) return false;
        closeAt = uart.nowUs();
        closeStable = false;
        closeInterrupted = false;
        state_ = ManualSessionState::Closing;
        if (startSample(AfterSample::CloseBefore)) return true;
        fail("manual_close_unconfirmed");
        return false;
    }
    ManualSessionState state() const override { return state_; }
    ManualStopState stopState() const override { return stop.state(); }
    const char* error() const override { return error_; }
    void tick() override {
        if (uart.stopRequested() && !stopFinished) {
            const uint64_t now = uart.nowUs();
            if (state_ != ManualSessionState::Stopping) {
                stopHadFault = state_ == ManualSessionState::Fault;
                closeInterrupted = state_ == ManualSessionState::Closing;
                power.cancelManualPower();
                powerReady = originKnown = returning = movementStarted = stable = movedOut = false;
                const uint64_t timeout = static_cast<uint64_t>(limits.timeoutMs) * 1000;
                if (now < lastNow || UINT64_MAX - now < timeout ||
                    !stop.begin(selected(), bounds.minimum, bounds.maximum, limits.tolerance,
                        static_cast<uint16_t>(limits.stopMs), static_cast<uint64_t>(limits.settleMs) * 1000,
                        static_cast<uint64_t>(bounds.maximumFeedbackAgeMs) * 1000,
                        now + timeout, possibleEffect)) {
                    finishStop(false);
                    return;
                }
                state_ = ManualSessionState::Stopping;
            }
            stop.tick();
            if (stop.state() == ManualStopState::Confirmed) finishStop(true);
            else if (stop.state() == ManualStopState::Failed) finishStop(false);
            return;
        }
        if (state_ == ManualSessionState::Idle || state_ == ManualSessionState::Stopped ||
            state_ == ManualSessionState::Fault || stopFinished) return;
        const uint64_t now = uart.nowUs();
        if (now < lastNow) { fail("manual_clock_invalid"); return; }
        lastNow = now;
        if (state_ == ManualSessionState::Moving && movementStarted &&
            (now < movementAt || now - movementAt >= static_cast<uint64_t>(limits.timeoutMs) * 1000)) {
            fail("manual_move_timeout"); return;
        }
        if (state_ == ManualSessionState::Closing &&
            (now < closeAt || now - closeAt >= timeout())) {
            fail("manual_close_timeout"); return;
        }
        if (phase == Phase::PowerInit) {
            Pose observed;
            if (!calibration.read(observed) || observed.yaw != bounds.expectedCalibration.yaw ||
                observed.pitch != bounds.expectedCalibration.pitch) { fail("manual_calibration_failed"); return; }
            if (!power.beginManualExternalPower()) { fail("manual_power_init_failed"); return; }
            trace("manual_power", "initialization_accepted");
            if (!uart.open()) { fail("manual_uart_open_failed"); return; }
            trace("manual_uart", "uart1_open_1000000_tx6_rx7");
            phase = Phase::PowerPoll;
            return;
        }
        if (phase == Phase::PowerPoll) {
            possibleEffect = true;
            const auto result = power.pollManualExternalPower(static_cast<uint32_t>(now / 1000), false);
            if (result == Power::ManualPowerState::Failed || result == Power::ManualPowerState::Cancelled) {
                fail("manual_power_failed"); return;
            }
            if (result == Power::ManualPowerState::Ready) {
                trace("manual_power", "rail_settled_500ms_readback_accepted");
                phase = Phase::StartupRx;
            }
            return;
        }
        if (phase == Phase::StartupRx) {
            const auto result = uart.synchronizeStartupRx();
            if (result != ManualIoState::Pending) {
                uart.describeStartupRx(startupError, sizeof(startupError));
                trace("manual_uart", startupError);
                if (result == ManualIoState::Failed) fail(startupError);
                else if (!startPing(1, Phase::PingYaw)) fail("manual_bus_failed");
            }
            return;
        }
        const auto busState = bus.state();
        if (busState == ManualScsState::Fault || busState == ManualScsState::Cancelled) {
            fail("manual_bus_failed"); return;
        }
        if (busState != ManualScsState::Idle && busState != ManualScsState::Complete) {
            const bool pingTx = busState == ManualScsState::Send &&
                (phase == Phase::PingYaw || phase == Phase::PingPitch);
            bus.tick();
            if (pingTx) trace("manual_ping_tx",
                bus.state() == ManualScsState::Drain ? "packet_accepted" : "failed_or_cancelled");
            return;
        }
        switch (phase) {
            case Phase::PingYaw:
                trace("manual_ping_response", "servo1_verified");
                if (!startPing(2, Phase::PingPitch)) fail("manual_bus_failed");
                break;
            case Phase::PingPitch:
                trace("manual_ping_response", "servo2_verified");
                powerReady = true;
                if (feedbackOnly) {
                    feedbackAt = now;
                    if (!startSample(AfterSample::Feedback)) fail("manual_bus_failed");
                } else { state_ = ManualSessionState::Powered; phase = Phase::None; }
                break;
            case Phase::Minimum:
                minimumMode = bus.value();
                if (!readWord(selected(), 11, Phase::Maximum)) fail("manual_bus_failed");
                break;
            case Phase::Maximum:
                if (minimumMode < 0 || minimumMode > bounds.minimum ||
                    bus.value() < bounds.maximum || bus.value() > 1000 || minimumMode >= bus.value() ||
                    !startSample(AfterSample::Initial)) fail("manual_mode_invalid");
                break;
            case Phase::Sample:
                values[sampleIndex++] = bus.value();
                if (sampleIndex < 6) {
                    if (!nextSampleRead()) fail("manual_bus_failed");
                } else finishSample(now);
                break;
            case Phase::Hold:
                if (!readWord(selected(), 42, Phase::HoldGoal)) fail("manual_bus_failed");
                break;
            case Phase::HoldGoal:
                if (bus.value() != origin || !startSample(AfterSample::AfterHold)) fail("manual_hold_unconfirmed");
                break;
            case Phase::Enable:
                if (!startSample(AfterSample::AfterEnable)) fail("manual_bus_failed");
                break;
            case Phase::Move:
                if (!readWord(selected(), 42, Phase::MoveGoal)) fail("manual_bus_failed");
                break;
            case Phase::MoveGoal:
                if (bus.value() != target || !startSample(AfterSample::Moving)) fail("manual_move_unconfirmed");
                break;
            case Phase::CloseDisable:
                closeStable = false;
                if (!startSample(AfterSample::CloseAfter)) fail("manual_close_unconfirmed");
                break;
            case Phase::None:
            case Phase::PowerInit:
            case Phase::PowerPoll:
            case Phase::StartupRx: break;
        }
    }
private:
    enum class Phase { None, PowerInit, PowerPoll, StartupRx, PingYaw, PingPitch, Minimum, Maximum,
        Sample, Hold, HoldGoal, Enable, Move, MoveGoal, CloseDisable };
    enum class AfterSample { Feedback, Initial, AfterHold, AfterEnable, Move, Moving, CloseBefore, CloseAfter };
    Scs& bus;
    Uart& uart;
    Power& power;
    Calibration& calibration;
    MotionLimits limits;
    ManualStop<Scs, Uart> stop;
    SingleAxisTestBounds bounds{};
    ManualSessionState state_ = ManualSessionState::Idle;
    Phase phase = Phase::None;
    AfterSample afterSample = AfterSample::Initial;
    const char* error_ = nullptr;
    char busError[64]{};
    char startupError[160]{};
    char terminalError[224]{};
    void (*diagnostic)(const char*, const char*) = nullptr;
    void trace(const char* event, const char* detail) {
        if (diagnostic) diagnostic(event, detail);
    }
    int values[6]{}, sampleIndex = 0, minimumMode = 0, origin = 0, otherOrigin = 0;
    int target = 0, requestedDelta = 0;
    uint32_t travelMs = 0;
    uint64_t lastNow = 0, sampleAt = 0, movementAt = 0, stableAt = 0;
    uint64_t closeAt = 0, closeStableAt = 0;
    int closeSelected = 0, closeOther = 0, closeTorque = 0;
    bool originKnown = false, possibleEffect = false, returning = false;
    bool movementStarted = false, stable = false, movedOut = false;
    bool powerReady = false, closeStable = false, closeInterrupted = false;
    bool stopFinished = false, stopHadFault = false;
    bool feedbackOnly = false, feedbackStable = false, feedbackRailOff = false;
    uint64_t feedbackAt = 0, feedbackStableAt = 0;
    Pose feedbackPose{};
    ManualFeedback captured{};

    void finishStop(bool torqueAcknowledged) {
        const bool railRequested = power.requestRailOff();
        stopFinished = true;
        if (!torqueAcknowledged || !railRequested || stopHadFault || closeInterrupted) {
            if (!error_) error_ = "manual_stop_unconfirmed";
            state_ = ManualSessionState::Fault;
            bus.abandon();
        } else {
            error_ = "manual_stop_physical_unconfirmed";
            state_ = ManualSessionState::Stopped;
        }
    }

    uint8_t selected() const { return bounds.axis == SingleAxis::Yaw ? 1 : 2; }
    int selectedPosition() const { return values[selected() == 1 ? 0 : 3]; }
    int selectedMoving() const { return values[selected() == 1 ? 1 : 4]; }
    int selectedTorque() const { return values[selected() == 1 ? 2 : 5]; }
    int otherPosition() const { return values[selected() == 1 ? 3 : 0]; }
    int otherMoving() const { return values[selected() == 1 ? 4 : 1]; }
    int otherTorque() const { return values[selected() == 1 ? 5 : 2]; }
    uint64_t timeout() const { return static_cast<uint64_t>(limits.timeoutMs) * 1000; }
    bool startPing(uint8_t id, Phase next) {
        if (!bus.ping(id, timeout())) return false;
        trace("manual_ping_queued", id == 1 ? "servo1" : "servo2");
        phase = next; return true;
    }
    bool readWord(uint8_t id, uint8_t reg, Phase next) {
        if (!bus.readWord(id, reg, timeout())) return false;
        phase = next; return true;
    }
    bool nextSampleRead() {
        const uint8_t id = sampleIndex < 3 ? 1 : 2;
        const int part = sampleIndex % 3;
        return part == 0 ? bus.readWord(id, 56, timeout()) :
            bus.readByte(id, part == 1 ? 66 : 40, timeout());
    }
    bool startSample(AfterSample next) {
        sampleAt = uart.nowUs();
        sampleIndex = 0;
        afterSample = next;
        phase = Phase::Sample;
        return nextSampleRead();
    }
    void finishSample(uint64_t now) {
        if (now < sampleAt || now - sampleAt > static_cast<uint64_t>(bounds.maximumFeedbackAgeMs) * 1000 ||
            selectedPosition() < bounds.minimum || selectedPosition() > bounds.maximum ||
            otherPosition() < 0 || otherPosition() > 1000 || otherMoving() != 0 || otherTorque() != 0 ||
            (selectedMoving() != 0 && selectedMoving() != 1) ||
            (selectedTorque() != 0 && selectedTorque() != 1) ||
            (originKnown && otherPosition() != otherOrigin)) {
            fail("manual_feedback_unconfirmed"); return;
        }
        if (afterSample != AfterSample::Moving && selectedMoving() != 0) {
            fail("manual_unexpected_motion"); return;
        }
        switch (afterSample) {
            case AfterSample::Feedback:
                if (selectedTorque() != 0 || now - feedbackAt >= timeout()) {
                    fail("manual_feedback_unconfirmed"); return;
                }
                if (!feedbackStable) {
                    feedbackPose = {values[0], values[3]};
                    feedbackStableAt = now;
                    feedbackStable = true;
                } else if (values[0] != feedbackPose.yaw || values[3] != feedbackPose.pitch) {
                    fail("manual_pose_changed"); return;
                } else if (now - feedbackStableAt >= static_cast<uint64_t>(limits.settleMs) * 1000) {
                    captured.stationary = true;
                    captured.position = feedbackPose;
                    state_ = ManualSessionState::Powered;
                    phase = Phase::None;
                    return;
                }
                if (!startSample(AfterSample::Feedback)) fail("manual_bus_failed");
                break;
            case AfterSample::Initial:
                if (selectedTorque() != 0) { fail("manual_torque_unconfirmed"); return; }
                origin = selectedPosition(); otherOrigin = otherPosition(); originKnown = true;
                possibleEffect = true;
                if (!bus.goal(selected(), origin, static_cast<uint16_t>(limits.stopMs), timeout()))
                    fail("manual_hold_unconfirmed");
                else phase = Phase::Hold;
                break;
            case AfterSample::AfterHold:
                if (selectedPosition() != origin) { fail("manual_pose_changed"); return; }
                if (selectedTorque() == 1) {
                    if (!startSample(AfterSample::AfterEnable)) fail("manual_bus_failed");
                } else if (!bus.enable(selected(), timeout())) fail("manual_enable_unconfirmed");
                else phase = Phase::Enable;
                break;
            case AfterSample::AfterEnable:
                if (selectedPosition() != origin || selectedTorque() != 1) {
                    fail("manual_enable_unconfirmed"); return;
                }
                state_ = ManualSessionState::Ready; phase = Phase::None; break;
            case AfterSample::Move: {
                if (selectedTorque() != 1) { fail("manual_torque_unconfirmed"); return; }
                const int delta = returning ? origin - selectedPosition() : requestedDelta;
                if (!delta || delta < -bounds.maximumStep || delta > bounds.maximumStep ||
                    selectedPosition() + delta < bounds.minimum || selectedPosition() + delta > bounds.maximum) {
                    fail("manual_step_invalid"); return;
                }
                target = selectedPosition() + delta;
                movementAt = now; movementStarted = true; stable = false; possibleEffect = true;
                if (!bus.goal(selected(), target, static_cast<uint16_t>(travelMs), timeout()))
                    fail("manual_move_unconfirmed");
                else phase = Phase::Move;
                break;
            }
            case AfterSample::Moving:
                if (selectedTorque() != 1) { fail("manual_torque_unconfirmed"); return; }
                if (selectedMoving() || selectedPosition() < target - limits.tolerance ||
                    selectedPosition() > target + limits.tolerance ||
                    now - movementAt < static_cast<uint64_t>(travelMs) * 1000) stable = false;
                else if (!stable) { stable = true; stableAt = now; }
                else if (now - stableAt >= static_cast<uint64_t>(limits.settleMs) * 1000) {
                    movementStarted = false; movedOut = !returning;
                    state_ = ManualSessionState::Ready; phase = Phase::None; return;
                }
                if (!startSample(AfterSample::Moving)) fail("manual_bus_failed");
                break;
            case AfterSample::CloseBefore:
            case AfterSample::CloseAfter: {
                const bool after = afterSample == AfterSample::CloseAfter;
                if (after && selectedTorque() != 0) { fail("manual_close_unconfirmed"); return; }
                if (!closeStable) {
                    if (!after) {
                        closeSelected = selectedPosition();
                        closeOther = otherPosition();
                        closeTorque = selectedTorque();
                    } else if (selectedPosition() != closeSelected || otherPosition() != closeOther) {
                        fail("manual_close_motion"); return;
                    }
                    closeStable = true;
                    closeStableAt = now;
                } else if (selectedPosition() != closeSelected || otherPosition() != closeOther ||
                    (!after && selectedTorque() != closeTorque)) {
                    fail("manual_close_motion"); return;
                } else if (now - closeStableAt >= static_cast<uint64_t>(limits.settleMs) * 1000) {
                    if (after || closeTorque == 0) {
                        state_ = ManualSessionState::Closed; phase = Phase::None; return;
                    }
                    if (!bus.disable(selected(), timeout())) fail("manual_close_unconfirmed");
                    else phase = Phase::CloseDisable;
                    return;
                }
                if (!startSample(afterSample)) fail("manual_close_unconfirmed");
                break;
            }
        }
    }
    void fail(const char* reason) {
        const bool cuttingRail = feedbackOnly && possibleEffect && !feedbackRailOff;
        bool cutAccepted = false;
        if (cuttingRail) {
            feedbackRailOff = true;
            power.cancelManualPower();
            cutAccepted = power.requestRailOff();
        }
        // Preserve the transaction before abandon/Stop can replace its context.
        if (!strcmp(reason, "manual_bus_failed")) {
            bus.describeFailure(busError, sizeof(busError));
            reason = busError;
        }
        if (cuttingRail) {
            snprintf(terminalError, sizeof(terminalError), "%s_cut_%s", reason,
                cutAccepted ? "accepted" : "unconfirmed");
            reason = terminalError;
        }
        error_ = reason;
        trace("manual_fault", reason);
        state_ = ManualSessionState::Fault;
        bus.abandon();
    }
};

} // namespace lab

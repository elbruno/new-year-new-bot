#pragma once

#include <string.h>
#include <initializer_list>
#include "LabMotion.h"
#include "LabSingleAxisTest.h"

namespace lab {

// Raw SCSCL registers from the pinned StackChan BSP. No angle/cache fallback.
namespace scs {
constexpr uint8_t yaw = 1, pitch = 2;
constexpr uint8_t minimum = 9, maximum = 11, torque = 40, goal = 42;
constexpr uint8_t position = 56, moving = 66;
}

template <typename Bus, typename Calibration>
class ScsMotionDriver : public MotionDriver {
public:
    ScsMotionDriver(Bus& bus, Calibration& storage, MotionLimits limits = {}, bool yawBench = false,
                    bool twoAxisTest = false, bool checkedDefault = false)
        : bus(bus), storage(storage), limits(limits), compiledLimits(limits),
          yawBench(yawBench), checkedActivation(twoAxisTest || checkedDefault) {}

    bool calibration(Pose& home) override {
        Pose value;
        CalibrationObservation* observation = nullptr;
        if (checkedActivation && !traceFrozen) {
            calibrationObservation = CalibrationObservation{};
            calibrationObservation.phase = "arm";
            calibrationObservation.atMs = clock;
            calibrationObservation.commandId = correlation;
            observation = &calibrationObservation;
        }
        calibrated = limits.valid() && storage.read(value, observation) &&
            (checkedActivation ? value.yaw >= 0 && value.yaw <= 1000 && value.pitch >= 0 && value.pitch <= 1000 :
                limits.contains(value));
        if (calibrated) home = value;
        else fail("calibration", "calibration_unconfirmed");
        return calibrated;
    }

    void inspectCalibration(uint32_t now) override {
        if (!checkedActivation || traceFrozen || strcmp(calibrationObservation.phase, "not_read")) return;
        calibrationObservation.phase = "startup";
        calibrationObservation.atMs = now;
        Pose unused;
        storage.read(unused, &calibrationObservation);
    }

    bool begin() override {
        if (manualActive) return false;
        if (!calibrated || !limits.valid()) return fail("begin", "calibration_unconfirmed");
        if (!started) started = bus.begin();
        if (!started) return fail("begin", "board_start_failed");
        return started;
    }

    MotionDiagnostic diagnostic() const override { return failure; }
    bool busStarted() const override { return started || manualActive; }
    bool cancelManualForStop() override {
        if (!manualActive) return false;
        manualMovePending = false;
        manualFail("single_axis_stop_unconfirmed");
        return true;
    }
    void emergencyStop() override {
        manualMovePending = false;
        bus.emergencyStop();
        armed = false;
    }
    bool samplePending() const override { return !strcmp(sample.state, "pending"); }
    bool observationPending() const override { return !strcmp(hardwareObservation.state, "pending"); }
    bool acceptsLimits(const MotionLimits& value) const override {
        const auto config = motionConfiguration(value, {}, {value.yawMinimum, value.pitchMinimum});
        if (!checkedActivation || configurationError(config, compiledLimits)) return false;
        MotionLimits expected = configurationLimits(config, compiledLimits);
        return value.reviewed == expected.reviewed && value.tolerance == expected.tolerance &&
            value.startupMs == expected.startupMs && value.stopMs == expected.stopMs &&
            value.settleMs == expected.settleMs && value.pollMs == expected.pollMs &&
            value.freshMs == expected.freshMs && value.timeoutMs == expected.timeoutMs &&
            (!started || (value.yawMinimum == limits.yawMinimum && value.yawMaximum == limits.yawMaximum &&
                value.pitchMinimum == limits.pitchMinimum && value.pitchMaximum == limits.pitchMaximum));
    }
    void applyLimits(const MotionLimits& value) override { limits = value; }
    bool releaseAfterEnableFault() override {
        if (!started || !enableObs.attempted) return true;
        if (enableObs.cleanup != EnableCleanup::NotAttempted) return enableObs.cleanup == EnableCleanup::TorqueOffConfirmed;
        bool ok = true;
        for (const uint8_t id : {scs::yaw, scs::pitch}) {
            auto& o = axisObs(id);
            o.cleanupAttempted = true;
            o.cleanupWrite = bus.EnableTorque(id, 0);
            o.cleanupError = bus.getLastError();
            o.cleanupStatus = o.cleanupError ? 0 : bus.getState();
            const bool writeOk = o.cleanupWrite == 1 && !o.cleanupError && !o.cleanupStatus;
            const int value = bus.readByte(id, scs::torque);
            const int error = bus.getLastError();
            const int status = error ? 0 : bus.getState();
            o.cleanupRead = value >= 0 && !error && !status ? value : -1;
            ok = ok && writeOk && o.cleanupRead == 0;
        }
        enableObs.cleanup = ok ? EnableCleanup::TorqueOffConfirmed : EnableCleanup::Failed;
        return ok;
    }
    bool enableCleanupRetained() const override { return enableObs.cleanup != EnableCleanup::NotAttempted; }
    void freezeTrace() override { traceFrozen = true; }
    void debugContext(uint32_t now, uint32_t commandId) override { clock = now; correlation = commandId; }
    void debugSnapshot(MotionDebugSnapshot& result) const override {
        result.busStarted = started;
        result.frozen = traceFrozen;
        result.overwritten = sequence > kMotionTraceCapacity ? sequence - kMotionTraceCapacity : 0;
        result.count = traceCount;
        for (uint8_t i = 0; i < traceCount; ++i)
            result.trace[i] = trace[(traceHead + kMotionTraceCapacity - traceCount + i) % kMotionTraceCapacity];
        result.sample = sample;
        result.calibration = calibrationObservation;
        result.enable = enableObs;
        result.hardware = hardwareObservation;
        result.torqueRelease = torqueReleaseObs;
        result.emergencyStop = bus.emergencyStopObservation();
    }
    const char* requestObservation(uint32_t bootId, uint32_t id, uint32_t now) override {
        if (manualActive) return "motion_busy";
        if (!checkedActivation) return "observation_unavailable";
        if (!bootId || !id) return "invalid_observation";
        if (hardwareObservation.bootId != bootId) {
            hardwareObservation = HardwareMotionObservation{};
            hardwareObservation.bootId = bootId;
        }
        if (id == hardwareObservation.observationId) return nullptr;
        if (id < hardwareObservation.observationId) return "stale_observation_id";
        if (observationPending()) return "observation_busy";
        if (hardwareObservation.observationId &&
            now - hardwareObservation.requestedAtMs < 1000) return "observation_rate_limited";
        hardwareObservation = HardwareMotionObservation{};
        hardwareObservation.bootId = bootId;
        hardwareObservation.observationId = id;
        hardwareObservation.requestedAtMs = now;
        hardwareObservation.state = "pending";
        return nullptr;
    }
    void tickObservation(uint32_t now) override {
        if (!observationPending()) return;
        if (now - hardwareObservation.requestedAtMs >= 1000) {
            finishObservation("failed", "observation_timeout", now);
            return;
        }
        if (!hardwareObservation.busInitialized) {
            calibrationObservation = CalibrationObservation{};
            calibrationObservation.phase = "observation";
            calibrationObservation.atMs = now;
            calibrationObservation.commandId = hardwareObservation.observationId;
            Pose unused;
            storage.read(unused, &calibrationObservation);
            if (!bus.beginReadOnly()) {
                finishObservation("failed", "servo_bus_unavailable", now);
                return;
            }
            hardwareObservation.busInitialized = true;
        }
        MotionValueObservation* value = nullptr;
        uint8_t servo = 0, address = 0;
        int minimum = 0, maximum = 0;
        bool wordRead = false;
        switch (hardwareObservation.count) {
            case 0: servo = scs::yaw; address = scs::position; minimum = 0; maximum = 1000;
                wordRead = true; value = &hardwareObservation.yaw.position; break;
            case 1: servo = scs::yaw; address = scs::moving; minimum = 0; maximum = 1;
                value = &hardwareObservation.yaw.moving; break;
            case 2: servo = scs::yaw; address = scs::torque; minimum = 0; maximum = 1;
                value = &hardwareObservation.yaw.torque; break;
            case 3: servo = scs::pitch; address = scs::position; minimum = 0; maximum = 1000;
                wordRead = true; value = &hardwareObservation.pitch.position; break;
            case 4: servo = scs::pitch; address = scs::moving; minimum = 0; maximum = 1;
                value = &hardwareObservation.pitch.moving; break;
            case 5: servo = scs::pitch; address = scs::torque; minimum = 0; maximum = 1;
                value = &hardwareObservation.pitch.torque; break;
            default: finishObservation(hardwareObservation.error ? "failed" : "complete",
                hardwareObservation.error, now); return;
        }
        const int result = wordRead ? bus.readWord(servo, address) : bus.readByte(servo, address);
        value->attempted = true;
        value->value = result;
        value->transportError = bus.getLastError();
        value->deviceStatus = value->transportError ? 0 : bus.getState();
        value->transactionOk = result >= 0 && !value->transportError && !value->deviceStatus;
        value->valid = value->transactionOk && result >= minimum && result <= maximum;
        if (!value->valid && !hardwareObservation.error)
            hardwareObservation.error = value->transportError ? "transport_error" :
                value->deviceStatus ? "device_status" : result < 0 ? "transaction_failed" :
                address == scs::position ? "position_out_of_range" :
                address == scs::moving ? "invalid_moving" : "invalid_torque";
        ++hardwareObservation.count;
        if (hardwareObservation.count == 6)
            finishObservation(hardwareObservation.error ? "failed" : "complete",
                hardwareObservation.error, now);
    }
    void cancelObservation(const char* reason, uint32_t now) override {
        if (observationPending()) finishObservation("cancelled", reason, now);
    }
    bool supportsTorqueRelease() const override { return checkedActivation && !yawBench; }
    bool torqueReleaseUncertain() const override { return torqueReleaseObs.writeUncertain; }
    bool releaseTorque(uint32_t now) override {
        if (torqueReleaseObs.attempted) return false;
        torqueReleaseObs = TorqueReleaseObservation{};
        torqueReleaseObs.attempted = true;
        torqueReleaseObs.atMs = now;
        torqueReleaseObs.commandId = correlation;
        torqueReleaseObs.state = "pending";
        if (!supportsTorqueRelease() || !started || !armed)
            return releaseFailed("torque_release_unavailable");
        Pose measured;
        bool moving = false;
        if (!read(measured, moving)) return releaseFailed("position_unconfirmed");
        if (moving) return releaseFailed("unexpected_motion");
        int initialTorque[2] = {-1, -1};
        for (int i = 0; i < 2; ++i) {
            const uint8_t id = i ? scs::pitch : scs::yaw;
            auto& detail = i ? torqueReleaseObs.pitch : torqueReleaseObs.yaw;
            if (!byte(id, scs::torque, initialTorque[i], "torque_release_before"))
                return releaseFailed("torque_state_unconfirmed");
            detail.before = initialTorque[i];
            if (initialTorque[i] != 0 && initialTorque[i] != 1)
                return releaseFailed("invalid_torque_state");
        }
        for (int i = 0; i < 2; ++i) {
            if (!initialTorque[i]) continue;
            const uint8_t id = i ? scs::pitch : scs::yaw;
            auto& detail = i ? torqueReleaseObs.pitch : torqueReleaseObs.yaw;
            Pose current;
            bool currentlyMoving = false;
            int currentTorque = -1;
            if (!read(current, currentlyMoving) || currentlyMoving || !same(current, measured))
                return releaseFailed("pose_changed");
            if (!byte(id, scs::torque, currentTorque, "torque_release_before") || currentTorque != 1)
                return releaseFailed("torque_state_changed");
            detail.writeAttempted = true;
            torqueReleaseObs.writeUncertain = true;
            detail.write = bus.EnableTorque(id, 0);
            detail.writeError = bus.getLastError();
            detail.writeStatus = detail.writeError ? 0 : bus.getState();
            const int readback = bus.readByte(id, scs::torque);
            detail.readError = bus.getLastError();
            detail.readStatus = detail.readError ? 0 : bus.getState();
            detail.readback = readback >= 0 && !detail.readError && !detail.readStatus ? readback : -1;
            if (detail.write != 1 || detail.writeError || detail.writeStatus || detail.readback != 0)
                return releaseFailed(detail.writeError ? "transport_error" :
                    detail.writeStatus ? "device_status" : "torque_release_unconfirmed");
        }
        Pose finalPose;
        bool finalMoving = false;
        if (!read(finalPose, finalMoving) || finalMoving || !same(finalPose, measured))
            return releaseFailed("torque_release_unconfirmed");
        for (const uint8_t id : {scs::yaw, scs::pitch}) {
            int torque = -1;
            if (!byte(id, scs::torque, torque, "torque_release_readback"))
                return releaseFailed("torque_release_unconfirmed");
            if (torque != 0) {
                fail("torque_release_readback", "torque_mismatch", id, scs::torque, 0, 0, torque, 0);
                return releaseFailed("torque_release_unconfirmed");
            }
            (id == scs::yaw ? torqueReleaseObs.yaw : torqueReleaseObs.pitch).finalTorque = torque;
        }
        torqueReleaseObs.writeUncertain = false;
        torqueReleaseObs.state = "confirmed";
        armed = false;
        return true;
    }
    const char* requestSample(uint32_t id, uint8_t servo, uint32_t now) override {
        if (!id || (servo != scs::yaw && servo != scs::pitch)) return "invalid_sample";
        if (!started) return "bus_not_started";
        if (id == sample.sampleId) return servo == sample.servoId ? nullptr : "sample_id_conflict";
        if (id < sample.sampleId) return "stale_sample_id";
        if (!strcmp(sample.state, "pending")) return "sample_busy";
        if (sample.sampleId && now - sample.requestedAtMs < 1000) return "sample_rate_limited";
        sample = ServoSample();
        sample.sampleId = id;
        sample.servoId = servo;
        sample.requestedAtMs = now;
        sample.state = "pending";
        return nullptr;
    }
    void cancelSample(const char* reason, uint32_t now) override {
        if (!strcmp(sample.state, "pending")) {
            sample.state = "cancelled";
            sample.error = reason;
            sample.finishedAtMs = now;
        }
    }
    void tickSample(uint32_t now) override {
        if (strcmp(sample.state, "pending")) return;
        const uint32_t elapsed = now - sample.requestedAtMs;
        // HTTP may enqueue after loop() sampled its clock. Wait for the next current tick.
        if (elapsed > INT32_MAX) return;
        if (elapsed >= 1000) {
            sample.state = "failed";
            sample.error = "sample_timeout";
            sample.finishedAtMs = now;
            return;
        }
        const auto reg = kSampleRegisters[sample.count];
        const int value = reg.width == 1 ? bus.readByte(sample.servoId, reg.address) :
            bus.readWord(sample.servoId, reg.address);
        const int error = bus.getLastError();
        const int status = error ? 0 : bus.getState();
        const bool ok = value >= 0 && error == 0 && status == 0;
        sample.values[sample.count++] = {0, 0, now, "debug_sample", "read",
            sample.servoId, reg.address, reg.width, value, error, status, ok};
        if (!ok || sample.count == kSampleRegisterCount) {
            sample.state = ok ? "complete" : "failed";
            sample.error = ok ? nullptr : error ? "transport_error" : status ? "device_status" : "transaction_failed";
            sample.finishedAtMs = now;
        }
    }

    bool read(Pose& pose, bool& moving) override {
        if (!started) return fail("feedback", "bus_not_started");
        Pose value;
        int yawMoving, pitchMoving;
        if (!word(scs::yaw, scs::position, value.yaw) ||
            !byte(scs::yaw, scs::moving, yawMoving) ||
            !word(scs::pitch, scs::position, value.pitch) ||
            !byte(scs::pitch, scs::moving, pitchMoving)) return false;
        if (yawMoving > 1) return fail("feedback", "invalid_moving", scs::yaw, scs::moving);
        if (pitchMoving > 1) return fail("feedback", "invalid_moving", scs::pitch, scs::moving);
        if (value.yaw < limits.yawMinimum || value.yaw > limits.yawMaximum)
            return fail("feedback", "position_out_of_range", scs::yaw, scs::position);
        if (value.pitch < limits.pitchMinimum || value.pitch > limits.pitchMaximum)
            return fail("feedback", "position_out_of_range", scs::pitch, scs::position);
        if (yawBench) {
            int pitchTorque;
            if (!byte(scs::pitch, scs::torque, pitchTorque)) return false;
            if (pitchTorque != 0)
                return fail("feedback", "pitch_torque_enabled", scs::pitch, scs::torque, 0, 0, pitchTorque, 0);
            if (originKnown && value.pitch != origin.pitch)
                return fail("feedback", "pitch_displaced", scs::pitch, scs::position);
        }
        pose = value;
        moving = yawMoving || pitchMoving;
        return true;
    }

    bool enableAt(const Pose& measured) override {
        if (!started || !limits.contains(measured)) return fail("enable", "invalid_measured_pose");
        Pose current;
        bool moving;
        if (!read(current, moving)) return false;
        if (moving || !same(current, measured)) return fail("enable", "pose_changed");
        if (yawBench && originKnown && (current.pitch != origin.pitch ||
            current.yaw < origin.yaw - kYawBenchStep || current.yaw > origin.yaw + kYawBenchStep))
            return fail("enable", "bench_envelope");
        if (armed) return torqueIs(1, "torque_after_lock"); // Logical Lock does not release torque.
        if (enableAttempted) return fail("enable", "enable_already_attempted");
        enableAttempted = true;
        if (checkedActivation && !yawBench) return enableTwoAxis(measured);
        // Bench: the servo's stored range only needs to cover the +/-16 envelope, not the full 0..1000 scale.
        const int yawLow = yawBench ? measured.yaw - kYawBenchStep : limits.yawMinimum;
        const int yawHigh = yawBench ? measured.yaw + kYawBenchStep : limits.yawMaximum;
        if (!positionMode(scs::yaw, yawLow, yawHigh) ||
            (!yawBench && !positionMode(scs::pitch, limits.pitchMinimum, limits.pitchMaximum)) ||
            !torqueIs(0, "torque_before_goal")) return false;
        // Both goals must be acknowledged/read back before either torque write.
        if (!goal(scs::yaw, measured.yaw, limits.stopMs) ||
            (!yawBench && !goal(scs::pitch, measured.pitch, limits.stopMs)) ||
            !read(current, moving)) return false;
        if (moving || !same(current, measured)) return fail("enable", "pose_changed");
        if (!torqueIs(0, "torque_after_goal") ||
            !healthy(bus.EnableTorque(scs::yaw, 1), 1, "torque_write", scs::yaw, scs::torque, "write", 1) ||
            (!yawBench && !healthy(bus.EnableTorque(scs::pitch, 1), 1, "torque_write", scs::pitch, scs::torque, "write", 1)) ||
            !torqueIs(1, "torque_after_enable")) return false;
        origin = measured;
        originKnown = true;
        armed = true;
        return true;
    }

    bool move(const Pose& target, uint32_t durationMs) override {
        if (!armed || !limits.contains(target) ||
            (durationMs != limits.travelMs && durationMs != limits.stopMs)) return fail("move", "invalid_target");
        if (yawBench && (!originKnown || target.pitch != origin.pitch ||
            target.yaw < origin.yaw - kYawBenchStep || target.yaw > origin.yaw + kYawBenchStep))
            return fail("move", "bench_envelope");
        if (!torqueIs(1, "torque_before_move")) return false;
        // Sequential acknowledged writes are not atomic. Never retry an uncertain write.
        return goal(scs::yaw, target.yaw, durationMs) &&
            (yawBench || goal(scs::pitch, target.pitch, durationMs));
    }

    // Not a runtime entry point: these synchronous calls must be phased before
    // use so the existing Stop path can run between bus transactions.
    bool startManual(const SingleAxisTestBounds& bounds, uint32_t travelMs) {
        if (manualAttempted || started || armed || observationPending() || samplePending() ||
            singleAxisBoundsError(bounds) || !bus.manualPowerReady() ||
            bounds.expectedCalibration.yaw < 0 || bounds.expectedCalibration.yaw > 1000 ||
            bounds.expectedCalibration.pitch < 0 || bounds.expectedCalibration.pitch > 1000)
            return false;
        const int low = bounds.axis == SingleAxis::Yaw ? compiledLimits.yawMinimum : compiledLimits.pitchMinimum;
        const int high = bounds.axis == SingleAxis::Yaw ? compiledLimits.yawMaximum : compiledLimits.pitchMaximum;
        if (bounds.minimum < low || bounds.maximum > high || bounds.maximumStep > compiledLimits.step ||
            bounds.maximumStep <= compiledLimits.tolerance || travelMs < compiledLimits.travelMs ||
            travelMs > UINT16_MAX ||
            static_cast<uint64_t>(travelMs) + compiledLimits.settleMs >= compiledLimits.timeoutMs) return false;
        manualAttempted = true;
        Pose calibrationValue;
        if (!storage.read(calibrationValue) || !same(calibrationValue, bounds.expectedCalibration))
            return manualFail("single_axis_calibration_unconfirmed");
        manualBounds = bounds;
        manualTravelMs = travelMs;
        manualActive = true;
        return true;
    }

    bool enableManual(uint32_t now) {
        if (!manualActive || manualFailed || manualEnableAttempted) return false;
        manualEnableAttempted = true;
        const uint8_t id = manualId();
        if (!limits.stopMs || limits.stopMs > UINT16_MAX ||
            !positionMode(id, manualBounds.minimum, manualBounds.maximum))
            return manualFail("single_axis_position_mode");
        Pose measured;
        int selectedTorque;
        if (!manualSample(measured, selectedTorque, true) || selectedTorque != 0)
            return manualFail("single_axis_feedback_unconfirmed");
        manualOtherOrigin = manualOther(measured);
        manualOriginKnown = true;
        if (!goal(id, manualValue(measured), limits.stopMs))
            return manualFail("single_axis_goal_unconfirmed");
        Pose after;
        if (!manualSample(after, selectedTorque, true) || !same(after, measured) ||
            (selectedTorque != 0 && selectedTorque != 1))
            return manualFail("single_axis_goal_postcondition");
        if (selectedTorque == 0 &&
            !healthy(bus.EnableTorque(id, 1), 1, "torque_write", id, scs::torque, "write", 1))
            return manualFail("single_axis_enable_unconfirmed");
        if (!manualSample(after, selectedTorque, true) || !same(after, measured) || selectedTorque != 1)
            return manualFail("single_axis_enable_postcondition");
        manualMeasured = measured;
        manualObservedAt = now;
        manualEnabled = true;
        return true;
    }

    bool moveManual(uint32_t now, int delta) {
        if (!manualActive || manualFailed || !manualEnabled || manualMovePending) return false;
        Pose measured;
        int selectedTorque;
        if (!manualSample(measured, selectedTorque, true) || selectedTorque != 1)
            return manualFail("single_axis_feedback_unconfirmed");
        SingleAxisTestFeedback feedback{true, true, true, manualBounds.expectedCalibration, measured, now};
        Pose target;
        if (planSingleAxisTest(manualBounds, feedback, now, delta, target)) return false;
        if (!goal(manualId(), manualValue(target), manualTravelMs))
            return manualFail("single_axis_move_unconfirmed");
        Pose after;
        if (!manualSample(after, selectedTorque, false) || selectedTorque != 1)
            return manualFail("single_axis_move_postcondition");
        manualMeasured = after;
        manualObservedAt = now;
        manualTarget = target;
        manualMoveAt = now;
        manualMovePending = true;
        manualSettling = false;
        return true;
    }

    bool pollManualMove(uint32_t now) {
        if (!manualMovePending || manualFailed) return false;
        const uint32_t elapsed = now - manualMoveAt;
        if (elapsed > INT32_MAX || elapsed >= limits.timeoutMs)
            return manualFail("single_axis_move_timeout");
        Pose measured;
        int selectedTorque;
        if (!manualSample(measured, selectedTorque, false) || selectedTorque != 1)
            return manualFail("single_axis_feedback_unconfirmed");
        manualMeasured = measured;
        manualObservedAt = now;
        const int difference = manualValue(measured) - manualValue(manualTarget);
        if (manualMoving || difference < -limits.tolerance || difference > limits.tolerance ||
            elapsed < manualTravelMs) {
            manualSettling = false;
            return false;
        }
        if (!manualSettling) {
            manualSettling = true;
            manualSettlingAt = now;
            return false;
        }
        if (now - manualSettlingAt < limits.settleMs) return false;
        manualMovePending = false;
        return true;
    }

    bool manualFault() const { return manualFailed; }
    bool manualMoveInProgress() const { return manualMovePending; }
    Pose manualPosition() const { return manualMeasured; }
    uint32_t manualPositionAt() const { return manualObservedAt; }

private:
    Bus& bus;
    Calibration& storage;
    MotionLimits limits;
    const MotionLimits compiledLimits;
    const bool yawBench;
    const bool checkedActivation;
    Pose origin{};
    bool originKnown = false;
    bool calibrated = false, started = false, enableAttempted = false, armed = false;
    SingleAxisTestBounds manualBounds{};
    Pose manualMeasured{}, manualTarget{};
    int manualOtherOrigin = 0;
    uint32_t manualObservedAt = 0, manualMoveAt = 0, manualSettlingAt = 0, manualTravelMs = 0;
    bool manualAttempted = false, manualActive = false, manualFailed = false;
    bool manualEnableAttempted = false, manualEnabled = false, manualOriginKnown = false;
    bool manualMovePending = false, manualSettling = false, manualMoving = false;
    MotionDiagnostic failure{};
    BusObservation trace[kMotionTraceCapacity]{};
    ServoSample sample{};
    CalibrationObservation calibrationObservation{};
    EnableObservation enableObs{};
    HardwareMotionObservation hardwareObservation{};
    TorqueReleaseObservation torqueReleaseObs{};
    EnableAxisObservation& axisObs(uint8_t id) { return id == scs::yaw ? enableObs.yaw : enableObs.pitch; }

    uint8_t manualId() const { return manualBounds.axis == SingleAxis::Yaw ? scs::yaw : scs::pitch; }
    int manualValue(const Pose& value) const {
        return manualBounds.axis == SingleAxis::Yaw ? value.yaw : value.pitch;
    }
    int manualOther(const Pose& value) const {
        return manualBounds.axis == SingleAxis::Yaw ? value.pitch : value.yaw;
    }
    bool manualFail(const char* cause) {
        manualFailed = true;
        return fail("single_axis", cause);
    }
    bool manualSample(Pose& value, int& selectedTorque, bool selectedStationary) {
        int yawMoving, pitchMoving, yawTorque, pitchTorque;
        if (!word(scs::yaw, scs::position, value.yaw) || !byte(scs::yaw, scs::moving, yawMoving) ||
            !byte(scs::yaw, scs::torque, yawTorque) ||
            !word(scs::pitch, scs::position, value.pitch) || !byte(scs::pitch, scs::moving, pitchMoving) ||
            !byte(scs::pitch, scs::torque, pitchTorque)) return false;
        const int selectedMoving = manualBounds.axis == SingleAxis::Yaw ? yawMoving : pitchMoving;
        const int otherMoving = manualBounds.axis == SingleAxis::Yaw ? pitchMoving : yawMoving;
        const int otherTorque = manualBounds.axis == SingleAxis::Yaw ? pitchTorque : yawTorque;
        selectedTorque = manualBounds.axis == SingleAxis::Yaw ? yawTorque : pitchTorque;
        manualMoving = selectedMoving == 1;
        return value.yaw >= 0 && value.yaw <= 1000 && value.pitch >= 0 && value.pitch <= 1000 &&
            manualValue(value) >= manualBounds.minimum && manualValue(value) <= manualBounds.maximum &&
            (selectedMoving == 0 || (!selectedStationary && selectedMoving == 1)) &&
            otherMoving == 0 && otherTorque == 0 &&
            (!manualOriginKnown || manualOther(value) == manualOtherOrigin);
    }
    void finishObservation(const char* state, const char* error, uint32_t now) {
        hardwareObservation.state = state;
        hardwareObservation.error = error;
        hardwareObservation.finishedAtMs = now;
    }
    bool releaseFailed(const char* cause) {
        torqueReleaseObs.state = "failed";
        torqueReleaseObs.error = cause;
        return fail("torque_release", cause);
    }
    // Reads every axis without short-circuiting so a failure still leaves evidence for both. Sampled reads
    // cannot prove the absence of transient motion between them. Expected torque -1 accepts 0 or 1.
    bool observeAxes(const Pose& measured, bool yawGoal, bool pitchGoal, int yawTorque, int pitchTorque,
                     const char* stage) {
        const bool goaled[2] = {yawGoal, pitchGoal};
        const int expectedTorque[2] = {yawTorque, pitchTorque};
        int goalValue[2] = {-1, -1}, positionValue[2] = {-1, -1}, movingValue[2] = {-1, -1},
            torqueValue[2] = {-1, -1};
        bool ok = true;
        for (int i = 0; i < 2; ++i) {
            const uint8_t id = i ? scs::pitch : scs::yaw;
            auto& o = axisObs(id);
            const bool goalOk = !goaled[i] || word(id, scs::goal, goalValue[i]);
            const bool positionOk = word(id, scs::position, positionValue[i]);
            const bool movingOk = byte(id, scs::moving, movingValue[i]);
            const bool torqueOk = byte(id, scs::torque, torqueValue[i], stage);
            if (goaled[i]) o.goalRead = goalOk ? goalValue[i] : -1;
            o.position = positionOk ? positionValue[i] : -1;
            o.moving = movingOk ? movingValue[i] : -1;
            o.torque = torqueOk ? torqueValue[i] : -1;
            if (goaled[i] && torqueOk && torqueValue[i] == 1 && o.enableWrite < 0) o.torqueOnAfterGoal = true;
            ok = ok && goalOk && positionOk && movingOk && torqueOk;
        }
        if (!ok) return false;
        for (int i = 0; i < 2; ++i) {
            const uint8_t id = i ? scs::pitch : scs::yaw;
            const int target = i ? measured.pitch : measured.yaw;
            if (movingValue[i] != 0) return fail("enable", "pose_changed", id, scs::moving, 0, 0, movingValue[i], 0);
            if (positionValue[i] != target) return fail("enable", "pose_changed", id, scs::position, 0, 0, positionValue[i], target);
            if (goaled[i] && goalValue[i] != target) return fail("goal_readback", "goal_mismatch", id, scs::goal, 0, 0, goalValue[i], target);
            if (torqueValue[i] != 0 && torqueValue[i] != 1)
                return fail(stage, "invalid_torque", id, scs::torque, 0, 0, torqueValue[i], 0);
            if (expectedTorque[i] >= 0 && torqueValue[i] != expectedTorque[i])
                return fail(stage, "torque_mismatch", id, scs::torque, 0, 0, torqueValue[i], expectedTorque[i]);
        }
        return true;
    }
    bool goalStep(uint8_t id, const Pose& measured, bool yawGoal, bool pitchGoal, int yawTorque, int pitchTorque) {
        auto& o = axisObs(id);
        enableObs.attempted = o.goalAttempted = true;
        const int target = id == scs::yaw ? measured.yaw : measured.pitch;
        o.goalWrite = bus.WritePos(id, target, static_cast<uint16_t>(limits.stopMs), 0);
        o.goalError = bus.getLastError();
        o.goalStatus = o.goalError ? 0 : bus.getState();
        const bool wrote = healthy(o.goalWrite, 1, "goal_write", id, scs::goal, "write", 6);
        const bool seen = observeAxes(measured, yawGoal, pitchGoal, yawTorque, pitchTorque, "torque_after_goal");
        return wrote && seen;
    }
    // Torque 1 after a goal write is accepted only with clean postconditions and is reported factually;
    // the vendor cause (auto-enable or otherwise) is unverified.
    bool enableTwoAxis(const Pose& measured) {
        if (!positionMode(scs::yaw, limits.yawMinimum, limits.yawMaximum) ||
            !positionMode(scs::pitch, limits.pitchMinimum, limits.pitchMaximum) ||
            !observeAxes(measured, false, false, 0, 0, "torque_before_goal") ||
            !goalStep(scs::yaw, measured, true, false, -1, 0) ||
            !goalStep(scs::pitch, measured, true, true, -1, -1) ||
            !observeAxes(measured, true, true, -1, -1, "torque_before_enable")) return false;
        for (const uint8_t id : {scs::yaw, scs::pitch}) {
            auto& o = axisObs(id);
            if (o.torque == 1) continue;
            o.enableWrite = bus.EnableTorque(id, 1);
            if (!healthy(o.enableWrite, 1, "torque_write", id, scs::torque, "write", 1)) return false;
            if (!observeAxes(measured, true, true, 1, id == scs::pitch ? 1 : -1, "torque_after_enable")) return false;
        }
        if (!observeAxes(measured, true, true, 1, 1, "torque_after_enable")) return false;
        origin = measured;
        originKnown = true;
        armed = true;
        return true;
    }
    uint32_t sequence = 0, correlation = 0, clock = 0;
    uint8_t traceHead = 0, traceCount = 0;
    bool traceFrozen = false;

    bool fail(const char* stage, const char* cause, uint8_t id = 0, uint8_t reg = 0,
              int transportError = 0, int deviceStatus = 0, int observed = 0, int expected = 0) {
        if (!failure.stage) failure = {stage, cause, id, reg, transportError, deviceStatus, observed, expected};
        traceFrozen = true;
        return false;
    }
    bool healthy(int result, int expected, const char* stage, uint8_t id, uint8_t address,
                 const char* operation = "read", uint8_t width = 1) {
        const int transportError = bus.getLastError();
        // The pinned bus can retain a previous status when no valid reply arrives.
        const int deviceStatus = transportError == 0 ? bus.getState() : 0;
        const bool ok = result == expected && transportError == 0 && deviceStatus == 0;
        if (!traceFrozen) {
            trace[traceHead] = {++sequence, correlation, clock, stage, operation, id, address, width,
                result, transportError, deviceStatus, ok};
            traceHead = (traceHead + 1) % kMotionTraceCapacity;
            if (traceCount < kMotionTraceCapacity) ++traceCount;
        }
        if (ok) return true;
        return fail(stage, transportError ? "transport_error" : deviceStatus ? "device_status" : "transaction_failed",
                    id, address, transportError, deviceStatus);
    }
    bool word(uint8_t id, uint8_t address, int& value) {
        const int result = bus.readWord(id, address);
        const char* stage = address == scs::goal ? "goal_readback" :
            address == scs::minimum || address == scs::maximum ? "position_mode" : "feedback";
        if (!healthy(result, result < 0 ? 0 : result, stage, id, address, "read", 2)) return false;
        value = result;
        return true;
    }
    bool byte(uint8_t id, uint8_t address, int& value, const char* stage = "feedback") {
        const int result = bus.readByte(id, address);
        if (!healthy(result, result < 0 ? 0 : result, stage, id, address)) return false;
        value = result;
        return true;
    }
    bool torqueIs(int expected, const char* stage) {
        int yaw, pitch;
        if (!byte(scs::yaw, scs::torque, yaw, stage)) return false;
        if (yaw != expected)
            return fail(stage, "torque_mismatch", scs::yaw, scs::torque, 0, 0, yaw, expected);
        const int pitchExpected = yawBench ? 0 : expected;
        if (!byte(scs::pitch, scs::torque, pitch, stage)) return false;
        return pitch == pitchExpected ||
            fail(stage, "torque_mismatch", scs::pitch, scs::torque, 0, 0, pitch, pitchExpected);
    }
    bool positionMode(uint8_t id, int minimum, int maximum) {
        int low, high;
        if (!word(id, scs::minimum, low) || !word(id, scs::maximum, high)) return false;
        if (low > minimum) return fail("position_mode", "incompatible_limits", id, scs::minimum, 0, 0, low, minimum);
        return (low < high && high >= maximum) ||
            fail("position_mode", "incompatible_limits", id, scs::maximum, 0, 0, high, maximum);
    }
    bool goal(uint8_t id, int position, uint32_t durationMs) {
        if (!healthy(bus.WritePos(id, position, static_cast<uint16_t>(durationMs), 0), 1,
                     "goal_write", id, scs::goal, "write", 6)) return false;
        int confirmed;
        if (!word(id, scs::goal, confirmed)) return false;
        return confirmed == position || fail("goal_readback", "goal_mismatch", id, scs::goal);
    }
    static bool same(const Pose& a, const Pose& b) {
        return a.yaw == b.yaw && a.pitch == b.pitch;
    }
};

} // namespace lab

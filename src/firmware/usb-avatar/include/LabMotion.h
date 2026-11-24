// Goal: Coordinate checked servo motion and stopping.
// Responsibilities: Define motion limits, configuration, state, and control.
// How it works:
// 1. Configuration and feedback are checked against reviewed limits.
// 2. Admitted operations advance through the motion controller.
// 3. Stop and fault states are tracked and exposed to callers.
#pragma once

#include <stdint.h>
#include <string.h>
#include "LabMotionDebug.h"

namespace lab {

struct Pose {
    int yaw = 0;
    int pitch = 0;
};

enum class SingleAxis { Yaw, Pitch };

struct SingleAxisTestBounds {
    bool reviewed = false;
    SingleAxis axis = SingleAxis::Yaw;
    int minimum = 0, maximum = 0, maximumStep = 0;
    Pose expectedCalibration{};
    uint32_t maximumFeedbackAgeMs = 0;
};

inline const char* singleAxisBoundsError(const SingleAxisTestBounds& bounds) {
    if (!bounds.reviewed || bounds.minimum < 0 || bounds.maximum > 1000 ||
        bounds.minimum >= bounds.maximum || bounds.maximumStep <= 0 ||
        bounds.maximumStep > bounds.maximum - bounds.minimum ||
        !bounds.maximumFeedbackAgeMs ||
        (bounds.axis != SingleAxis::Yaw && bounds.axis != SingleAxis::Pitch))
        return "single_axis_bounds_unreviewed";
    return nullptr;
}

enum class MotionOperation { Left, Right, Up, Down, Home, Nod };
enum class MotionState { Blocked, Disarmed, Arming, Ready, Moving, Stopping, Fault };
enum class StopResult { Pending, Confirmed, Failed };

inline constexpr int kYawBenchStep = 16;

struct MotionLimits {
    bool reviewed = false;
    int yawMinimum = 0, yawMaximum = 0, pitchMinimum = 0, pitchMaximum = 0;
    int step = 0, nodStep = 0, tolerance = 0;
    uint32_t startupMs = 0, travelMs = 0, stopMs = 0, settleMs = 0;
    uint32_t pollMs = 0, freshMs = 0, timeoutMs = 0;

    constexpr bool valid() const {
        return reviewed && yawMinimum >= 0 && yawMinimum < yawMaximum && yawMaximum <= 1000 &&
            pitchMinimum >= 0 && pitchMinimum < pitchMaximum && pitchMaximum <= 1000 &&
            step > 0 && step <= yawMaximum - yawMinimum && step <= pitchMaximum - pitchMinimum &&
            nodStep > 0 && nodStep <= pitchMaximum - pitchMinimum && tolerance >= 0 &&
            tolerance < step && tolerance < nodStep && startupMs > 0 && startupMs <= 5000 &&
            travelMs > 0 && travelMs <= 5000 && stopMs > 0 && stopMs <= 5000 &&
            settleMs > 0 && settleMs <= 5000 && pollMs > 0 && freshMs >= pollMs &&
            freshMs <= timeoutMs && pollMs <= settleMs && timeoutMs <= 5000 &&
            timeoutMs > 2ULL * (static_cast<uint64_t>(travelMs) + settleMs) &&
            static_cast<uint64_t>(stopMs) + settleMs < timeoutMs;
    }
    constexpr bool contains(const Pose& pose) const {
        return pose.yaw >= yawMinimum && pose.yaw <= yawMaximum &&
            pose.pitch >= pitchMinimum && pose.pitch <= pitchMaximum;
    }
};

inline constexpr MotionLimits yawBenchLimits() {
    return {true, 0, 1000, 0, 1000, kYawBenchStep, kYawBenchStep, 2,
        200, 1000, 100, 200, 20, 100, 4000};
}

struct MotionConfiguration {
    int expectedYawRaw = 0, expectedPitchRaw = 0;
    int homeYawRaw = 0, homePitchRaw = 0;
    int yawMinRaw = 0, yawMaxRaw = 0, pitchMinRaw = 0, pitchMaxRaw = 0;
    int stepRaw = 0, nodStepRaw = 0;
    uint32_t travelMs = 0;
};

inline MotionConfiguration motionConfiguration(const MotionLimits& limits, Pose expected, Pose home) {
    return {expected.yaw, expected.pitch, home.yaw, home.pitch,
        limits.yawMinimum, limits.yawMaximum, limits.pitchMinimum, limits.pitchMaximum,
        limits.step, limits.nodStep, limits.travelMs};
}

inline constexpr MotionLimits configurationLimits(
        const MotionConfiguration& value, MotionLimits baseline) {
    baseline.yawMinimum = value.yawMinRaw; baseline.yawMaximum = value.yawMaxRaw;
    baseline.pitchMinimum = value.pitchMinRaw; baseline.pitchMaximum = value.pitchMaxRaw;
    baseline.step = value.stepRaw; baseline.nodStep = value.nodStepRaw;
    baseline.travelMs = value.travelMs;
    return baseline;
}

inline constexpr const char* configurationError(
        const MotionConfiguration& value, const MotionLimits& baseline) {
    if (value.expectedYawRaw < 0 || value.expectedYawRaw > 1000) return "config_expected_yaw_invalid";
    if (value.expectedPitchRaw < 0 || value.expectedPitchRaw > 1000) return "config_expected_pitch_invalid";
    if (value.yawMinRaw < baseline.yawMinimum || value.yawMaxRaw > baseline.yawMaximum ||
        value.yawMinRaw >= value.yawMaxRaw) return "config_yaw_bounds_invalid";
    if (value.pitchMinRaw < baseline.pitchMinimum || value.pitchMaxRaw > baseline.pitchMaximum ||
        value.pitchMinRaw >= value.pitchMaxRaw) return "config_pitch_bounds_invalid";
    const auto limits = configurationLimits(value, baseline);
    if (!limits.contains({value.homeYawRaw, value.homePitchRaw})) return "config_home_out_of_limits";
    if (value.stepRaw <= baseline.tolerance || value.stepRaw > baseline.step ||
        value.stepRaw > value.yawMaxRaw - value.yawMinRaw ||
        value.stepRaw > value.pitchMaxRaw - value.pitchMinRaw) return "config_step_invalid";
    if (value.nodStepRaw <= baseline.tolerance || value.nodStepRaw > baseline.nodStep ||
        value.nodStepRaw > value.pitchMaxRaw - value.pitchMinRaw) return "config_nod_step_invalid";
    if (value.travelMs < baseline.travelMs || !limits.valid()) return "config_timing_invalid";
    return nullptr;
}

inline bool sameReferenceEnvelope(const MotionConfiguration& first, const MotionConfiguration& second) {
    return first.expectedYawRaw == second.expectedYawRaw && first.expectedPitchRaw == second.expectedPitchRaw &&
        first.yawMinRaw == second.yawMinRaw && first.yawMaxRaw == second.yawMaxRaw &&
        first.pitchMinRaw == second.pitchMinRaw && first.pitchMaxRaw == second.pitchMaxRaw;
}

struct MotionConfigurationSnapshot {
    MotionConfiguration active{}, compiled{};
    MotionLimits bounds{};
    uint32_t revision = 0;
    bool supported = false, busStarted = false, operatorSelected = false;
    const char* applyReason = nullptr;
    bool configured = true;
};

struct MotionDiagnostic {
    const char* stage = nullptr;
    const char* cause = nullptr;
    uint8_t servoId = 0, reg = 0;
    int transportError = 0, deviceStatus = 0;
    // Raw values for limit/range mismatches; zero when not applicable.
    int observed = 0, expected = 0;
};

class MotionDriver {
public:
    virtual ~MotionDriver() = default;
    // Read-only verified calibration/home; never substitute or persist default values.
    virtual bool calibration(Pose& home) = 0;
    virtual bool begin() = 0;
    virtual bool read(Pose& pose, bool& moving) = 0;
    virtual bool enableAt(const Pose& measured) = 0;
    virtual bool move(const Pose& target, uint32_t durationMs) = 0;
    virtual MotionDiagnostic diagnostic() const { return {}; }
    virtual void debugSnapshot(MotionDebugSnapshot& result) const { result.clear(); }
    virtual void debugContext(uint32_t, uint32_t) {}
    virtual void inspectCalibration(uint32_t) {}
    virtual bool busStarted() const { return false; }
    virtual bool samplePending() const { return false; }
    virtual bool acceptsLimits(const MotionLimits&) const { return false; }
    // Called only after pure acceptance, under the device mutex. Never touches hardware.
    virtual void applyLimits(const MotionLimits&) {}
    virtual const char* requestSample(uint32_t, uint8_t, uint32_t) { return "debug_unavailable"; }
    virtual void tickSample(uint32_t) {}
    virtual void cancelSample(const char*, uint32_t) {}
    virtual const char* requestObservation(uint32_t, uint32_t, uint32_t) { return "observation_unavailable"; }
    virtual void tickObservation(uint32_t) {}
    virtual void cancelObservation(const char*, uint32_t) {}
    virtual bool observationPending() const { return false; }
    virtual void freezeTrace() {}
    // After a failed Enable on a started bus: one torque-off attempt per axis with readback. No
    // goal, calibration, mode or NVS write. The terminal result is retained and never replayed.
    virtual bool releaseAfterEnableFault() { return true; }
    virtual bool enableCleanupRetained() const { return false; }
    virtual bool supportsTorqueRelease() const { return false; }
    virtual bool releaseTorque(uint32_t) { return false; }
    virtual bool torqueReleaseUncertain() const { return false; }
    virtual bool cancelManualForStop() { return false; }
    virtual void emergencyStop() {}
};

struct MotionSnapshot {
    MotionState state;
    bool hasPosition;
    Pose position;
    const char* error;
    bool localYawBench = false;
    MotionDiagnostic diagnostic{};
    bool remoteYawBench = false;
    const char* primaryError = nullptr;
    const char* stopResult = "not_requested";
    bool twoAxisTest = false;
    bool checkedDefault = false;
    bool armAvailable = false, torqueReleaseAvailable = false;
};

inline const char* motionStateName(MotionState state) {
    switch (state) {
        case MotionState::Disarmed: return "disarmed";
        case MotionState::Arming: return "arming";
        case MotionState::Ready: return "ready";
        case MotionState::Moving: return "moving";
        case MotionState::Stopping: return "stopping";
        case MotionState::Fault: return "fault";
        default: return "blocked";
    }
}

class MotionController {
public:
    explicit MotionController(MotionDriver* driver = nullptr, MotionLimits limits = {}, bool yawBench = false,
                              bool remoteYawBench = false, bool twoAxisTest = false,
                              Pose expectedCalibration = {}, Pose testHome = {},
                              bool checkedDefault = false, bool configured = true)
        : driver(driver), limits(limits), yawBenchMode(yawBench),
          remoteYawBenchMode(yawBench && remoteYawBench), twoAxisTestMode(twoAxisTest && !yawBench),
          normalCheckedMode(checkedDefault && !yawBench && !twoAxisTest),
          configurationReady(configured), compiledConfigured(configured),
          expectedCalibration(expectedCalibration), testHome(testHome),
          compiledLimits(limits), compiledConfiguration(motionConfiguration(limits, expectedCalibration, testHome)) {
        if (normalCheckedMode && !configurationReady) error = "motion_config_required";
        else if (limits.reviewed) { state = MotionState::Disarmed; error = nullptr; }
        else if (twoAxisTestMode) error = "motion_test_config_required";
    }

    MotionController(MotionDriver* driver, MotionLimits absoluteCeiling,
                     const MotionConfiguration* unitConfiguration, const char* unitError = nullptr)
        : MotionController(driver, absoluteCeiling, false, false, false, {}, {}, true, false) {
        if (!unitConfiguration && !unitError) return;
        if (unitError || !unitConfiguration || configurationError(*unitConfiguration, absoluteCeiling)) {
            error = compiledError = unitError ? unitError : "invalid_motion_configuration";
            return;
        }
        limits = compiledLimits = configurationLimits(*unitConfiguration, absoluteCeiling);
        compiledConfiguration = *unitConfiguration;
        expectedCalibration = {unitConfiguration->expectedYawRaw, unitConfiguration->expectedPitchRaw};
        testHome = {unitConfiguration->homeYawRaw, unitConfiguration->homePitchRaw};
        configurationReady = compiledConfigured = true;
        state = MotionState::Disarmed;
        error = nullptr;
    }

    bool startup(uint32_t now) {
        if (!normalCheckedMode || startupAttempted) return false;
        startupAttempted = true;
        return configurationReady && arm(now);
    }

    bool arm(uint32_t now) {
        if (emergencyStopped) {
            error = "motion_stop_unconfirmed";
            return false;
        }
        if (torqueReleaseAttempted) {
            error = torqueReleaseUncertain ? "torque_release_uncertain" : "torque_released_requires_restart";
            return false;
        }
        if (driver && driver->observationPending()) return false;
        if (normalCheckedMode && !configurationReady) return false;
        if (!limits.reviewed) return false;
        if (state != MotionState::Disarmed) return false;
        if (!limits.valid()) return fail("invalid_motion_limits");
        if (!driver) return fail("motion_adapter_unavailable");
        if (normalCheckedMode &&
            configurationError(motionConfiguration(limits, expectedCalibration, testHome), compiledLimits))
            return fail("invalid_motion_configuration");
        if (!driver->calibration(home) || (!yawBenchMode && !checkedConfiguration() && !limits.contains(home)))
            return fail("calibration_unconfirmed");
        if (checkedConfiguration()) {
            if (home.yaw != expectedCalibration.yaw || home.pitch != expectedCalibration.pitch)
                return fail("calibration_mismatch");
            if (!limits.contains(testHome)) return fail("home_out_of_limits");
            home = testHome;
        }
        if (!driverStarted && !driver->begin()) return fail("motion_start_failed");
        driverStarted = true;
        state = MotionState::Arming;
        error = nullptr;
        known = false;
        stopConfirmed = false;
        began = now;
        return true;
    }

    bool available() const { return state == MotionState::Ready || state == MotionState::Moving; }
    const char* unavailableReason() const { return error ? error : "motion_not_ready"; }
    uint32_t minimumDuration() const { return limits.timeoutMs; }
    MotionSnapshot snapshot() const {
        return {state, known, measured, error, benchOnly(), failure, remoteYawBenchMode, primaryError, lastStop,
            twoAxisTestMode, normalCheckedMode, armAvailable(), releaseTorqueAvailable()};
    }
    bool benchOnly() const { return yawBenchMode && !remoteYawBenchMode; }
    bool remoteYawBench() const { return remoteYawBenchMode; }
    bool remoteArmAllowed() const { return remoteYawBenchMode || checkedConfiguration(); }
    bool armAvailable() const {
        return remoteArmAllowed() && !torqueReleaseAttempted && state == MotionState::Disarmed &&
            !(driver && driver->observationPending());
    }
    bool releaseTorqueAvailable() const {
        return checkedConfiguration() && state == MotionState::Disarmed && energized && driverStarted &&
            !torqueReleaseAttempted && driver && driver->supportsTorqueRelease();
    }
    bool observationPending() const { return driver && driver->observationPending(); }
    bool twoAxisTest() const { return twoAxisTestMode; }
    bool checkedConfiguration() const { return twoAxisTestMode || normalCheckedMode; }
    uint32_t configurationRevision() const { return configRevision; }
    const char* canPrepareSingleAxis(const SingleAxisTestBounds& value, uint32_t travelMs) const {
        if (!normalCheckedMode || configurationReady || state != MotionState::Blocked ||
            driverStarted || (driver && driver->busStarted()) || observationPending() ||
            (driver && driver->samplePending())) return "single_axis_not_inactive";
        if (configRevision == UINT32_MAX) return "config_revision_exhausted";
        if (singleAxisBoundsError(value)) return "single_axis_bounds_unreviewed";
        const int low = value.axis == SingleAxis::Yaw ? compiledLimits.yawMinimum : compiledLimits.pitchMinimum;
        const int high = value.axis == SingleAxis::Yaw ? compiledLimits.yawMaximum : compiledLimits.pitchMaximum;
        if (value.minimum < low || value.maximum > high || value.maximumStep > compiledLimits.step ||
            value.maximumStep <= compiledLimits.tolerance ||
            value.expectedCalibration.yaw < 0 || value.expectedCalibration.yaw > 1000 ||
            value.expectedCalibration.pitch < 0 || value.expectedCalibration.pitch > 1000)
            return "single_axis_bounds_invalid";
        if (travelMs < compiledLimits.travelMs || travelMs > UINT16_MAX ||
            static_cast<uint64_t>(travelMs) + compiledLimits.settleMs >= compiledLimits.timeoutMs)
            return "single_axis_duration_invalid";
        return nullptr;
    }
    bool prepareSingleAxis(const SingleAxisTestBounds& value, uint32_t travelMs) {
        if (canPrepareSingleAxis(value, travelMs)) return false;
        manualPreparation = value;
        manualTravelMs = travelMs;
        manualPrepared = true;
        ++configRevision;
        return true;
    }
    bool singleAxisPrepared() const { return manualPrepared; }
    void invalidateSingleAxisPreparation() {
        manualPrepared = false;
        manualPreparation = {};
        manualTravelMs = 0;
    }
    const char* configurationAdmission() const {
        if (!checkedConfiguration()) return "config_unavailable";
        if (normalCheckedMode && !compiledConfigured) return unavailableReason();
        if (state != MotionState::Disarmed &&
            !(normalCheckedMode && state == MotionState::Blocked && !configurationReady))
            return "motion_not_disarmed";
        if (driver && driver->samplePending()) return "sample_busy";
        if (observationPending()) return "observation_busy";
        if (configRevision == UINT32_MAX) return "config_revision_exhausted";
        return nullptr;
    }
    void configurationSnapshot(MotionConfigurationSnapshot& result) const {
        result = {motionConfiguration(limits, expectedCalibration, testHome), compiledConfiguration,
            compiledLimits, configRevision, checkedConfiguration() && compiledLimits.valid(),
            driverStarted || (driver && driver->busStarted()), operatorSelected, configurationAdmission(),
            configurationReady};
        if (!result.applyReason) result.applyReason = canApplyConfiguration(result.active);
    }
    const char* validateConfiguration(const MotionConfiguration& value) const {
        if (!checkedConfiguration()) return "config_unavailable";
        if (normalCheckedMode && !compiledConfigured) return unavailableReason();
        if (normalCheckedMode && (value.expectedYawRaw != compiledConfiguration.expectedYawRaw ||
            value.expectedPitchRaw != compiledConfiguration.expectedPitchRaw))
            return "config_reference_changed";
        return configurationError(value, compiledLimits);
    }
    const char* canApplyConfiguration(const MotionConfiguration& value) const {
        if (const char* error = validateConfiguration(value)) return error;
        if (const char* error = configurationAdmission()) return error;
        if ((driverStarted || (driver && driver->busStarted())) &&
            !sameReferenceEnvelope(value, motionConfiguration(limits, expectedCalibration, testHome)))
            return "config_reboot_required";
        if (!driver || !driver->acceptsLimits(configurationLimits(value, compiledLimits)))
            return "config_driver_rejected";
        return nullptr;
    }
    bool applyConfiguration(const MotionConfiguration& value, bool reset) {
        if (canApplyConfiguration(value)) return false;
        const auto next = configurationLimits(value, compiledLimits);
        driver->applyLimits(next);
        limits = next;
        expectedCalibration = {value.expectedYawRaw, value.expectedPitchRaw};
        testHome = {value.homeYawRaw, value.homePitchRaw};
        home = testHome;
        operatorSelected = !reset;
        if (normalCheckedMode && !configurationReady) {
            configurationReady = true;
            state = MotionState::Disarmed;
            error = nullptr;
        }
        ++configRevision;
        return true;
    }
    void resetConfigurationForBoot() {
        invalidateSingleAxisPreparation();
        // A live driver cannot be reinterpreted by a bookkeeping reset.
        if (!checkedConfiguration() || driverStarted || (driver && driver->busStarted())) return;
        if (driver && driver->acceptsLimits(compiledLimits)) driver->applyLimits(compiledLimits);
        limits = compiledLimits;
        expectedCalibration = {compiledConfiguration.expectedYawRaw, compiledConfiguration.expectedPitchRaw};
        testHome = {compiledConfiguration.homeYawRaw, compiledConfiguration.homePitchRaw};
        configRevision = 0;
        operatorSelected = false;
        if (normalCheckedMode) {
            configurationReady = compiledConfigured;
            if (state != MotionState::Fault) {
                state = configurationReady ? MotionState::Disarmed : MotionState::Blocked;
                error = configurationReady ? nullptr : compiledError;
            }
        }
    }
    void debugSnapshot(MotionDebugSnapshot& result) const {
        if (driver) driver->debugSnapshot(result);
        else result.clear();
        if (checkedConfiguration()) {
            result.calibration.referenceConfigured = configurationReady;
            result.calibration.expectedYaw = expectedCalibration.yaw;
            result.calibration.expectedPitch = expectedCalibration.pitch;
            result.calibration.homeYaw = testHome.yaw;
            result.calibration.homePitch = testHome.pitch;
        }
    }
    void inspectCalibration(uint32_t now) {
        if (checkedConfiguration() && driver) driver->inspectCalibration(now);
    }
    void debugContext(uint32_t now, uint32_t commandId) { if (driver) driver->debugContext(now, commandId); }
    const char* requestObservation(uint32_t bootId, uint32_t id, uint32_t now) {
        if (!checkedConfiguration()) return "observation_unavailable";
        if (state != MotionState::Blocked && state != MotionState::Disarmed) return "motion_busy";
        if (!driver) return "motion_adapter_unavailable";
        if (driver->samplePending()) return "sample_busy";
        return driver->requestObservation(bootId, id, now);
    }
    void cancelObservation(uint32_t now, const char* reason = "stopping") {
        if (driver) driver->cancelObservation(reason, now);
    }
    const char* releaseTorque(uint32_t now, uint32_t commandId = 0) {
        if (!releaseTorqueAvailable()) return "torque_release_unavailable";
        torqueReleaseAttempted = true;
        driver->debugContext(now, commandId);
        if (!driver->releaseTorque(now)) {
            torqueReleaseUncertain = driver->torqueReleaseUncertain();
            fail("torque_release_failed");
            return error;
        }
        torqueReleaseUncertain = false;
        energized = false;
        known = false;
        error = "torque_released";
        return nullptr;
    }
    const char* requestSample(uint32_t id, uint8_t servo, uint32_t now) {
        if (!remoteYawBenchMode && !twoAxisTestMode) return "debug_disabled";
        if (observationPending()) return "observation_busy";
        if (state != MotionState::Disarmed && state != MotionState::Fault) return "motion_busy";
        return driver ? driver->requestSample(id, servo, now) : "debug_unavailable";
    }
    void tickSample(uint32_t now) {
        if (!driver || (!remoteYawBenchMode && !twoAxisTestMode)) return;
        if (state == MotionState::Disarmed || state == MotionState::Fault) driver->tickSample(now);
        else driver->cancelSample("motion_busy", now);
    }
    void cancelSample(uint32_t now, const char* reason = "stopping") {
        if (driver) driver->cancelSample(reason, now);
    }

    const char* start(MotionOperation operation, uint32_t now) {
        if (yawBenchMode && operation != MotionOperation::Left && operation != MotionOperation::Right)
            return "bench_yaw_only";
        if (state != MotionState::Ready) return unavailableReason();
        bool moving = false;
        if (!read(now, moving)) return unavailableReason();
        if (moving) { fail("unexpected_motion"); return error; }
        origin = target = measured;
        switch (operation) {
            case MotionOperation::Left: target.yaw -= limits.step; break;
            case MotionOperation::Right: target.yaw += limits.step; break;
            case MotionOperation::Up: target.pitch += limits.step; break;
            case MotionOperation::Down: target.pitch -= limits.step; break;
            case MotionOperation::Home: target = home; break;
            case MotionOperation::Nod: target.pitch += limits.nodStep; break;
        }
        if (!limits.contains(target)) return "motion_limit";
        returnLeg = operation == MotionOperation::Nod;
        state = MotionState::Moving;
        began = legBegan = now;
        stable = false;
        if (!driver->move(target, limits.travelMs)) { fail("motion_write_failed"); return error; }
        return nullptr;
    }

    void tick(uint32_t now) {
        if (driver) driver->tickObservation(now);
        if (state == MotionState::Arming) {
            if (now - began < limits.startupMs) return;
            bool moving = false;
            if (!read(now, moving)) return;
            if (moving) { fail("unexpected_motion"); return; }
            if (yawBenchMode && !benchOriginKnown) {
                if (measured.yaw < kYawBenchStep || measured.yaw > 1000 - kYawBenchStep) {
                    fail("bench_origin_out_of_range");
                    return;
                }
                limits.yawMinimum = measured.yaw - kYawBenchStep;
                limits.yawMaximum = measured.yaw + kYawBenchStep;
                benchOriginKnown = true;
            }
            energized = true;
            if (!driver->enableAt(measured)) {
                fail("motion_arm_failed");
                driver->releaseAfterEnableFault();
                return;
            }
            state = MotionState::Ready;
            return;
        }
        if (!available() && state != MotionState::Stopping) return;
        if (state != MotionState::Ready && now - began >= limits.timeoutMs) {
            fail(state == MotionState::Stopping ? "motion_stop_unconfirmed" : "motion_timeout");
            return;
        }
        if (now - lastRead > limits.freshMs) known = false;
        if (now - lastRead < limits.pollMs) return;
        bool moving = false;
        if (!read(now, moving)) return;
        if (state == MotionState::Ready) {
            if (moving) fail("unexpected_motion");
            return;
        }
        if (moving || !near(measured, target)) { stable = false; return; }
        const uint32_t travel = state == MotionState::Stopping ? limits.stopMs : limits.travelMs;
        if (now - legBegan < travel) return;
        if (!stable) { stable = true; stableSince = now; return; }
        if (now - stableSince < limits.settleMs) return;
        if (state == MotionState::Stopping) {
            state = stopFault ? MotionState::Fault : stopLocked ? MotionState::Disarmed : MotionState::Ready;
            if (stopFault || stopLocked) known = false;
            if (!stopFault) error = checkedConfiguration() ?
                (error && !strcmp(error, "motion_locked") ? "motion_locked" : "motion_stopped") : nullptr;
            stopConfirmed = true;
        } else if (returnLeg) {
            target = origin;
            returnLeg = false;
            stable = false;
            legBegan = now;
            if (!driver->move(target, limits.travelMs)) fail("motion_write_failed");
        } else state = MotionState::Ready;
    }

    StopResult requestStop(uint32_t now) {
        invalidateSingleAxisPreparation();
        cancelObservation(now, "stopping");
        startupAttempted = true;
        if (emergencyStopped) return stopOutcome(StopResult::Failed);
        if (state == MotionState::Stopping) return stopOutcome(StopResult::Pending);
        stopConfirmed = false;
        if (driver && driver->cancelManualForStop()) return stopOutcome(StopResult::Failed);
        if (torqueReleaseUncertain) return stopOutcome(StopResult::Failed);
        if (!energized) {
            if (state == MotionState::Arming) state = MotionState::Disarmed;
            if (checkedConfiguration() && state == MotionState::Disarmed) error = "motion_stopped";
            known = false;
            return stopOutcome(StopResult::Confirmed);
        }

        // A partially enabled fault has no armed hold to return to: report the retained torque-off result.
        if (state == MotionState::Fault && driver->enableCleanupRetained())
            return stopOutcome(driver->releaseAfterEnableFault() ? StopResult::Confirmed : StopResult::Failed);
        stopFault = state == MotionState::Fault;
        stopLocked = checkedConfiguration() || state == MotionState::Disarmed || state == MotionState::Arming;
        bool moving = false;
        if (!read(now, moving)) return stopOutcome(StopResult::Failed);
        if (!stopFault && (state == MotionState::Ready || stopLocked) && !moving) {
            if (stopLocked) {
                state = MotionState::Disarmed;
                known = false;
                if (checkedConfiguration()) error = "motion_stopped";
            }
            return stopOutcome(StopResult::Confirmed);
        }
        target = measured;
        state = MotionState::Stopping;
        began = legBegan = now;
        stable = returnLeg = false;
        if (!driver->move(target, limits.stopMs)) {
            fail("motion_stop_unconfirmed");
            return stopOutcome(StopResult::Failed);
        }
        return stopOutcome(StopResult::Pending);
    }

    StopResult emergencyStop(uint32_t now) {
        invalidateSingleAxisPreparation();
        cancelObservation(now, "stopping");
        startupAttempted = true;
        known = false;
        if (!emergencyStopped) {
            emergencyStopped = true;
            if (driver) driver->emergencyStop();
        }
        state = MotionState::Fault;
        if (!error || !strcmp(error, "motion_stopped")) error = "motion_stop_unconfirmed";
        return stopOutcome(StopResult::Failed);
    }

    StopResult pollStop(uint32_t now) {
        tick(now);
        if (stopConfirmed) return stopOutcome(StopResult::Confirmed);
        return stopOutcome(state == MotionState::Stopping ? StopResult::Pending : StopResult::Failed);
    }

    bool disarm(uint32_t now) {
        startupAttempted = true;
        if (checkedConfiguration()) {
            if (state == MotionState::Fault || state == MotionState::Blocked) return false;
            const auto outcome = requestStop(now);
            if (outcome == StopResult::Failed) return false;
            error = "motion_locked";
            return true;
        }
        if (!energized) return state == MotionState::Blocked || state == MotionState::Disarmed;
        if (state != MotionState::Ready) return false;
        bool moving = false;
        if (!read(now, moving) || moving) return false;
        state = MotionState::Disarmed;
        known = false;
        return true;
    }

private:
    MotionDriver* driver;
    MotionLimits limits;
    bool yawBenchMode = false, remoteYawBenchMode = false, benchOriginKnown = false;
    bool twoAxisTestMode = false;
    bool normalCheckedMode = false, configurationReady = true, compiledConfigured = true, startupAttempted = false;
    const char* compiledError = "motion_config_required";
    Pose expectedCalibration{}, testHome{};
    MotionLimits compiledLimits;
    MotionConfiguration compiledConfiguration;
    uint32_t configRevision = 0;
    SingleAxisTestBounds manualPreparation{};
    uint32_t manualTravelMs = 0;
    bool manualPrepared = false;
    bool operatorSelected = false;
    MotionState state = MotionState::Blocked;
    const char* error = "motion_limits_unreviewed";
    const char* primaryError = nullptr;
    const char* lastStop = "not_requested";
    MotionDiagnostic failure{};
    Pose measured{}, home{}, origin{}, target{};
    bool known = false, driverStarted = false, energized = false, returnLeg = false, stable = false;
    bool torqueReleaseAttempted = false, torqueReleaseUncertain = false;
    bool stopConfirmed = false, stopFault = false, stopLocked = false, emergencyStopped = false;
    uint32_t began = 0, legBegan = 0, lastRead = 0, stableSince = 0;

    bool fail(const char* code) {
        invalidateSingleAxisPreparation();
        if (!primaryError) primaryError = code;
        if (!failure.stage) {
            failure = driver ? driver->diagnostic() : MotionDiagnostic{};
            if (!failure.stage) failure = {"controller", code};
        }
        if (driver) driver->freezeTrace();
        state = MotionState::Fault;
        known = false;
        error = code;
        return false;
    }
    StopResult stopOutcome(StopResult result) {
        lastStop = result == StopResult::Confirmed ? "confirmed" : result == StopResult::Pending ? "pending" : "failed";
        return result;
    }
    bool read(uint32_t now, bool& moving) {
        Pose value;
        if (!driver->read(value, moving))
            return fail(state == MotionState::Stopping ? "motion_stop_unconfirmed" : "position_unconfirmed");
        if (!limits.contains(value)) {
            if (!failure.stage) failure = {"controller", "position_out_of_limits"};
            return fail(state == MotionState::Stopping ? "motion_stop_unconfirmed" : "position_unconfirmed");
        }
        measured = value;
        known = true;
        lastRead = now;
        return true;
    }
    bool near(const Pose& first, const Pose& second) const {
        const int yaw = first.yaw - second.yaw, pitch = first.pitch - second.pitch;
        return yaw >= -limits.tolerance && yaw <= limits.tolerance &&
            pitch >= -limits.tolerance && pitch <= limits.tolerance;
    }
};

}  // namespace lab

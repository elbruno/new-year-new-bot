// Goal: Represent checked motion observations for diagnostics.
// Responsibilities: Define calibration, bus, servo, and Stop observations.
// How it works:
// 1. Motion code records structured observations and failure details.
// 2. Snapshots group observations for a particular operation.
// 3. Diagnostic consumers report the recorded state without inventing results.
#pragma once

#include <stdint.h>

namespace lab {

inline constexpr uint8_t kMotionTraceCapacity = 32;
struct ServoRegister { uint8_t address, width; };
// Fixed SCSCL telemetry only. No EEPROM writes, ID discovery or arbitrary addresses.
inline constexpr ServoRegister kSampleRegisters[] = {
    {9, 2}, {11, 2}, {40, 1}, {42, 2}, {44, 2}, {46, 2},
    {56, 2}, {58, 2}, {60, 2}, {62, 1}, {63, 1}, {66, 1}, {69, 2}
};
inline constexpr uint8_t kSampleRegisterCount = sizeof(kSampleRegisters) / sizeof(kSampleRegisters[0]);

enum class CalibrationReadResult { NotRead, Ok, NotFound, TypeMismatch, Error };

inline const char* calibrationReadName(CalibrationReadResult result) {
    switch (result) {
        case CalibrationReadResult::Ok: return "ok";
        case CalibrationReadResult::NotFound: return "not_found";
        case CalibrationReadResult::TypeMismatch: return "type_mismatch";
        case CalibrationReadResult::Error: return "error";
        default: return "not_read";
    }
}

struct CalibrationAxisObservation {
    CalibrationReadResult result = CalibrationReadResult::NotRead;
    int32_t code = 0, raw = 0;
    bool hasValue() const { return result == CalibrationReadResult::Ok; }
    bool valid() const { return hasValue() && raw >= 0 && raw <= 1000; }
};

struct CalibrationObservation {
    const char* phase = "not_read";
    uint32_t atMs = 0, commandId = 0;
    CalibrationReadResult openResult = CalibrationReadResult::NotRead;
    int32_t openCode = 0;
    CalibrationAxisObservation yaw{}, pitch{};
    bool referenceConfigured = false;
    int expectedYaw = 0, expectedPitch = 0, homeYaw = 0, homePitch = 0;
};

struct BusObservation {
    uint32_t sequence = 0, commandId = 0, atMs = 0;
    const char* stage = "debug_sample";
    const char* operation = "read";
    uint8_t servoId = 0, reg = 0, width = 0;
    int value = 0, transportError = 0, deviceStatus = 0;
    bool ok = false;
};

struct ServoSample {
    uint32_t sampleId = 0, requestedAtMs = 0, finishedAtMs = 0;
    uint8_t servoId = 0, count = 0;
    const char* state = "idle";
    const char* error = nullptr;
    BusObservation values[kSampleRegisterCount]{};
};

struct MotionValueObservation {
    bool attempted = false, transactionOk = false, valid = false;
    int value = -1, transportError = 0, deviceStatus = 0;
};

struct MotionAxisObservation {
    MotionValueObservation position{}, moving{}, torque{};
};

struct HardwareMotionObservation {
    uint32_t bootId = 0, observationId = 0, requestedAtMs = 0, finishedAtMs = 0;
    const char* state = "idle";
    const char* error = nullptr;
    bool busInitialized = false;
    uint8_t count = 0;
    MotionAxisObservation yaw{}, pitch{};
};

// Retained per-axis Enable evidence. -1 means unread/not attempted. It survives the frozen
// trace and never replaces the first fault; sampled reads cannot prove absence of motion between reads.
struct EnableAxisObservation {
    bool goalAttempted = false, torqueOnAfterGoal = false, cleanupAttempted = false;
    int goalWrite = -1, goalRead = -1, position = -1, moving = -1, torque = -1, enableWrite = -1;
    int goalError = 0, goalStatus = 0;
    int cleanupWrite = -1, cleanupRead = -1, cleanupError = 0, cleanupStatus = 0;
};

enum class EnableCleanup : uint8_t { NotAttempted, TorqueOffConfirmed, Failed };

inline const char* enableCleanupName(EnableCleanup value) {
    return value == EnableCleanup::TorqueOffConfirmed ? "torque_off_confirmed" :
        value == EnableCleanup::Failed ? "failed" : "not_attempted";
}

struct EnableObservation {
    bool attempted = false;
    EnableCleanup cleanup = EnableCleanup::NotAttempted;
    EnableAxisObservation yaw{}, pitch{};
};

struct TorqueReleaseAxisObservation {
    bool writeAttempted = false;
    int before = -1, write = -1, writeError = 0, writeStatus = 0;
    int readback = -1, readError = 0, readStatus = 0;
    int finalTorque = -1;
};

struct TorqueReleaseObservation {
    bool attempted = false, writeUncertain = false;
    uint32_t atMs = 0, commandId = 0;
    const char* state = "not_attempted";
    const char* error = nullptr;
    TorqueReleaseAxisObservation yaw{}, pitch{};
};

struct EmergencyStopObservation {
    bool requested = false, uartOwned = false, txAttempted = false, txDrained = false;
    bool txUncertain = false, railAttempted = false, railRequestAccepted = false;
};

struct MotionDebugSnapshot {
    bool busStarted = false, frozen = false;
    uint32_t overwritten = 0;
    uint8_t count = 0;
    BusObservation trace[kMotionTraceCapacity]{};
    ServoSample sample{};
    CalibrationObservation calibration{};
    EnableObservation enable{};
    HardwareMotionObservation hardware{};
    TorqueReleaseObservation torqueRelease{};
    EmergencyStopObservation emergencyStop{};

    void clear() {
        enable = EnableObservation{};
        hardware = HardwareMotionObservation{};
        torqueRelease = TorqueReleaseObservation{};
        emergencyStop = EmergencyStopObservation{};
        busStarted = frozen = false;
        overwritten = count = 0;
        for (auto& value : trace) value = BusObservation{};
        sample.sampleId = sample.requestedAtMs = sample.finishedAtMs = 0;
        sample.servoId = sample.count = 0;
        sample.state = "idle";
        sample.error = nullptr;
        for (auto& value : sample.values) value = BusObservation{};
        calibration = CalibrationObservation{};
    }
};

struct RuntimeLogEvent {
    uint32_t atMs = 0, bootId = 0, commandId = 0;
    const char* event = "boot";
    const char* action = "invalid";
    const char* state = "none";
    const char* error = nullptr;
};

} // namespace lab

#pragma once

#include <atomic>
#include <drivers/FTServo_Arduino/src/SCSCL.h>
#include <drivers/PY32IOExpander/PY32IOExpander.hpp>
#include <nvs.h>
#include "LabScsMotion.h"

namespace lab {

class StackChanCalibration {
public:
    bool read(Pose& home, CalibrationObservation* observation = nullptr) {
        if (observation) {
            observation->yaw = CalibrationAxisObservation{};
            observation->pitch = CalibrationAxisObservation{};
        }
        nvs_handle_t handle;
        const auto opened = nvs_open("servo", NVS_READONLY, &handle);
        if (observation) {
            observation->openResult = classify(opened);
            observation->openCode = opened;
        }
        if (opened != ESP_OK) return false;
        int32_t yaw = 0, pitch = 0;
        const auto yawResult = nvs_get_i32(handle, "zero_pos_1", &yaw);
        const auto pitchResult = nvs_get_i32(handle, "zero_pos_2", &pitch);
        nvs_close(handle);
        if (observation) {
            observe(observation->yaw, yawResult, yaw);
            observe(observation->pitch, pitchResult, pitch);
        }
        if (yawResult != ESP_OK || pitchResult != ESP_OK ||
            yaw < 0 || yaw > 1000 || pitch < 0 || pitch > 1000) return false;
        home = {static_cast<int>(yaw), static_cast<int>(pitch)};
        return true;
    }
private:
    static CalibrationReadResult classify(int result) {
        if (result == ESP_OK) return CalibrationReadResult::Ok;
        if (result == ESP_ERR_NVS_NOT_FOUND) return CalibrationReadResult::NotFound;
        if (result == ESP_ERR_NVS_TYPE_MISMATCH) return CalibrationReadResult::TypeMismatch;
        return CalibrationReadResult::Error;
    }
    static void observe(CalibrationAxisObservation& axis, int result, int32_t raw) {
        axis.result = classify(result);
        axis.code = result;
        if (result == ESP_OK) axis.raw = raw;
    }
};

class StackChanScsBus : public SCSCL {
public:
    enum class ManualPowerState {
        Idle, Expander, Direction, Pull, Rail, Readback, Settling, PingYaw, PingPitch,
        Ready, Failed, Cancelled
    };
    explicit StackChanScsBus(bool benchPower = false) : SCSCL(1, 1), benchPower(benchPower) {}
    bool reserveForOfficialBsp() {
        if (bspReserved || ownsUart || manualExternal || powerAttempted || railOffAttempted ||
            stopObservation.requested || uart_is_driver_installed(UART_NUM_1)) return false;
        bspReserved = true;
        return true;
    }
    bool begin() {
#if defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC)
        return false;
#else
        if (bspReserved || manualPower != ManualPowerState::Idle) return false;
        if (!beginReadOnly()) return false;
        if (ready) return true;
        if (powerAttempted) return false;
        powerAttempted = true;
        // Checked activation powers the shared rail, without BSP initialization or calibration writes.
        ready = !benchPower || (power.begin() && power.setDirection(0, true) &&
            power.setPullMode(0, m5::PY32IOExpander_Class::pull_up) &&
            power.digitalWrite(0, true) && power.getWriteValue(0));
        if (ready && benchPower) settle();
        return ready;
#endif
    }
    bool beginReadOnly() {
        if (bspReserved || manualExternal) return false;
        if (ownsUart) return true;
        if (uart_is_driver_installed(UART_NUM_1)) return false;
        // Open only the servo UART; do not change the shared servo-rail enable pin.
        ownsUart = SCSCL::begin(UART_NUM_1, 1000000, 6, 7);
        return ownsUart;
    }
    bool txFailed() const { return txUncertain; }
#if defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC)
    enum class DiagnosticRailState : uint8_t { Open, Owned, StopWaiting, Cutting, Accepted, Failed };
    enum DiagnosticRailEvidence : uint32_t {
        LowPossible = 1, LowAccepted = 2, DirectionPossible = 4,
        DirectionAccepted = 8, CleanupUnknown = 16
    };
    DiagnosticRailState diagnosticRailStatus() const {
        return diagnosticRail.load(std::memory_order_acquire);
    }
    uint32_t diagnosticRailEvidence() const {
        diagnosticRail.load(std::memory_order_acquire);
        return diagnosticEvidence.load(std::memory_order_relaxed);
    }
    void requestDiagnosticRailCut() {
        auto current = diagnosticRail.load(std::memory_order_acquire);
        for (;;) {
            if (current == DiagnosticRailState::Open) {
                if (diagnosticRail.compare_exchange_weak(current, DiagnosticRailState::Cutting)) {
                    cutDiagnosticRail();
                    return;
                }
            } else if (current == DiagnosticRailState::Owned) {
                if (diagnosticRail.compare_exchange_weak(current, DiagnosticRailState::StopWaiting)) return;
            } else return;
        }
    }
#endif
    uint8_t getLastError() { return txUncertain || stopObservation.requested ? 0xff : SCSCL::getLastError(); }
    const EmergencyStopObservation& emergencyStopObservation() const { return stopObservation; }
    void emergencyStop() {
        if (stopObservation.requested) return;
        stopObservation.requested = true;
        stopObservation.uartOwned = ownsUart && !manualExternal;
        if (stopObservation.uartOwned && !txUncertain && !txPending) {
            uint8_t ids[] = {scs::yaw, scs::pitch};
            uint8_t values[] = {0, 0};
            stopObservation.txAttempted = true;
            terminalStop = true;
            syncWrite(ids, 2, scs::torque, values, 1);
            terminalStop = false;
            stopObservation.txDrained = !txUncertain && !txPending;
        }
        stopObservation.txUncertain = txUncertain || txPending;
        stopObservation.railAttempted = true;
        stopObservation.railRequestAccepted = requestRailOff();
    }
    // The caller must service Stop between polls; cancellation does not turn off
    // the shared rail or establish that either servo is stationary.
    bool beginManualPower() {
#if defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC)
        return false;
#else
        if (bspReserved || !benchPower || manualPower != ManualPowerState::Idle || powerAttempted || ready)
            return false;
        manualPower = ManualPowerState::Failed;
        powerAttempted = true;
        if (!beginReadOnly()) return false;
        manualPower = ManualPowerState::Expander;
        return true;
#endif
    }
    bool manualPowerReady() const { return manualPower == ManualPowerState::Ready; }
    ManualPowerState pollManualPower(uint32_t now) {
        if (manualExternal) return manualPower;
        return pollPower(now);
    }
    // The caller has exclusive ownership of an independent manual UART.
    bool beginManualExternalPower() {
        if (bspReserved || !benchPower || manualPower != ManualPowerState::Idle ||
            powerAttempted || ready || ownsUart) return false;
        manualExternal = true;
        powerAttempted = true;
        manualPower = ManualPowerState::Expander;
        return true;
    }
    ManualPowerState pollManualExternalPower(uint32_t now, bool stop) {
        if (!manualExternal) return manualPower;
        if (stop) cancelManualPower();
        return pollPower(now);
    }
private:
    ManualPowerState pollPower(uint32_t now) {
#if defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC)
        auto expected = DiagnosticRailState::Open;
        if (!diagnosticRail.compare_exchange_strong(expected, DiagnosticRailState::Owned)) {
            cancelManualPower();
            return manualPower;
        }
#endif
        switch (manualPower) {
            case ManualPowerState::Expander:
                manualPower = power.begin() ? ManualPowerState::Direction : ManualPowerState::Failed;
                break;
            case ManualPowerState::Direction:
                manualPower = power.setDirection(0, true) ? ManualPowerState::Pull : ManualPowerState::Failed;
                break;
            case ManualPowerState::Pull:
                manualPower = power.setPullMode(0, m5::PY32IOExpander_Class::pull_up) ?
                    ManualPowerState::Rail : ManualPowerState::Failed;
                break;
            case ManualPowerState::Rail:
                manualPower = power.digitalWrite(0, true) ? ManualPowerState::Readback : ManualPowerState::Failed;
                break;
            case ManualPowerState::Readback:
                if (!power.getWriteValue(0)) manualPower = ManualPowerState::Failed;
                else { manualPowerAt = now; manualPower = ManualPowerState::Settling; }
                break;
            case ManualPowerState::Settling:
                if (now - manualPowerAt >= 500 && now - manualPowerAt <= INT32_MAX)
                    manualPower = manualExternal ? ManualPowerState::Ready : ManualPowerState::PingYaw;
                break;
            case ManualPowerState::PingYaw:
                manualPower = Ping(scs::yaw) == scs::yaw && !txFailed() ?
                    ManualPowerState::PingPitch : ManualPowerState::Failed;
                break;
            case ManualPowerState::PingPitch:
                manualPower = Ping(scs::pitch) == scs::pitch && !txFailed() ?
                    ManualPowerState::Ready : ManualPowerState::Failed;
                break;
            default: break;
        }
#if defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC)
        expected = DiagnosticRailState::Owned;
        if (!diagnosticRail.compare_exchange_strong(expected, DiagnosticRailState::Open)) {
            if (expected == DiagnosticRailState::StopWaiting) {
                expected = DiagnosticRailState::StopWaiting;
                if (diagnosticRail.compare_exchange_strong(expected, DiagnosticRailState::Cutting))
                    cutDiagnosticRail();
            }
            cancelManualPower();
        }
#endif
        return manualPower;
    }
public:
    void cancelManualPower() {
        if (manualPower != ManualPowerState::Failed)
            manualPower = ManualPowerState::Cancelled;
    }
    bool requestRailOff() {
#if defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC)
        requestDiagnosticRailCut();
        powerAttempted = true;
        ready = false;
        cancelManualPower();
        return diagnosticRailStatus() == DiagnosticRailState::Accepted;
#else
        if (railOffAttempted) return false;
        railOffAttempted = true;
        powerAttempted = true;
        ready = false;
        cancelManualPower();
        const bool present = power.begin();
        const bool low = power.digitalWrite(0, false);
        const bool output = low && power.setDirection(0, true);
        return present && low && output;
#endif
    }
    ~StackChanScsBus() {
        // The pinned begin() can retain the port number on an ownership failure.
        if (ownsUart) SCSCL::end();
    }
    StackChanScsBus(const StackChanScsBus&) = delete;
    StackChanScsBus& operator=(const StackChanScsBus&) = delete;
protected:
    int writeSCS(unsigned char* data, int length) override {
        if (txUncertain || !ownsUart || (stopObservation.requested && !terminalStop)) return 0;
        txPending = true;
        const int sent = SCSCL::writeSCS(data, length);
        if (sent != length) txUncertain = true;
        return sent;
    }
    int writeSCS(unsigned char value) override {
        if (txUncertain || !ownsUart || (stopObservation.requested && !terminalStop)) return 0;
        txPending = true;
        const int sent = SCSCL::writeSCS(value);
        if (sent != 1) txUncertain = true;
        return sent;
    }
    void rFlushSCS() override {
        if (!terminalStop && !stopObservation.requested) SCSCL::rFlushSCS();
    }
    void wFlushSCS() override {
        if (!ownsUart || !txPending || txUncertain) return;
        if (uart_wait_tx_done(uart_num, pdMS_TO_TICKS(100)) != ESP_OK) txUncertain = true;
        else if (!txUncertain) txPending = false;
    }
private:
#if defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC)
    std::atomic<DiagnosticRailState> diagnosticRail{DiagnosticRailState::Open};
    std::atomic<uint32_t> diagnosticEvidence{0};
    void cutDiagnosticRail() {
        uint32_t evidence = 0;
        const int port = M5.In_I2C.getPort();
        const auto low = m5gfx::i2c::tryWriteRegister8(port, 0x6f, 0x05, 0, 0xfe, 100000);
        if (low.write_possible) evidence |= LowPossible;
        if (low.ok) evidence |= LowAccepted;
        if (low.cleanup_unknown) evidence |= CleanupUnknown;
        bool ok = low.ok;
        if (ok) {
            const auto direction = m5gfx::i2c::tryWriteRegister8(port, 0x6f, 0x03, 1, 0xff, 100000);
            if (direction.write_possible) evidence |= DirectionPossible;
            if (direction.ok) evidence |= DirectionAccepted;
            if (direction.cleanup_unknown) evidence |= CleanupUnknown;
            ok = direction.ok;
        }
        diagnosticEvidence.store(evidence, std::memory_order_relaxed);
        diagnosticRail.store(ok ? DiagnosticRailState::Accepted : DiagnosticRailState::Failed,
                             std::memory_order_release);
    }
#endif
    // Servos need time to boot after the rail turns on (the BSP waits 200 ms).
    // Read-only Ping probes only; no write is ever retried. Failure is left to
    // the driver's first feedback read, which records the diagnostic.
    void settle() {
        delay(500);
        for (int attempt = 0; attempt < 15; ++attempt) {
            if (Ping(scs::yaw) == scs::yaw && !txFailed() &&
                Ping(scs::pitch) == scs::pitch && !txFailed()) return;
            if (txFailed()) return;
            delay(100);
        }
    }
    bool ownsUart = false;
    bool bspReserved = false;
    const bool benchPower;
    bool ready = false, powerAttempted = false, manualExternal = false, railOffAttempted = false;
    bool txUncertain = false, txPending = false, terminalStop = false;
    EmergencyStopObservation stopObservation{};
    ManualPowerState manualPower = ManualPowerState::Idle;
    uint32_t manualPowerAt = 0;
    m5::PY32IOExpander_Class power;
};

static_assert(scs::minimum == SCSCL_MIN_ANGLE_LIMIT_L && scs::maximum == SCSCL_MAX_ANGLE_LIMIT_L &&
    scs::torque == SCSCL_TORQUE_ENABLE && scs::goal == SCSCL_GOAL_POSITION_L &&
    scs::position == SCSCL_PRESENT_POSITION_L && scs::moving == SCSCL_MOVING,
    "Review the raw motion adapter when BSP registers change.");

} // namespace lab

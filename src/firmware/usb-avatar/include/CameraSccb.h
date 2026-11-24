#pragma once

#include <M5Unified.h>
#include <atomic>
#include <freertos/task.h>

extern "C" bool netbot_camera_power_lock();
extern "C" bool netbot_camera_power_unlock();

namespace lab {
inline bool (*cameraNfcBusKnown)() = nullptr;
class CameraSccb {
public:
    std::atomic<bool> failed{false}, uncertain{false};
    void begin(const std::atomic<bool>& cancel) {
        failed = false;
        cancelled = &cancel;
        owner = xTaskGetCurrentTaskHandle();
        active = true;
    }
    void end() { active = false; owner = nullptr; }
    bool ready() const {
        return active && !failed && !uncertain.load() && cancelled && !cancelled->load() &&
            (!cameraNfcBusKnown || cameraNfcBusKnown()) &&
            owner == xTaskGetCurrentTaskHandle() && M5.In_I2C.isEnabled() &&
            M5.In_I2C.getSDA() == 12 && M5.In_I2C.getSCL() == 11;
    }
    bool transfer(uint8_t address, uint8_t reg, uint8_t& value, bool write) {
        if (!ready()) { failed = true; return false; }
        vTaskDelay(1);
        if (!ready()) { failed = true; return false; }
        bool cleanupUnknown = false;
        if (!m5gfx::i2c::beginTransaction(M5.In_I2C.getPort(), address, 100000, false,
                                         true, cleanupUnknown).has_value()) {
            failed = true;
            uncertain = uncertain || cleanupUnknown;
            return false;
        }
        bool ok = M5.In_I2C.write(&reg, 1);
        bool payload = false;
        if (write) {
            if (ok) { payload = true; ok = M5.In_I2C.write(&value, 1); }
        } else {
            ok = ok && M5.In_I2C.restart(address, true, 100000) && M5.In_I2C.read(&value, 1, true);
        }
        const bool stopped = M5.In_I2C.stop();
        if (!ok || !stopped) {
            failed.store(true);
            if (payload || !stopped) uncertain.store(true);
        }
        return ok && stopped;
    }
    bool supply(uint8_t bit, uint8_t voltageRegister) {
        uint8_t enabled = 0, voltage = 0;
        return transfer(0x34, 0x90, enabled, false) && (enabled & bit) &&
            transfer(0x34, voltageRegister, voltage, false) && (voltage & 31) == 28;
    }
    bool reset(bool high) {
        if (!ready() || !netbot_camera_power_lock()) return false;
        uint8_t config = 0, mode = 0, before = 0;
        bool ok = transfer(0x58, 0x05, config, false) && !(config & 1) &&
            transfer(0x58, 0x13, mode, false) && (mode & 1) &&
            transfer(0x58, 0x03, before, false);
        const uint8_t after = static_cast<uint8_t>((before & ~1) | (high ? 1 : 0));
        if (ok && after != before) {
            uint8_t wanted = after, observed = 0;
            ok = transfer(0x58, 0x03, wanted, true) &&
                transfer(0x58, 0x03, observed, false) && observed == after;
            if (!ok) uncertain = true;
        }
        if (!netbot_camera_power_unlock()) { ok = false; uncertain = true; }
        return ok;
    }
private:
    bool active = false;
    TaskHandle_t owner = nullptr;
    const std::atomic<bool>* cancelled = nullptr;
};
extern CameraSccb cameraSccb;
} // namespace lab

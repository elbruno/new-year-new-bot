// Goal: Coordinate borrowed I2C access for NFC operations.
// Responsibilities: Track transaction and cleanup outcomes on the shared bus.
// How it works:
// 1. NFC operations start a bus transaction when allowed.
// 2. Reads and writes use the borrowed bus.
// 3. Cleanup records whether the bus was safely released.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace lab {

enum class NfcCleanupPhase { Unknown, StartStop, CloseStop, RfControl };

inline const char* nfcCleanupReason(NfcCleanupPhase phase) {
    switch (phase) {
        case NfcCleanupPhase::StartStop: return "start_stop";
        case NfcCleanupPhase::CloseStop: return "close_stop";
        case NfcCleanupPhase::RfControl: return "rf_control";
        default: return "unknown";
    }
}

// One owner, only START/RESTART to STOP. No controller begin/release or power writes.
template<class Bus, class Admission>
class NfcBorrowedBus {
public:
    NfcBorrowedBus(Bus& bus, Admission& admission) : bus(bus), admission(admission) {}
    bool failed = false;
    bool stopUnknown = false;
    NfcCleanupPhase cleanupPhase = NfcCleanupPhase::Unknown;
    bool effectPossible = false;

    bool close() {
        if (!owned) return !stopUnknown;
        owned = false;
        if (!bus.stop()) {
            failed = stopUnknown = true;
            cleanupPhase = NfcCleanupPhase::CloseStop;
            return false;
        }
        return true;
    }
    bool read(uint8_t* data, size_t size) {
        if (!start(true)) return false;
        const bool read = bus.read(data, size, true);
        const bool stopped = close();
        if (!read) failed = true;
        return read && stopped;
    }
    bool write(const uint8_t* prefix, size_t prefixSize, const uint8_t* data, size_t size, bool stop) {
        if (!start(false)) return false;
        // The pinned reader uses no-STOP writes only for register-read prefixes.
        if (stop && (prefixSize || size)) effectPossible = true;
        const bool wrote = (!prefixSize || bus.write(prefix, prefixSize)) &&
            (!size || bus.write(data, size));
        if (!wrote) failed = true;
        const bool stopped = (stop || !wrote) ? close() : true;
        return wrote && stopped;
    }

private:
    Bus& bus;
    Admission& admission;
    bool owned = false;

    bool start(bool read) {
        if (failed || !admission.allowed()) { close(); return false; }
        if (owned) {
            if (bus.restart(0x50, read, 400000)) return true;
            failed = true;
            close();
            return false;
        }
        // A failed owned START is cleaned by the driver once; preserve unknown STOP.
        bool cleanupUnknown = false;
        if (!bus.tryStart(0x50, read, 400000, cleanupUnknown)) {
            failed = true;
            stopUnknown = cleanupUnknown;
            if (cleanupUnknown) cleanupPhase = NfcCleanupPhase::StartStop;
            return false;
        }
        owned = true;
        if (!admission.allowed()) { close(); return false; }
        return true;
    }
};
} // namespace lab

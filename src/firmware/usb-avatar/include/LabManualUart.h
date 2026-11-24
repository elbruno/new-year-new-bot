#pragma once

#include <atomic>
#include "LabManualScs.h"

namespace lab {

// Api is bound to the target UART or a synthetic test. No method is called
// until the owner explicitly opens the transport; a failed instance is terminal.
template <typename Api>
class ManualUart {
public:
    using Task = typename Api::Task;
    ManualUart(Api& api, const ManualStopLatch& stop) : api(api), stop(stop) {}
    ManualUart(const ManualUart&) = delete;
    ManualUart& operator=(const ManualUart&) = delete;

    bool open() {
        if (attempted || stop.requested()) return false;
        attempted = true;
        owner = api.currentTask();
        if (!owner || api.installed() || stop.requested() || !api.installNoTxRing() ||
            stop.requested() || !api.configure() || stop.requested() || !api.setPins() || stop.requested()) {
            failed = true;
            return false;
        }
        opened = true;
        return true;
    }

    uint64_t nowUs() const { return api.nowUs(); }
    bool stopRequested() const { return stop.requested(); }
    void setStopAllowance(bool allowed) { stopAllowance = allowed; }
    ManualIoState txIdle() { return waitTx(); }
    ManualIoState txDone() { return waitTx(); }
    // Only the initial, settled-rail window may consume unsolicited RX.
    // Snapshot once, retain every discarded byte, then require 20 ms silence.
    ManualIoState synchronizeStartupRx() {
        if (!enter()) { syncReason = "owner_or_stop"; return ManualIoState::Failed; }
        const auto result = pollStartupRx();
        leave();
        if (result == ManualIoState::Failed) failed = true;
        return result;
    }
    void describeStartupRx(char* output, size_t capacity) const {
        char hex[sizeof(syncBytes) * 2 + 1]{};
        for (size_t i = 0; i < syncRead; ++i)
            snprintf(hex + i * 2, sizeof(hex) - i * 2, "%02x", syncBytes[i]);
        snprintf(output, capacity, "uart1_sync_%s_initial%u_read%u_remaining%u_driver%d_hex%s",
            syncReason, static_cast<unsigned>(syncInitial), static_cast<unsigned>(syncRead),
            static_cast<unsigned>(syncRemaining), syncError, syncRead ? hex : "none");
    }
    ManualRxResult rxEmpty() {
        if (!opened || failed.load() || (stop.requested() && !stopAllowance)) {
            failed = true;
            return {ManualRxState::Unavailable};
        }
        if (api.currentTask() != owner || executing.test_and_set()) {
            failed = true;
            return {ManualRxState::OwnershipError};
        }
        size_t count = 0;
        const int error = api.rxCount(count);
        leave();
        if (error != 0) {
            failed = true;
            return {ManualRxState::DriverError, count, error};
        }
        if (count != 0) {
            failed = true;
            return {ManualRxState::PendingBytes, count};
        }
        return {ManualRxState::Empty};
    }
    int send(const uint8_t* bytes, size_t count) {
        if (!enter()) return -1;
        transmitted = true;
        const int sent = api.send(bytes, count);
        leave();
        if (sent != static_cast<int>(count)) failed = true;
        return sent;
    }
    int read(uint8_t* byte) {
        if (!enter()) return -1;
        const int count = api.read(byte);
        leave();
        if (count < 0 || count > 1) failed = true;
        return count;
    }

private:
    Api& api;
    const ManualStopLatch& stop;
    Task owner{};
    bool attempted = false, opened = false, stopAllowance = false;
    bool transmitted = false, syncStarted = false, syncDrained = false, syncDone = false;
    uint64_t syncAt = 0, syncLast = 0, syncQuietAt = 0;
    size_t syncInitial = 0, syncRead = 0, syncRemaining = 0;
    uint8_t syncBytes[16]{};
    int syncError = 0;
    const char* syncReason = "pending";
    std::atomic<bool> failed{false};
    std::atomic_flag executing = ATOMIC_FLAG_INIT;

    bool enter() {
        if (!opened || failed.load() || (stop.requested() && !stopAllowance) || api.currentTask() != owner) {
            failed = true;
            return false;
        }
        if (executing.test_and_set()) {
            failed = true;
            return false;
        }
        return true;
    }
    void leave() { executing.clear(); }
    ManualIoState pollStartupRx() {
        if (transmitted || syncDone) {
            syncReason = "reuse";
            return ManualIoState::Failed;
        }
        const uint64_t now = api.nowUs();
        if (!syncStarted) {
            syncStarted = true;
            syncAt = syncLast = now;
            syncError = api.rxCount(syncInitial);
            syncRemaining = syncInitial;
            if (syncError) { syncReason = "driver"; return ManualIoState::Failed; }
            if (syncInitial > sizeof(syncBytes)) { syncReason = "overflow"; return ManualIoState::Failed; }
        }
        if (now < syncLast || now - syncAt >= 100000) {
            syncReason = now < syncLast ? "clock" : "timeout";
            return ManualIoState::Failed;
        }
        syncLast = now;
        const auto idle = api.waitTx();
        if (idle == ManualIoState::Failed) { syncReason = "tx"; return ManualIoState::Failed; }
        if (idle != ManualIoState::Ready) return ManualIoState::Pending;
        if (syncRead < syncInitial) {
            if (api.read(&syncBytes[syncRead]) != 1) {
                syncReason = "read";
                return ManualIoState::Failed;
            }
            ++syncRead;
            --syncRemaining;
            return ManualIoState::Pending;
        }
        syncError = api.rxCount(syncRemaining);
        if (syncError) { syncReason = "driver"; return ManualIoState::Failed; }
        if (syncRemaining) { syncReason = "noisy"; return ManualIoState::Failed; }
        if (!syncDrained) { syncDrained = true; syncQuietAt = now; }
        if (now - syncQuietAt < 20000) return ManualIoState::Pending;
        syncDone = true;
        syncReason = "ready";
        return ManualIoState::Ready;
    }
    ManualIoState waitTx() {
        if (!enter()) return ManualIoState::Failed;
        const auto result = api.waitTx();
        leave();
        if (result == ManualIoState::Failed) failed = true;
        return result;
    }
};

} // namespace lab

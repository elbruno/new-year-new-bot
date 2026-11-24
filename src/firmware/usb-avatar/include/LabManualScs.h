#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <atomic>

namespace lab {

class ManualStopLatch {
public:
    uint32_t request() {
        uint32_t old = value.load();
        for (;;) {
            if (old >= UINT32_MAX - 2) {
                value.store(UINT32_MAX);
                return UINT32_MAX;
            }
            const uint32_t next = (old + 2) | 1;
            if (value.compare_exchange_weak(old, next)) return next;
        }
    }
    bool requested() const { return (value.load() & 1) != 0; }
    uint32_t snapshot() const { return value.load(); }
    bool clear(uint32_t expected) {
        return expected != UINT32_MAX && (expected & 1) &&
            value.compare_exchange_strong(expected, expected & ~uint32_t{1});
    }
private:
    std::atomic<uint32_t> value{0};
};

enum class ManualIoState { Pending, Ready, Failed };
enum class ManualRxState { Empty, PendingBytes, DriverError, OwnershipError, Unavailable };
struct ManualRxResult {
    ManualRxState state = ManualRxState::Unavailable;
    size_t count = 0;
    int error = 0;
};
enum class ManualScsState { Idle, WaitIdle, CheckRx, Send, Drain, Receive, Complete, Cancelled, Fault };
enum class ManualScsError {
    None, Invalid, Stop, Timeout, Clock, Io, RxPending, RxDriver, RxOwnership,
    PartialWrite, Response, Status, Abandoned
};

// Experimental protocol engine. Its caller owns the UART, absolute clock,
// Stop latch and serialization; this class does not establish a target deadline.
template <typename Io>
class ManualScs {
public:
    explicit ManualScs(Io& io) : io(io) {}

    bool ping(uint8_t id, uint64_t timeoutUs) {
        return start(id, 1, 0, nullptr, 0, 0, timeoutUs);
    }
    bool readByte(uint8_t id, uint8_t address, uint64_t timeoutUs) {
        if (!readable(address, false)) return false;
        const uint8_t width = 1;
        return start(id, 2, address, &width, 1, width, timeoutUs);
    }
    bool readWord(uint8_t id, uint8_t address, uint64_t timeoutUs) {
        if (!readable(address, true)) return false;
        const uint8_t width = 2;
        return start(id, 2, address, &width, 1, width, timeoutUs);
    }
    bool goal(uint8_t id, uint16_t position, uint16_t duration, uint64_t timeoutUs) {
        if (position > 1000 || !duration) return false;
        const uint8_t data[] = {static_cast<uint8_t>(position >> 8), static_cast<uint8_t>(position),
            static_cast<uint8_t>(duration >> 8), static_cast<uint8_t>(duration), 0, 0};
        return start(id, 3, 42, data, sizeof(data), 0, timeoutUs);
    }
    bool enable(uint8_t id, uint64_t timeoutUs) {
        const uint8_t enabled = 1;
        return start(id, 3, 40, &enabled, 1, 0, timeoutUs);
    }
    bool disable(uint8_t id, uint64_t timeoutUs) {
        const uint8_t disabled = 0;
        return start(id, 3, 40, &disabled, 1, 0, timeoutUs);
    }
    bool resumeStopped() {
        if (io.stopRequested() || current != ManualScsState::Complete || !safetyMode) return false;
        current = ManualScsState::Idle;
        safetyMode = false;
        io.setStopAllowance(false);
        return true;
    }
    bool rearmClosed() {
        if (io.stopRequested() || current != ManualScsState::Complete || safetyMode) return false;
        current = ManualScsState::Idle;
        safetyHoldAttempted = false;
        safetyDisableAttempts = 0;
        return true;
    }
    bool safetyReadByte(uint8_t id, uint8_t address, uint64_t timeoutUs) {
        if (!readable(address, false)) return false;
        const uint8_t width = 1;
        return start(id, 2, address, &width, 1, width, timeoutUs, true);
    }
    bool safetyReadWord(uint8_t id, uint8_t address, uint64_t timeoutUs) {
        if (!readable(address, true)) return false;
        const uint8_t width = 2;
        return start(id, 2, address, &width, 1, width, timeoutUs, true);
    }
    bool safetyHold(uint8_t id, uint16_t position, uint16_t duration, uint64_t timeoutUs) {
        if (safetyHoldAttempted || position > 1000 || !duration) return false;
        const uint8_t data[] = {static_cast<uint8_t>(position >> 8), static_cast<uint8_t>(position),
            static_cast<uint8_t>(duration >> 8), static_cast<uint8_t>(duration), 0, 0};
        if (!start(id, 3, 42, data, sizeof(data), 0, timeoutUs, true)) return false;
        safetyHoldAttempted = true;
        return true;
    }
    bool safetyDisable(uint8_t id, uint64_t timeoutUs) {
        if (id != 1 && id != 2) return false;
        const uint8_t mask = static_cast<uint8_t>(1u << (id - 1));
        if (safetyDisableAttempts & mask) return false;
        const uint8_t disabled = 0;
        if (!start(id, 3, 40, &disabled, 1, 0, timeoutUs, true)) return false;
        safetyDisableAttempts |= mask;
        return true;
    }
    bool enterStop() {
        if (!io.stopRequested()) return false;
        if (current == ManualScsState::Idle || current == ManualScsState::Complete) {
            failurePhase = current;
            current = ManualScsState::Cancelled;
            failure = ManualScsError::Stop;
            io.setStopAllowance(false);
        }
        return current == ManualScsState::Cancelled;
    }
    void abandon() { if (current != ManualScsState::Fault) fault(ManualScsError::Abandoned); }
    ManualScsState state() const { return current; }
    ManualScsError error() const { return failure; }
    bool possiblyWritten() const { return sent; }
    int value() const { return result; }
    void describeFailure(char* output, size_t capacity) const {
        if (failure == ManualScsError::None) {
            snprintf(output, capacity, "manual_bus_start_rejected");
            return;
        }
        const char* phase = "start";
        switch (failurePhase) {
            case ManualScsState::WaitIdle: phase = "wait_idle"; break;
            case ManualScsState::CheckRx: phase = "check_rx"; break;
            case ManualScsState::Send: phase = "send"; break;
            case ManualScsState::Drain: phase = "drain"; break;
            case ManualScsState::Receive: phase = "receive"; break;
            default: break;
        }
        const char* reason = "unavailable";
        switch (failure) {
            case ManualScsError::Invalid: reason = "invalid"; break;
            case ManualScsError::Stop: reason = "stop"; break;
            case ManualScsError::Timeout: reason = "timeout"; break;
            case ManualScsError::Clock: reason = "clock"; break;
            case ManualScsError::Io: reason = "io"; break;
            case ManualScsError::RxPending: reason = "pending"; break;
            case ManualScsError::RxDriver: reason = "driver"; break;
            case ManualScsError::RxOwnership: reason = "owner"; break;
            case ManualScsError::PartialWrite: reason = "partial_tx"; break;
            case ManualScsError::Response: reason = "response"; break;
            case ManualScsError::Status: reason = "status"; break;
            case ManualScsError::Abandoned: reason = "abandoned"; break;
            default: break;
        }
        char operation[8], cause[20];
        if (tx[4] == 1) snprintf(operation, sizeof(operation), "ping");
        else snprintf(operation, sizeof(operation), "%c%u", tx[4] == 2 ? 'r' : 'w',
            static_cast<unsigned>(tx[5]));
        if (failure == ManualScsError::Status)
            snprintf(cause, sizeof(cause), "status%u", static_cast<unsigned>(rx[4]));
        else if (failure == ManualScsError::RxDriver)
            snprintf(cause, sizeof(cause), "driver%d", checkRxError);
        else snprintf(cause, sizeof(cause), "%s", reason);
        const auto count = failurePhase == ManualScsState::CheckRx ? checkRxCount : rxSize;
        snprintf(output, capacity, "manual_s%u_%s_%s_%s_rx%u",
            static_cast<unsigned>(servo), operation, phase, cause, static_cast<unsigned>(count));
    }

    void tick() {
        if (current == ManualScsState::Idle || current == ManualScsState::Complete ||
            current == ManualScsState::Cancelled || current == ManualScsState::Fault) return;
        if (!check()) return;
        switch (current) {
            case ManualScsState::WaitIdle: {
                const auto status = io.txIdle();
                if (!check()) return;
                if (status == ManualIoState::Failed) fault(ManualScsError::Io);
                else if (status == ManualIoState::Ready) current = ManualScsState::CheckRx;
                return;
            }
            case ManualScsState::CheckRx: {
                const auto result = io.rxEmpty();
                if (!check()) return;
                checkRxCount = result.count;
                checkRxError = result.error;
                switch (result.state) {
                    case ManualRxState::Empty: current = ManualScsState::Send; break;
                    case ManualRxState::PendingBytes: fault(ManualScsError::RxPending); break;
                    case ManualRxState::DriverError: fault(ManualScsError::RxDriver); break;
                    case ManualRxState::OwnershipError: fault(ManualScsError::RxOwnership); break;
                    default: fault(ManualScsError::Io); break;
                }
                return;
            }
            case ManualScsState::Send: {
                // A failed call can have transmitted an unknown prefix.
                sent = true;
                const int count = io.send(tx, txSize);
                if (!check()) return;
                if (count != static_cast<int>(txSize))
                    fault(count < 0 ? ManualScsError::Io : ManualScsError::PartialWrite);
                else current = ManualScsState::Drain;
                return;
            }
            case ManualScsState::Drain: {
                const auto status = io.txDone();
                if (!check()) return;
                if (status == ManualIoState::Failed) fault(ManualScsError::Io);
                else if (status == ManualIoState::Ready) current = ManualScsState::Receive;
                return;
            }
            case ManualScsState::Receive: {
                uint8_t byte = 0;
                const int count = io.read(&byte);
                if (!check()) return;
                if (count < 0 || count > 1) { fault(ManualScsError::Io); return; }
                if (!count) return;
                rx[rxSize++] = byte;
                if ((rxSize == 1 && rx[0] != 0xff) ||
                    (rxSize == 2 && rx[1] != 0xff) ||
                    (rxSize == 3 && rx[2] != servo) ||
                    (rxSize == 4 && rx[3] != expectedData + 2)) {
                    fault(ManualScsError::Response);
                    return;
                }
                if (rxSize == static_cast<size_t>(expectedData) + 6) finish();
                return;
            }
            default: return;
        }
    }

private:
    Io& io;
    ManualScsState current = ManualScsState::Idle;
    ManualScsState failurePhase = ManualScsState::Idle;
    ManualScsError failure = ManualScsError::None;
    uint64_t deadline = 0, lastNow = 0;
    uint8_t tx[13]{}, rx[8]{}, servo = 0, expectedData = 0;
    size_t txSize = 0, rxSize = 0;
    size_t checkRxCount = 0;
    int checkRxError = 0;
    int result = -1;
    bool sent = false, cancelled = false, safetyMode = false, safetyHoldAttempted = false;
    uint8_t safetyDisableAttempts = 0;

    static bool readable(uint8_t address, bool word) {
        return word ? address == 9 || address == 11 || address == 42 || address == 56 :
            address == 40 || address == 66;
    }
    bool start(uint8_t id, uint8_t instruction, uint8_t address, const uint8_t* data,
               size_t count, uint8_t responseData, uint64_t timeoutUs, bool safety = false) {
        if (safety) {
            if (!io.stopRequested() ||
                (current != ManualScsState::Cancelled &&
                 !(safetyMode && current == ManualScsState::Complete))) return false;
        } else if (current != ManualScsState::Idle && current != ManualScsState::Complete) return false;
        if ((id != 1 && id != 2) || !timeoutUs || count > 6) return false;
        const uint64_t now = io.nowUs();
        if ((!safety && io.stopRequested()) || UINT64_MAX - now < timeoutUs) return false;
        safetyMode = safety;
        io.setStopAllowance(safety);
        lastNow = now;
        deadline = now + timeoutUs;
        servo = id;
        expectedData = responseData;
        txSize = 0;
        rxSize = 0;
        checkRxCount = 0;
        checkRxError = 0;
        result = -1;
        sent = false;
        cancelled = false;
        failure = ManualScsError::None;
        tx[txSize++] = 0xff;
        tx[txSize++] = 0xff;
        tx[txSize++] = id;
        const uint8_t length = static_cast<uint8_t>(count ? count + 3 : 2);
        tx[txSize++] = length;
        tx[txSize++] = instruction;
        uint8_t sum = static_cast<uint8_t>(id + length + instruction);
        if (count) {
            tx[txSize++] = address;
            sum = static_cast<uint8_t>(sum + address);
            for (size_t i = 0; i < count; ++i) {
                tx[txSize++] = data[i];
                sum = static_cast<uint8_t>(sum + data[i]);
            }
        }
        tx[txSize++] = static_cast<uint8_t>(~sum);
        current = ManualScsState::WaitIdle;
        return true;
    }
    bool check() {
        const uint64_t now = io.nowUs();
        if (now < lastNow) { fault(ManualScsError::Clock); return false; }
        lastNow = now;
        if (io.stopRequested() && !safetyMode) {
            if (!sent) {
                failurePhase = current;
                current = ManualScsState::Cancelled;
                failure = ManualScsError::Stop;
                io.setStopAllowance(false);
                return false;
            }
            cancelled = true;
            io.setStopAllowance(true);
        }
        if (now >= deadline) { fault(ManualScsError::Timeout); return false; }
        return true;
    }
    void fault(ManualScsError error) {
        failurePhase = current;
        failure = error;
        current = ManualScsState::Fault;
        io.setStopAllowance(false);
    }
    void finish() {
        uint8_t sum = 0;
        for (size_t i = 2; i + 1 < rxSize; ++i) sum = static_cast<uint8_t>(sum + rx[i]);
        if (rx[rxSize - 1] != static_cast<uint8_t>(~sum)) { fault(ManualScsError::Response); return; }
        if (rx[4]) { fault(ManualScsError::Status); return; }
        result = expectedData == 2 ? (static_cast<int>(rx[5]) << 8) | rx[6] :
            expectedData == 1 ? rx[5] : servo;
        if (cancelled) failurePhase = current;
        current = cancelled ? ManualScsState::Cancelled : ManualScsState::Complete;
        if (cancelled) failure = ManualScsError::Stop;
        io.setStopAllowance(false);
    }
};

} // namespace lab

#pragma once

#include <M5Unified.h>
#include <M5UnitUnifiedNFC.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "LabMenu.h"
#include "NfcBorrowedBus.h"
#include "NfcText.h"

#if !defined(CORE_DEBUG_LEVEL) || CORE_DEBUG_LEVEL != 0 || (defined(M5_LOG_LEVEL) && M5_LOG_LEVEL != 0)
#error "NFC phone payload and tag diagnostics must remain disabled."
#endif

namespace lab {

class NfcAdmission {
public:
    bool (*busUsable)() = nullptr;
    std::atomic<bool> cancel{false};
    uint32_t started = 0;
    bool cleaning = false;
    bool allowed() const {
        return (!busUsable || busUsable()) &&
            (cleaning || (!cancel.load() && millis() - started < Menu::kTimeoutMs));
    }
};

struct NfcInternalBus {
    m5::I2C_Class& bus;
    bool tryStart(uint8_t address, bool read, uint32_t clock, bool& cleanupUnknown) {
        return m5gfx::i2c::beginTransaction(bus.getPort(), address, clock, read, true, cleanupUnknown).has_value();
    }
    bool restart(uint8_t address, bool read, uint32_t clock) { return bus.restart(address, read, clock); }
    bool read(uint8_t* data, size_t size, bool nack) { return bus.read(data, size, nack); }
    bool write(const uint8_t* data, size_t size) { return bus.write(data, size); }
    bool stop() { return bus.stop(); }
};

class NfcBusImpl : public m5::unit::AdapterI2C::I2CImpl {
public:
    using Error = m5::hal::error::error_t;
    NfcBusImpl(NfcBorrowedBus<NfcInternalBus, NfcAdmission>& transfer) : I2CImpl(0x50, 400000), transfer(transfer) {}
    bool begin() override { return true; } // M5 owns the already initialized controller.
    bool end() override { return transfer.close(); }
    Error readWithTransaction(uint8_t* data, size_t size) override {
        vTaskDelay(1);
        return result(transfer.read(data, size));
    }
    Error writeWithTransaction(const uint8_t* data, size_t size, uint32_t stop) override {
        vTaskDelay(1);
        return result(transfer.write(nullptr, 0, data, size, stop));
    }
    Error writeWithTransaction(uint8_t reg, const uint8_t* data, size_t size, uint32_t stop) override {
        vTaskDelay(1);
        return result(transfer.write(&reg, 1, data, size, stop));
    }
    Error writeWithTransaction(uint16_t reg, const uint8_t* data, size_t size, uint32_t stop) override {
        vTaskDelay(1);
        const uint8_t prefix[] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg)};
        return result(transfer.write(prefix, 2, data, size, stop));
    }
    Error wakeup() override {
        vTaskDelay(1);
        return result(transfer.write(nullptr, 0, nullptr, 0, true));
    }
private:
    NfcBorrowedBus<NfcInternalBus, NfcAdmission>& transfer;
    static Error result(bool success) { return success ? Error::OK : Error::I2C_BUS_ERROR; }
};

class NfcAdapter : public m5::unit::AdapterI2C {
public:
    explicit NfcAdapter(NfcBorrowedBus<NfcInternalBus, NfcAdmission>& transfer) {
        _impl.reset(new NfcBusImpl(transfer));
    }
};

class NfcLayer : public m5::nfc::NFCLayerA {
public:
    NfcLayer(m5::unit::UnitNFC& unit, NfcAdmission& admission) : NFCLayerA(unit), admission(admission) {}
    bool cancelled() const { return !admission.allowed(); }
    bool apdu(uint8_t* rx, uint16_t& received, const uint8_t* command, uint16_t size) {
        return isoDEP()->transceiveAPDU(rx, received, command, size);
    }
    bool transceive(uint8_t* rx, uint16_t& received, const uint8_t* command,
                    uint16_t size, uint32_t timeout) override {
        if (cancelled()) { received = 0; return false; }
        vTaskDelay(1);
        const uint32_t elapsed = millis() - admission.started;
        if (admission.cancel.load() || elapsed >= Menu::kTimeoutMs) { received = 0; return false; }
        const uint32_t remaining = Menu::kTimeoutMs - elapsed;
        return NFCLayerA::transceive(rx, received, command, size, std::min(timeout, remaining));
    }
    bool receive(uint8_t* rx, uint16_t& received, uint32_t timeout) override {
        const uint32_t elapsed = millis() - admission.started;
        if (admission.cancel.load() || elapsed >= Menu::kTimeoutMs) { received = 0; return false; }
        return NFCLayerA::receive(rx, received,
            std::min(timeout, Menu::kTimeoutMs - elapsed));
    }
private:
    NfcAdmission& admission;
};

class DemoNfc {
public:
    explicit DemoNfc(bool (*busUsable)() = nullptr) { admission.busUsable = busUsable; }
    bool busKnown() const { return !cleanupUnknown.load(); }
    const char* state = "select_read";
    const char* cleanupReason = "unknown";
    char text[256]{};
    uint32_t revision = 0;
    bool isBusy() const { return busy; }

    bool read(uint32_t now) {
        if (busy || latched) { state = latched ? "nfc_cleanup_unknown" : "nfc_busy"; ++revision; return false; }
        if (admission.busUsable && !admission.busUsable()) {
            state = "nfc_shared_bus_unavailable";
            ++revision;
            return false;
        }
        if (!M5.In_I2C.isEnabled() || M5.In_I2C.getSDA() != 12 || M5.In_I2C.getSCL() != 11) {
            state = "nfc_internal_bus_unavailable";
            ++revision;
            return false;
        }
        text[0] = '\0';
        cleanupReason = "unknown";
        admission.cancel.store(false);
        admission.started = now;
        admission.cleaning = false;
        done.store(false);
        busy = true;
        state = "reading_phone";
        ++revision;
        // One task avoids holding the UI/Stop loop through RF waits or WTX.
        // Stack covers the two 512-byte ISO-DEP frames, APDU vectors and NDEF buffers.
        if (xTaskCreate(run, "nfc-read", 8192, this, 1, nullptr) != pdPASS) {
            busy = false;
            state = "nfc_task_unavailable";
            ++revision;
            return false;
        }
        return true;
    }

    void tick(uint32_t now, bool visible, bool stopped) {
        if (busy && (!visible || stopped || now - admission.started >= Menu::kTimeoutMs)) {
            admission.cancel.store(true);
            if (state != cancelling) { state = cancelling; ++revision; }
        }
        if (!busy || !done.load(std::memory_order_acquire)) return;
        busy = false;
        latched = cleanupUnknown;
        const bool expired = now - admission.started >= Menu::kTimeoutMs;
        const bool cancelled = admission.cancel.load() || expired;
        state = cleanupUnknown ? "nfc_cleanup_unknown" : expired ? "nfc_timeout" : cancelled ? "nfc_cancelled" :
            result.error ? result.error : "received_text";
        cleanupReason = nfcCleanupReason(cleanupUnknown ? cleanupPhase : NfcCleanupPhase::Unknown);
        if (!cleanupUnknown && !cancelled && !result.error) memcpy(text, result.text, sizeof(text));
        ++revision;
    }

private:
    static constexpr const char* cancelling = "nfc_cancelling";
    NfcAdmission admission;
    std::atomic<bool> done{false};
    bool busy = false, latched = false;
    std::atomic<bool> cleanupUnknown{false};
    NfcCleanupPhase cleanupPhase = NfcCleanupPhase::Unknown;
    NfcText result;

    static void run(void* pointer) {
        auto& self = *static_cast<DemoNfc*>(pointer);
        NfcText result;
        bool cleanupUnknown = false;
        {
            NfcInternalBus bus{M5.In_I2C};
            NfcBorrowedBus<NfcInternalBus, NfcAdmission> transfer(bus, self.admission);
            m5::unit::UnitNFC unit;
            if (!unit.assign(M5.In_I2C)) result.error = "nfc_adapter_failed";
            else {
                // Public Adapter move assignment replaces only its owned implementation.
                // It does not transfer ownership of M5.In_I2C or call release().
                NfcAdapter borrowed(transfer);
                *unit.adapter() = std::move(borrowed);
                NfcLayer layer(unit, self.admission);
                if (!unit.begin()) result.error = "nfc_reader_unavailable";
                else {
                    m5::nfc::a::PICC picc;
                    // A single detect pass, not continuous polling or UID presentation.
                    if (!layer.detect(picc, 0)) result.error = "nfc_phone_not_detected";
                    else if (!picc.isISO14443_4()) result.error = "nfc_type4_required";
                    else if (!layer.reactivate(picc)) result.error = "nfc_activation_failed";
                    else result = readNfcText(layer);
                }
                // Stop RF once, including partial initialization. A failed bus operation
                // latches transfer.failed and prevents retry/cleanup writes on that bus.
                self.admission.cleaning = true;
                uint8_t control = 0xFF;
                cleanupUnknown = !transfer.close();
                if (!cleanupUnknown && transfer.effectPossible)
                    cleanupUnknown = !unit.writeOperationControl(0) ||
                        !unit.readOperationControl(control) || control != 0;
                if (transfer.failed && !cleanupUnknown) result.error = "nfc_i2c_failed";
            }
            cleanupUnknown = !transfer.close() || cleanupUnknown;
            self.cleanupPhase = transfer.stopUnknown ? transfer.cleanupPhase :
                cleanupUnknown ? NfcCleanupPhase::RfControl : NfcCleanupPhase::Unknown;
        }
        self.result = result;
        self.cleanupUnknown = cleanupUnknown;
        self.done.store(true, std::memory_order_release);
        vTaskDelete(nullptr);
    }
};
} // namespace lab

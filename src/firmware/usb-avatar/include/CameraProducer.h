// Goal: Coordinate camera capture and save requests.
// Responsibilities: Manage camera work, request identities, and shared resources.
// How it works:
// 1. Requests are checked against the current boot and camera state.
// 2. A backend performs admitted capture or save work.
// 3. Receipts expose outcomes while cancellation and cleanup are tracked.
#pragma once

#include <atomic>
#include <algorithm>
#include <mutex>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace lab {
constexpr size_t kCameraJpegCapacity = 131072;

struct CameraImage {
    uint8_t* bytes = nullptr;
    size_t length = 0;
    const char* error = nullptr;
    bool uncertain = false;
};
struct CameraSave {
    const char* error = nullptr;
    bool uncertain = false;
};
class CameraBackend {
public:
    virtual ~CameraBackend() = default;
    virtual CameraImage capture(const std::atomic<bool>& cancelled) = 0;
    virtual CameraSave save(uint32_t boot, uint32_t capture, const uint8_t* bytes, size_t length,
                            const std::atomic<bool>& cancelled) = 0;
};
struct CameraReceipt {
    uint32_t bootId = 0, requestId = 0, captureId = 0, byteLength = 0, saveRequestId = 0;
    const char* state = "failed";
    const char* saveState = "none";
    const char* error = "camera_unavailable";
    const char* saveError = nullptr;
};
struct CameraAdmission {
    int code = 409;
    const char* error = "camera_busy";
    CameraReceipt receipt;
};

class CameraProducer {
#ifdef NETBOT_TEST_BUILD
    friend struct DiagnosticCameraTestAccess;
#endif
public:
    using Launch = bool (*)(void (*)(void*), void*);
    CameraProducer(CameraBackend& backend, Launch launch, void (*finish)() = nullptr)
        : backend(backend), launch(launch), finish(finish) {}
    ~CameraProducer() { if (!isBusy()) free(image.bytes); }
    bool initialize(uint32_t boot) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!boot || receipt.bootId) return false;
        receipt.bootId = boot;
        return true;
    }
    bool isBusy() const { return busy.load() || imageHeld.load(); }
    bool sharedResourcesAvailable() {
        std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
        return lock.owns_lock() && !latched && !powerOffPending && !isBusy();
    }
    void cancel() { cancelled.store(true); }
    const char* preparePowerOff() {
        std::lock_guard<std::mutex> lock(mutex);
        powerOffPending = true;
        cancelled.store(true);
        if (latched) return "camera_cleanup_unknown";
        return isBusy() ? "camera_stopping" : nullptr;
    }
    void cancelPowerOff() {
        std::lock_guard<std::mutex> lock(mutex);
        powerOffPending = false;
    }
    void observeStop(uint32_t epoch) {
        if (stopEpoch.exchange(epoch) != epoch) cancel();
    }
    CameraAdmission capture(uint32_t boot, uint32_t request, bool httpRequest = false) {
        auto lock = mutexLock(httpRequest);
        if (!lock.owns_lock()) return {503, "camera_busy", {}};
        reconcileLaunchFailure();
        if (!boot || !request) return reject(400, "invalid_camera_identity");
        if (boot != receipt.bootId) return reject(409, "boot_changed");
        if (powerOffPending) return reject(409, "camera_stopping");
        if (isBusy()) return reject(409, "camera_busy");
        if (latched) return reject(409, "camera_cleanup_unknown");
        if (request == receipt.requestId) return reject(409, "camera_request_used");
        if (nextCapture == UINT32_MAX) return reject(409, "camera_identity_exhausted");
        free(image.bytes);
        image = {};
        receipt = {boot, request, 0, 0, 0, "accepted", "none", nullptr, nullptr};
        saving = false;
        cancelled.store(false);
        busy.store(true);
        const CameraAdmission response{202, nullptr, receipt};
        lock.unlock();
        return startAdmission(response, httpRequest);
    }
    CameraAdmission save(uint32_t boot, uint32_t capture, uint32_t request, bool httpRequest = false) {
        auto lock = mutexLock(httpRequest);
        if (!lock.owns_lock()) return {503, "camera_busy", {}};
        reconcileLaunchFailure();
        if (!boot || !capture || !request) return reject(400, "invalid_camera_identity");
        if (boot != receipt.bootId) return reject(409, "boot_changed");
        if (powerOffPending) return reject(409, "camera_stopping");
        if (busy.load()) return reject(409, "camera_busy");
        if (latched) return reject(409, "camera_cleanup_unknown");
        if (receipt.state != captured || capture != receipt.captureId)
            return reject(409, "camera_capture_changed");
        if (receipt.saveRequestId) return reject(409, "camera_save_used");
        receipt.saveRequestId = request;
        receipt.saveState = accepted;
        saving = true;
        cancelled.store(false);
        busy.store(true);
        const CameraAdmission response{202, nullptr, receipt};
        lock.unlock();
        return startAdmission(response, httpRequest);
    }
    bool snapshot(uint32_t boot, uint32_t request, uint32_t capture, uint32_t save,
                  CameraReceipt& output, bool httpRequest = false, bool* contended = nullptr) {
        if (contended) *contended = false;
        auto lock = mutexLock(httpRequest);
        if (!lock.owns_lock()) {
            if (contended) *contended = true;
            return false;
        }
        reconcileLaunchFailure();
        if (boot != receipt.bootId || !request || request != receipt.requestId ||
            (capture && capture != receipt.captureId) || save != receipt.saveRequestId) return false;
        output = receipt;
        return true;
    }
    bool holdImage(uint32_t boot, uint32_t capture, const uint8_t*& bytes, size_t& length,
                   bool httpRequest = false, bool* contended = nullptr) {
        if (contended) *contended = false;
        auto lock = mutexLock(httpRequest);
        if (!lock.owns_lock()) {
            if (contended) *contended = true;
            return false;
        }
        reconcileLaunchFailure();
        if (boot != receipt.bootId || !capture || capture != receipt.captureId ||
            receipt.state != captured || imageHeld.load() || latched || powerOffPending) return false;
        imageHeld.store(true);
        bytes = image.bytes;
        length = image.length;
        return true;
    }
    bool releaseImage(bool complete, bool httpRequest = false) {
        auto lock = mutexLock(httpRequest);
        if (!lock.owns_lock()) return false;
        if (complete) imageHeld.store(false);
        else latched = true; // The async request and its byte lease remain owned.
        return true;
    }
    bool startTask(void (*job)(void*), void* context) { return launch && launch(job, context); }
    void finishTask() { if (finish) finish(); }

private:
    static constexpr const char* captured = "captured";
    static constexpr const char* accepted = "accepted";
    CameraBackend& backend;
    Launch launch;
    void (*finish)();
    std::mutex mutex;
    std::atomic<bool> busy{false}, imageHeld{false}, cancelled{false}, launchFailed{false};
    bool saving = false, latched = false, powerOffPending = false;
    uint32_t nextCapture = 0;
    std::atomic<uint32_t> stopEpoch{0};
    CameraReceipt receipt;
    CameraImage image;

    std::unique_lock<std::mutex> mutexLock(bool httpRequest) {
#if defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC)
        if (httpRequest) return std::unique_lock<std::mutex>(mutex, std::try_to_lock);
#else
        (void)httpRequest;
#endif
        return std::unique_lock<std::mutex>(mutex);
    }
    CameraAdmission reject(int code, const char* error) { return {code, error, receipt}; }
    void reconcileLaunchFailure() {
        if (!launchFailed.exchange(false, std::memory_order_acq_rel)) return;
        busy.store(false);
        if (saving) { receipt.saveState = "failed"; receipt.saveError = "sd_write_failed"; }
        else { receipt.state = "failed"; receipt.error = "camera_unavailable"; }
    }
    CameraAdmission startAdmission(const CameraAdmission& response, bool httpRequest) {
        if (startTask(worker, this)) return response;
        launchFailed.store(true, std::memory_order_release);
        auto lock = mutexLock(httpRequest);
        if (!lock.owns_lock()) return {503, "camera_busy", {}};
        reconcileLaunchFailure();
        return reject(503, "camera_task_unavailable");
    }
    static void worker(void* pointer) {
        auto& self = *static_cast<CameraProducer*>(pointer);
        if (self.saving) {
            const CameraSave result = self.backend.save(self.receipt.bootId, self.receipt.captureId,
                self.image.bytes, self.image.length, self.cancelled);
            std::lock_guard<std::mutex> lock(self.mutex);
            self.latched = self.latched || result.uncertain;
            self.receipt.saveState = result.error ? "failed" : "saved";
            self.receipt.saveError = result.error;
            self.busy.store(false);
        } else {
            CameraImage result = self.backend.capture(self.cancelled);
            std::lock_guard<std::mutex> lock(self.mutex);
            self.latched = self.latched || result.uncertain;
            if (!result.error && (!result.bytes || result.length < 4 ||
                    result.length >= kCameraJpegCapacity || result.bytes[0] != 0xFF ||
                    result.bytes[1] != 0xD8 || result.bytes[result.length - 2] != 0xFF ||
                    result.bytes[result.length - 1] != 0xD9))
                result.error = result.length >= kCameraJpegCapacity ? "jpeg_capacity" : "jpeg_invalid";
            if (self.cancelled.load()) result.error = "capture_failed";
            if (result.error) {
                free(result.bytes);
                self.receipt.state = "failed";
                self.receipt.error = result.error;
            } else {
                self.image = result;
                self.receipt.state = captured;
                self.receipt.captureId = ++self.nextCapture;
                self.receipt.byteLength = static_cast<uint32_t>(result.length);
            }
            self.busy.store(false);
        }
        self.finishTask();
    }
};
} // namespace lab

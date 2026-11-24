#pragma once

#include <mutex>
#include <atomic>
#include "LabMenu.h"
#include "LabNotification.h"
#include "LabRuntime.h"
#include "LabManualSession.h"
#include "BspMotionTest.h"
#include "LabAudioTest.h"
#include "LabImageDisplay.h"
#include "LabTemporaryText.h"
#include "LabRgbControl.h"

namespace lab {

// One pending remote UI request; HTTP only records it and loop() runs it like a touch.
struct UiRequest {
    enum class Kind : uint8_t { None, Screen, Touch } kind = Kind::None;
    Page page = Page::Face;
    uint8_t element = 0;
    NotificationIdentity notification{};
};

struct ManualActionReceipt {
    uint32_t bootId = 0, revision = 0, commandId = 0;
    const char* action = nullptr;
    const char* outcome = nullptr;
};

struct ImuVector {
    bool valid = false;
    float x = 0, y = 0, z = 0;
    uint32_t at = 0;
};

struct SensorDiagnostics {
    bool published = false;
    bool topInitialized = false, topAvailable = false, topValid = false;
    uint8_t front = 0, middle = 0, back = 0;
    uint32_t topAt = 0;
    const char* topError = "not_started";
    bool lightInitialized = false, lightAvailable = false, lightValid = false;
    uint16_t proximity = 0, light0 = 0, light1 = 0;
    uint32_t lightAt = 0;
    const char* lightError = "not_started";
    bool imuAvailable = false;
    const char* imuError = "not_started";
    ImuVector acceleration, gyroscope;
    bool rgbActive = false;
    uint32_t rgbDurationMs = 0;
    const char* rgbError = "not_started";
    bool pettingEnabled = false, soundEnabled = false, quiet = false, eligible = false;
    const char* ineligibleReason = "unknown";
    bool settled = false, released = false;
    uint8_t touchCount = 0, releaseCount = 0, stage = 0;
    bool pettingActive = false;
    const char* soundState = "not_requested";
};

// HTTP only admits work or requests Stop. All hardware calls run in loop().
class Device {
#ifdef NETBOT_TEST_BUILD
    friend struct DiagnosticDeviceTestAccess;
#endif
public:
    explicit Device(Audio& audio, MotionController* motion = nullptr,
                    ManualSessionControl* manual = nullptr, ManualStopLatch* manualStop = nullptr,
                    bool manualPermitted = false, bool manualExclusive = false)
        : runtime(1, audio, motion), manual(manual), manualStop(manualStop),
          manualPermitted(manualPermitted && manual && manualStop),
          manualExclusive(manualExclusive && manual && manualStop) {}
    std::mutex mutex;
    Runtime runtime;
    Notification notification;
    bool stopRequested = false;
    std::atomic<bool> bspYawStop{false};
    std::atomic<bool> bspYawUnverified{false};
    using MotionTestAction = BspMotionTest::Action;
    static MotionTestAction localMotionTestAction(BspMotionTest::Axis axis, int direction) {
        return BspMotionTest::localMotionTestAction(axis, direction);
    }
    BspMotionTest* motionTest = nullptr;
    AudioTest* audioTest = nullptr;
    ImageDisplay* imageDisplay = nullptr;
    TemporaryText* temporaryText = nullptr;
    RgbController* rgbController = nullptr;
    RgbEffect rgbEffect;
    bool imageUiReady = false;
    MotionTestAction pendingMotionTest = MotionTestAction::None;
    bool motionTestReady = false;
    StopReceipt stopReceipt{};
    uint32_t remoteStopGeneration = 0, nextStopId = 0, stopBoot = 0;
    bool remoteStopStarted = false;
    Snapshot stopped{};
    Page uiPage = Page::Face;  // published by loop() for read-only UI endpoints
    bool uiFaceStopVisible = true;
    bool uiPettingEnabled = true, uiPettingSoundEnabled = true;
    SensorDiagnostics sensors{};
    NotificationIdentity uiNotification{};
    UiRequest ui{};
    bool uiInProgress = false;

#if defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC)
    using RailCut = void (*)(void*);
    using RailRead = uint32_t (*)(void*);
    void initializeDiagnosticStop(uint32_t boot, void* context, RailCut cut, RailRead read) {
        diagnosticContext = context;
        diagnosticCut = cut;
        diagnosticRead = read;
        manualBoot.store(boot, std::memory_order_release);
        diagnosticBoot.store(boot, std::memory_order_release);
    }
    Admission requestDiagnosticStop(uint32_t boot) {
        if (!boot || boot != diagnosticBoot.load(std::memory_order_acquire) ||
            !manualStop || !diagnosticCut || !diagnosticRead)
            return {409, "boot_changed", false};
        uint8_t expected = 0;
        const bool first = diagnosticRequested.compare_exchange_strong(expected, 1);
        if (first) {
            bspYawStop.store(true, std::memory_order_release);
            manualStop->request();
            diagnosticRequested.store(2, std::memory_order_release);
            diagnosticCut(diagnosticContext);
        }
        return {202, first ? "accepted" : "existing", first};
    }
    bool diagnosticStopRequested() const { return diagnosticRequested.load(std::memory_order_acquire) == 2; }
    uint32_t diagnosticStopBoot() const { return diagnosticBoot.load(std::memory_order_acquire); }
    uint32_t diagnosticRailResult() const { return diagnosticRead(diagnosticContext); }
    uint8_t diagnosticTorqueResult() const { return diagnosticTorque.load(std::memory_order_acquire); }
#endif
    void tick(uint32_t now) {
        ManualCommand work;
        bool runManual = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
#if defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC)
            reconcileDiagnosticStop();
#endif
            reconcileStop();
            if (manualReserved) {
                if (runtime.snapshot().bootId != manualBoot.load()) {
                    manualStop->request();
                    pendingManual = {};
                    if (manualReceipt.outcome && !strcmp(manualReceipt.outcome, "pending"))
                        manualReceipt.outcome = "uncertain";
                }
                work = pendingManual;
                pendingManual = {};
                if (work.operation != ManualCommand::None) manualWorkInFlight = true;
                runManual = true;
            } else {
                if (stopRequested) {
                    runtime.cancelSample(now);
                    if (!remoteStopStarted && runtime.stopGeneration() == remoteStopGeneration) {
                        runtime.emergencyStop(now);
                        remoteStopGeneration = runtime.stopGeneration();
                        remoteStopStarted = true;
                    }
                }
                runtime.tick(now);
                reconcileStop();
                if (!stopRequested) runtime.tickSample(now);
            }
        }
        if (!runManual) return;
        bool accepted = true;
        switch (work.operation) {
            case ManualCommand::Prepare: accepted = manual->prepare(work.bounds, work.travelMs); break;
            case ManualCommand::Power: accepted = manual->requestPower(); break;
            case ManualCommand::FeedbackPower: accepted = manual->requestFeedbackPower(work.expected); break;
            case ManualCommand::Enable: accepted = manual->requestEnable(); break;
            case ManualCommand::Move: accepted = manual->requestMove(work.delta); break;
            case ManualCommand::Return: accepted = manual->requestReturn(); break;
            case ManualCommand::Close: accepted = manual->requestClose(work.supported); break;
            default: break;
        }
        manual->tick();
        std::lock_guard<std::mutex> lock(mutex);
        manualState = manual->state();
        manualFeedback = manual->feedback();
#if defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC)
        if (diagnosticStopRequested()) {
            const auto outcome = manual->stopState();
            if (outcome == ManualStopState::Failed || manualState == ManualSessionState::Fault)
                diagnosticTorque.store(2, std::memory_order_release);
            else if (outcome == ManualStopState::Confirmed)
                diagnosticTorque.store(1, std::memory_order_release);
        }
#endif
        if (!accepted) manualCommandError = "manual_action_rejected";
        else if (manualState == ManualSessionState::Fault || manualState == ManualSessionState::Stopped)
            manualCommandError = manual->error();
        if (manualReceipt.outcome && !strcmp(manualReceipt.outcome, "pending")) {
            const bool dispatched = work.operation != ManualCommand::None &&
                work.operation != ManualCommand::Prepare;
            if (manualStop->requested() || runtime.snapshot().bootId != manualReceipt.bootId ||
                manualState == ManualSessionState::Fault) manualReceipt.outcome = "uncertain";
            else if (dispatched && !accepted) manualReceipt.outcome = "failed";
            else {
                if (dispatched) manualReceiptStarted = true;
                if (manualReceiptStarted && manualActionCompleted()) manualReceipt.outcome = "completed";
            }
        }
        if (manualState == ManualSessionState::Fault) runtime.invalidateSingleAxisPreparation();
        if (manualState == ManualSessionState::Prepared || manualState == ManualSessionState::Powered ||
            manualState == ManualSessionState::Ready || manualState == ManualSessionState::Stopped ||
            manualState == ManualSessionState::Closed || manualState == ManualSessionState::Fault)
            manualWorkInFlight = false;
        if (stopRequested) {
            const auto outcome = manual->stopState();
            if (outcome == ManualStopState::Confirmed || outcome == ManualStopState::Failed ||
                outcome == ManualStopState::CancelledWithoutEffect ||
                (!accepted && manualState == ManualSessionState::Idle) ||
                (manualState == ManualSessionState::Fault && outcome == ManualStopState::Idle)) {
                stopReceipt.outcome = "failed";
                stopped = runtime.snapshot();
                stopped.motion.stopResult = stopReceipt.outcome;
                stopped.stopReceipt = stopReceipt;
                stopRequested = false;
            }
        }
    }
    Admission requestStop(uint32_t bootId, StopReceipt* identity = nullptr) {
        if (manualExclusive && !manualBoot.load()) {
            std::lock_guard<std::mutex> lock(mutex);
            if (bootId && bootId == runtime.snapshot().bootId && !manualBoot.load())
                manualBoot.store(bootId);
        }
        const uint32_t token = manualStop && manualBoot.load() == bootId && bootId ?
            manualStop->request() : 0;
        std::lock_guard<std::mutex> lock(mutex);
        reconcileStop();
        if (bootId != runtime.snapshot().bootId) return {409, "boot_changed", false};
        bspYawStop.store(true, std::memory_order_release);
        if (pendingMotionTest != MotionTestAction::None && motionTest) {
            pendingMotionTest = MotionTestAction::None;
            motionTest->cancelPending();
        }
        if (manualExclusive) manualReserved = true;
        if (manualReserved) runtime.invalidateSingleAxisPreparation();
        if (manualReserved) pendingManual = {};
        if (manualReserved && manualReceipt.outcome && !strcmp(manualReceipt.outcome, "pending"))
            manualReceipt.outcome = "uncertain";
        if (manualReserved && stopReceipt.stopId && stopReceipt.bootId == bootId) {
            if (stopRequested && token) activeStopToken = token;
            if (identity) *identity = stopReceipt;
            return {202, "existing", false};
        }
        if (stopRequested && stopReceipt.bootId == bootId &&
            runtime.stopGeneration() == remoteStopGeneration) {
            if (identity) *identity = stopReceipt;
            return {202, "pending", false};
        }
        if (stopBoot != bootId) {
            stopBoot = bootId;
            nextStopId = 0;
        }
        if (nextStopId == UINT32_MAX) return {409, "stop_ids_exhausted", false};
        stopReceipt = {bootId, ++nextStopId, "pending"};
        activeStopToken = token;
        stopRequested = true;
        remoteStopStarted = false;
        remoteStopGeneration = runtime.stopGeneration();
        stopped = runtime.snapshot();
        stopped.stopReceipt = stopReceipt;
        if (identity) *identity = stopReceipt;
        return {202, "accepted", true};
    }
    Snapshot stopStatus() {
        std::lock_guard<std::mutex> lock(mutex);
#if defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC)
        reconcileDiagnosticStop();
#endif
        reconcileStop();
        if (stopReceipt.outcome) return stopped;
        return runtime.snapshot();
    }
    Admission admit(const Command& command, Snapshot* receipt = nullptr) {
        std::lock_guard<std::mutex> lock(mutex);
        reconcileStop();
        if (manualReserved || manualExclusive) return {409, "manual_session_active", false};
        if (stopRequested) return {409, "stopping", false};
        const Admission result = runtime.admit(command);
        if (receipt) {
            *receipt = runtime.snapshot();
            receipt->stopReceipt = stopReceipt.bootId == receipt->bootId ? stopReceipt : StopReceipt{};
        }
        return result;
    }
    Snapshot snapshot() {
        std::lock_guard<std::mutex> lock(mutex);
        reconcileStop();
        auto value = runtime.snapshot();
        value.stopReceipt = stopReceipt.bootId == value.bootId ? stopReceipt : StopReceipt{};
        if (manualReserved)
            value.motion.stopResult = value.stopReceipt.outcome ? value.stopReceipt.outcome : "not_requested";
        return value;
    }
#if defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC)
    bool trySnapshot(Snapshot& value) {
        std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
        if (!lock.owns_lock()) return false;
        reconcileDiagnosticStop();
        reconcileStop();
        value = runtime.snapshot();
        value.stopReceipt = stopReceipt.bootId == value.bootId ? stopReceipt : StopReceipt{};
        if (manualReserved)
            value.motion.stopResult = value.stopReceipt.outcome ? value.stopReceipt.outcome : "not_requested";
        return true;
    }
    bool tryAdmit(const Command& command, Snapshot& receipt, Admission& result) {
        std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
        if (!lock.owns_lock()) return false;
        reconcileDiagnosticStop();
        reconcileStop();
        if (manualReserved || manualExclusive) result = {409, "manual_session_active", false};
        else if (stopRequested) result = {409, "stopping", false};
        else result = runtime.admit(command);
        receipt = runtime.snapshot();
        receipt.stopReceipt = stopReceipt.bootId == receipt.bootId ? stopReceipt : StopReceipt{};
        return true;
    }
#endif
    // Called with mutex held after the ordinary configuration admission.
    bool queueManualPreparation(uint32_t bootId, const SingleAxisTestBounds& bounds, uint32_t travelMs) {
        if (!canQueueManualPreparation(bounds.axis) || !acceptsManualTrial(bounds)) return false;
        manualBoot.store(bootId);
        manualRevision = runtime.snapshot().configRevision;
        manualReserved = true;
        manualAxis = bounds.axis;
        manualReceipt = {};
        manualReceiptStarted = false;
        manualCommandError = nullptr;
        stopReceipt = {};
        pendingManual.operation = ManualCommand::Prepare;
        pendingManual.bounds = bounds;
        pendingManual.travelMs = travelMs;
        return true;
    }
    bool manualActive() const { return manualReserved || manualExclusive; } // mutex held
    bool manualActiveSafe() { std::lock_guard<std::mutex> lock(mutex); return manualActive(); }
    bool manualAllowed() const { return manualPermitted; }
    ManualSessionState manualSnapshot() const { return manualState; } // mutex held
    const char* manualError() const { return manualCommandError; } // mutex held
    ManualFeedback manualFeedbackSnapshot() const { return manualFeedback; }
    bool manualSessionCurrent() const {
        return !manualReserved || (manualBoot.load() && manualBoot.load() == runtime.snapshot().bootId);
    }
    uint32_t manualSessionRevision() const { return manualRevision; } // mutex held
    uint32_t manualLastCommandId() const { return manualSessionCurrent() ? lastManualId : 0; }
    ManualActionReceipt manualActionReceipt() const {
        return manualSessionCurrent() ? manualReceipt : ManualActionReceipt{};
    }
    bool canQueueManualPreparation(SingleAxis axis) const {
        return manualPermitted && !manualStop->requested() && !stopRequested && !manualWorkInFlight &&
            pendingManual.operation == ManualCommand::None &&
            (!manualReserved || (manualState == ManualSessionState::Closed && axis != manualAxis) ||
                (manualState == ManualSessionState::Powered && manualFeedback.feedbackOnly &&
                    manualFeedback.stationary && axis == SingleAxis::Yaw));
    }
    bool acceptsManualTrial(const SingleAxisTestBounds& value) const {
        return !manualFeedback.feedbackOnly ||
            (manualFeedback.stationary && localYawTrial(value, manualFeedback.position, manualExpected));
    }
    const char* queueManualAction(uint32_t bootId, uint32_t commandId, const char* action, int delta,
                                  bool supported = false, Pose expected = {}) {
        const bool feedbackPower = !strcmp(action, "manual-feedback-power");
        if (!manualPermitted || (!manualReserved && !feedbackPower)) return "manual_unavailable";
        if (!bootId || bootId != (manualReserved ? manualBoot.load() : runtime.snapshot().bootId))
            return "boot_changed";
        if (stopRequested || manualStop->requested()) return "stopping";
        if (!commandId || commandId <= lastManualId) return "manual_command_stale";
        if (pendingManual.operation != ManualCommand::None || manualWorkInFlight) return "manual_busy";
        ManualCommand::Operation operation = ManualCommand::None;
        if (feedbackPower && !manualReserved && manualState == ManualSessionState::Idle && supported) {
            MotionConfigurationSnapshot config;
            runtime.configurationSnapshot(config);
            if (config.configured || config.busStarted || !config.supported) return "manual_unavailable";
            manualReserved = true;
            manualBoot.store(bootId);
            manualRevision = config.revision;
            manualExpected = expected;
            operation = ManualCommand::FeedbackPower;
        } else if (!strcmp(action, "manual-power") && manualState == ManualSessionState::Prepared)
            operation = ManualCommand::Power;
        else if (!strcmp(action, "manual-enable") && manualState == ManualSessionState::Powered &&
            !manualFeedback.feedbackOnly)
            operation = ManualCommand::Enable;
        else if (!strcmp(action, "manual-move") && manualState == ManualSessionState::Ready)
            operation = ManualCommand::Move;
        else if (!strcmp(action, "manual-return") && manualState == ManualSessionState::Ready)
            operation = ManualCommand::Return;
        else if (!strcmp(action, "manual-close") && supported &&
            manualState == ManualSessionState::Ready)
            operation = ManualCommand::Close;
        if (operation == ManualCommand::None) return "manual_action_unavailable";
        if (operation == ManualCommand::Close) stopReceipt = {};
        lastManualId = commandId;
        pendingManual.operation = operation;
        pendingManual.delta = delta;
        pendingManual.supported = supported;
        pendingManual.expected = expected;
        manualReceipt = {bootId, manualRevision, commandId, manualActionName(operation), "pending"};
        manualReceiptStarted = false;
        manualCommandError = nullptr;
        return nullptr;
    }
private:
#if defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC)
    std::atomic<uint32_t> diagnosticBoot{0};
    void* diagnosticContext = nullptr;
    RailCut diagnosticCut = nullptr;
    RailRead diagnosticRead = nullptr;
    std::atomic<uint8_t> diagnosticRequested{0};
    std::atomic<uint8_t> diagnosticTorque{0};
    bool diagnosticReconciled = false;
    void reconcileDiagnosticStop() {
        if (!diagnosticStopRequested() || diagnosticReconciled) return;
        diagnosticReconciled = true;
        manualReserved = true;
        runtime.invalidateSingleAxisPreparation();
        pendingManual = {};
        if (manualReceipt.outcome && !strcmp(manualReceipt.outcome, "pending"))
            manualReceipt.outcome = "uncertain";
        stopReceipt = {diagnosticStopBoot(), 1, "pending"};
        stopRequested = true;
        remoteStopStarted = false;
        stopped = runtime.snapshot();
        stopped.stopReceipt = stopReceipt;
    }
#endif
    struct ManualCommand {
        enum Operation { None, Prepare, Power, FeedbackPower, Enable, Move, Return, Close } operation = None;
        SingleAxisTestBounds bounds{};
        uint32_t travelMs = 0;
        int delta = 0;
        bool supported = false;
        Pose expected{};
    };
    ManualSessionControl* manual;
    ManualStopLatch* manualStop;
    const bool manualPermitted;
    const bool manualExclusive;
    std::atomic<uint32_t> manualBoot{0};
    ManualCommand pendingManual{};
    ManualSessionState manualState = ManualSessionState::Idle;
    ManualFeedback manualFeedback{};
    Pose manualExpected{};
    const char* manualCommandError = nullptr;
    uint32_t lastManualId = 0, activeStopToken = 0, manualRevision = 0;
    ManualActionReceipt manualReceipt{};
    bool manualReceiptStarted = false;
    SingleAxis manualAxis = SingleAxis::Yaw;
    bool manualReserved = false, manualWorkInFlight = false;
    static const char* manualActionName(ManualCommand::Operation operation) {
        switch (operation) {
            case ManualCommand::Power: return "manual-power";
            case ManualCommand::FeedbackPower: return "manual-feedback-power";
            case ManualCommand::Enable: return "manual-enable";
            case ManualCommand::Move: return "manual-move";
            case ManualCommand::Return: return "manual-return";
            case ManualCommand::Close: return "manual-close";
            default: return nullptr;
        }
    }
    bool manualActionCompleted() const {
        if (!manualReceipt.action) return false;
        if (!strcmp(manualReceipt.action, "manual-power") ||
            !strcmp(manualReceipt.action, "manual-feedback-power")) return manualState == ManualSessionState::Powered;
        if (!strcmp(manualReceipt.action, "manual-close")) return manualState == ManualSessionState::Closed;
        return manualState == ManualSessionState::Ready;
    }
    void reconcileStop() {
        if (manualReserved) return;
        const auto current = runtime.snapshot();
        if ((stopReceipt.stopId && stopReceipt.bootId != current.bootId) ||
            (stopRequested && (!stopReceipt.stopId || !stopReceipt.outcome ||
                strcmp(stopReceipt.outcome, "pending") || runtime.stopGeneration() != remoteStopGeneration))) {
            stopReceipt = {};
            stopRequested = false;
        }
        if (!stopRequested || !remoteStopStarted) return;
        const bool experimental = bspYawUnverified.load(std::memory_order_acquire);
        const char* result = experimental ? "failed" : current.motion.stopResult;
        if (!result || (strcmp(result, "pending") && strcmp(result, "confirmed") && strcmp(result, "failed"))) {
            stopReceipt = {};
            stopRequested = false;
            return;
        }
        stopReceipt.outcome = result;
        stopped = current;
        if (experimental) {
            stopped.motion.stopResult = "failed";
            stopped.motion.error = stopped.error = "experimental_bsp_stop_unverified";
        }
        stopped.stopReceipt = stopReceipt;
        stopRequested = !strcmp(result, "pending");
    }
};

}  // namespace lab

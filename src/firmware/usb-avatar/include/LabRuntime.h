// Goal: Coordinate the robot's firmware behavior in one runtime.
// Responsibilities: Track command, avatar, motion, audio, and notification state.
// How it works:
// 1. Requests are admitted against the command and device state.
// 2. Runtime updates advance actions and reactions over time.
// 3. Snapshots expose the resulting state to display and API callers.
#pragma once

#include "AvatarState.h"
#include "LabContract.h"
#include "LabMotion.h"
#include "LabNotification.h"
#include "IdleRecipes.h"
#include "LabAudioTest.h"

namespace lab {

enum class Indicator { None, Warning, Important };
enum class PettingAnimation : uint8_t { Current, PetA, PetB, Grumpy };

struct PettingAnimationState {
    bool active;
    PettingAnimation animation;
    uint32_t startedAt;
    uint8_t sequenceStage = 0;
};

inline PettingAnimation pettingAnimationFromRandom(uint32_t value) {
    switch (value % 3U) {
        case 1: return PettingAnimation::PetA;
        case 2: return PettingAnimation::PetB;
        default: return PettingAnimation::Current;
    }
}

class Audio {
public:
    virtual ~Audio() = default;
    virtual bool available() const = 0;
    virtual bool play(uint32_t durationMs) = 0;
    // Unsupported tone backends reject admission, rather than substituting a sound.
    virtual bool playTone(uint32_t, uint32_t) { return false; }
    virtual void stop() = 0;
};

inline const char* actionName(Action action) {
    switch (action) {
        case Action::Neutral: return "neutral";
        case Action::Wink: return "wink";
        case Action::Smile: return "smile";
        case Action::LookUp: return "look_up";
        case Action::Sleep: return "sleep";
        case Action::Wake: return "wake";
        case Action::Warning: return "warning";
        case Action::Important: return "important";
        case Action::Sound: return "sound";
        case Action::Nod: return "nod";
        case Action::ImportantNotification: return "important_notification";
        case Action::MoveLeft: return "move_left";
        case Action::MoveRight: return "move_right";
        case Action::MoveUp: return "move_up";
        case Action::MoveDown: return "move_down";
        case Action::Home: return "home";
        case Action::ImportantMessage: return "important_message";
        case Action::Arm: return "arm";
        case Action::Lock: return "lock";
        case Action::ReleaseTorque: return "release_torque";
        default: return "invalid";
    }
}

struct StopReceipt {
    uint32_t bootId = 0, stopId = 0;
    const char* outcome = nullptr;
};

struct Snapshot {
    uint32_t bootId;
    Command command;
    CommandState state;
    const char* error;
    bool sound;
    MotionSnapshot motion{MotionState::Blocked, false, {}, "motion_limits_unreviewed"};
    bool quiet = false;
    uint32_t configRevision = 0;
    StopReceipt stopReceipt{};
};

struct IdleFrame {
    bool active = false;
    uint8_t motionIndex = 0;
    IdleEyes eyes = IdleEyes::Neutral;
};

class Runtime {
public:
    Runtime(uint32_t boot, Audio& audio, MotionController* controller = nullptr)
        : ledger(boot), audio(audio), motion(controller ? *controller : blockedMotion) {
        avatar.automatic = false;
        updateIdleFrame();
    }

    void setPettingAudio(AudioTest* test) { pettingAudio = test; }

    void resetBoot(uint32_t boot, uint32_t now) {
        cancelPettingAudio(now);
        cancelNotificationSound();
        if (startupSoundAttempted) wifiCueSeen = true;
        cancelCues();
        if (boot != ledger.bootId()) motion.resetConfigurationForBoot();
        audio.stop();
        ledger = CommandLedger(boot);
        avatar.reset(now);
        avatar.automatic = false;
        idleScheduler.reset(now);
        started = false;
        pettingCommand = false;
        pettingActive = false;
        pettingAnimation = PettingAnimation::Current;
        pettingStartedAt = now;
        pettingCue = NotificationCue::NotRequested;
        resetPettingSequence();
        stopping = false;
        stopAttempt = 0;
        pendingFailure = nullptr;
        indicator = Indicator::None;
        logHead = logCount = 0;
        droppedLogs = 0;
        clock = now;
        record("boot");
        updateIdleFrame();
    }

    const char* startupSound(uint32_t now) {
        if (startupSoundAttempted) return startupSoundError;
        startupSoundAttempted = true;
        clock = now;
        if (quiet) {
            record("startup_sound_skipped");
            return nullptr;
        }
        if (!audio.available()) startupSoundError = "startup_sound_unavailable";
        else if (!audio.play(100)) startupSoundError = "startup_sound_failed";
        else { startupSoundPlayed = true; startupSoundAt = now; }
        record(startupSoundError ? "failure" : "startup_sound", startupSoundError);
        return startupSoundError;
    }

    void wifiReady(uint32_t now) {
        if (wifiCueSeen) return;
        wifiCueSeen = true;
        clock = now;
        if (quiet || stopping || ledger.busy()) record("wifi_sound_skipped");
        else wifiNotes = 2;
    }

    void homeReady() {
        if (!quiet && !stopping && !ledger.busy()) homeNotes = 1;
    }

    void homeCaptured() {
        if (!quiet && !stopping && !ledger.busy() && !captureNotes) captureNotes = 2;
    }

    const char* wifiSoundError() const { return wifiCueError; }

    void cancelNotificationSound() {
        pettingSecond = false;
        const bool active = notificationSoundActive;
        notificationSoundActive = false;
        if (active) audio.stop();
    }

    NotificationCue notificationSound(uint32_t now) {
        if (quiet) return NotificationCue::Quiet;
        if (stopping || ledger.busy() || notificationSoundActive || cueBusy(now) ||
            (pettingAudio && pettingAudio->active()))
            return NotificationCue::Busy;
        if (!audio.available()) return NotificationCue::Unavailable;
        if (!audio.play(120)) {
            audio.stop();
            return NotificationCue::Failed;
        }
        notificationSoundActive = true;
        notificationSoundAt = now;
        return NotificationCue::Requested;
    }

    bool supports(Action action, bool local = false) const {
        if (action == Action::Invalid) return false;
        if (action == Action::Arm) return motion.armAvailable();
        if (action == Action::ReleaseTorque) return motion.releaseTorqueAvailable();
        if (action == Action::Lock) return motion.checkedConfiguration() &&
            (motion.snapshot().state == MotionState::Ready || motion.snapshot().state == MotionState::Disarmed);
        if (motion.remoteYawBench() && requiresMotion(action) &&
            action != Action::MoveLeft && action != Action::MoveRight) return false;
        if (requiresMotion(action) && motion.benchOnly() &&
            (!local || (action != Action::MoveLeft && action != Action::MoveRight))) return false;
        if (requiresMotion(action) && action != Action::ReleaseTorque && !motion.available()) return false;
        if (action == Action::ImportantMessage && (quiet || !audio.available())) return false;
        return action != Action::Sound || audio.available();
    }

    Admission admit(const Command& command, bool local = false) {
        const Admission result = admitChecked(command, local);
        if (result.execute) {
            cancelNotificationSound();
            cancelCues();
        }
        record(result.execute ? "received" : result.status >= 400 ? "rejected" : "duplicate",
            result.code, &command);
        return result;
    }

private:
    Admission admitChecked(const Command& command, bool local) {
        if (stopping) return {409, "stopping", false};
        if (motion.observationPending() && requiresMotion(command.action))
            return {409, "observation_busy", false};
        // Check duplicates before readiness, including after a terminal Stop or fault.
        if (command.bootId == ledger.bootId() && command.commandId > ledger.command().commandId &&
            command.durationMs >= kMinimumDurationMs && command.durationMs <= kMaximumDurationMs) {
            if (motion.checkedConfiguration() && requiresMotion(command.action) && command.action != Action::Lock) {
                if (local && motion.twoAxisTest() && command.action == Action::Arm && motion.configurationRevision())
                    return {409, "config_companion_enable_required", false};
                if ((motion.configurationRevision() && !command.hasConfigRevision) ||
                    (command.hasConfigRevision && command.configRevision != motion.configurationRevision()))
                    return {409, "config_revision_changed", false};
            }
            if (command.action == Action::Lock) {
                if (!motion.checkedConfiguration()) return {409, "remote_lock_blocked", false};
            } else if (command.action == Action::Arm) {
                if (!motion.armAvailable()) return {409, "remote_arm_blocked", false};
                if (motion.snapshot().state != MotionState::Disarmed) return {409, "motion_not_disarmed", false};
                if (command.durationMs < motion.minimumDuration()) return {400, "motion_duration_too_short", false};
            } else if (command.action == Action::ReleaseTorque) {
                if (!motion.releaseTorqueAvailable()) return {409, "torque_release_unavailable", false};
            } else if (motion.remoteYawBench() && requiresMotion(command.action) &&
                command.action != Action::MoveLeft && command.action != Action::MoveRight) {
                return {409, "bench_yaw_only", false};
            } else if (requiresMotion(command.action) && motion.benchOnly() &&
                (!local || (command.action != Action::MoveLeft && command.action != Action::MoveRight)))
                return {409, local ? "bench_yaw_only" : "bench_local_only", false};
            if (command.action != Action::Arm && command.action != Action::Lock &&
                command.action != Action::ReleaseTorque &&
                requiresMotion(command.action) && !motion.available())
                return {409, motion.unavailableReason(), false};
            if (command.action != Action::Arm && command.action != Action::ReleaseTorque &&
                requiresMotion(command.action) &&
                command.durationMs < motion.minimumDuration())
                return {400, "motion_duration_too_short", false};
            if (command.action == Action::ImportantMessage && (quiet || !audio.available()))
                return {409, quiet ? "routine_quiet" : "sound_unavailable", false};
        }
        return ledger.admit(command, supports(command.action, local));
    }

public:
    bool armMotion(uint32_t now) {
        motion.debugContext(now, 0);
        const bool armed = !(motion.twoAxisTest() && motion.configurationRevision()) &&
            !ledger.busy() && !stopping && motion.arm(now);
        if (armed) cancelCues();
        return armed;
    }
    bool disarmMotion(uint32_t now) { return !ledger.busy() && !stopping && motion.disarm(now); }
    void debugSnapshot(MotionDebugSnapshot& result) const { motion.debugSnapshot(result); }
    const char* requestMotionObservation(uint32_t bootId, uint32_t id, uint32_t now) {
        if (stopping) return "stopping";
        if (ledger.busy()) return "command_busy";
        return motion.requestObservation(bootId, id, now);
    }
    void configurationSnapshot(MotionConfigurationSnapshot& result) const {
        motion.configurationSnapshot(result);
        if (ledger.busy()) result.applyReason = "command_busy";
        else if (stopping) result.applyReason = "stopping";
    }
    const char* configurationConflict(uint32_t boot, uint32_t revision) const {
        if (boot != ledger.bootId()) return "boot_changed";
        if (revision != motion.configurationRevision()) return "config_revision_changed";
        return nullptr;
    }
    const char* validateConfiguration(const MotionConfiguration& value) const {
        return motion.validateConfiguration(value);
    }
    const char* canApplyConfiguration(const MotionConfiguration& value) const {
        if (ledger.busy()) return "command_busy";
        if (stopping) return "stopping";
        return motion.canApplyConfiguration(value);
    }
    bool applyConfiguration(const MotionConfiguration& value, bool reset) {
        return !canApplyConfiguration(value) && motion.applyConfiguration(value, reset);
    }
    const char* prepareSingleAxis(uint32_t boot, uint32_t revision,
                                  const SingleAxisTestBounds& value, uint32_t travelMs) {
        if (const char* conflict = configurationConflict(boot, revision)) return conflict;
        if (ledger.busy()) return "command_busy";
        if (stopping) return "stopping";
        motion.invalidateSingleAxisPreparation();
        if (const char* error = motion.canPrepareSingleAxis(value, travelMs)) return error;
        return motion.prepareSingleAxis(value, travelMs) ? nullptr : "single_axis_not_inactive";
    }
    void invalidateSingleAxisPreparation() { motion.invalidateSingleAxisPreparation(); }
    const char* requestSample(uint32_t boot, uint32_t id, uint8_t servo, uint32_t now) {
        if (boot != ledger.bootId()) return "boot_changed";
        if (ledger.busy()) return "command_busy";
        return motion.requestSample(id, servo, now);
    }
    void tickSample(uint32_t now) {
        if (ledger.busy()) motion.cancelSample(now, "command_busy");
        else motion.tickSample(now);
    }
    void cancelSample(uint32_t now) { motion.cancelSample(now); }
    bool popLog(RuntimeLogEvent& event) {
        if (!logCount) return false;
        event = logs[(logHead + kLogCapacity - logCount) % kLogCapacity];
        --logCount;
        return true;
    }
    uint32_t lostLogs() const { return droppedLogs; }

    Admission local(Action action, uint32_t duration = 1000) {
        if (ledger.command().commandId == UINT32_MAX) return {409, "ids_exhausted", false};
        return admit({ledger.bootId(), ledger.command().commandId + 1, action, duration,
            motion.configurationRevision(), motion.configurationRevision() != 0}, true);
    }

    Admission pet(uint32_t now, bool sound,
                  PettingAnimation animation = PettingAnimation::Current) {
        if (stopping || ledger.busy() || notificationSoundActive || cueBusy(now))
            return {409, "petting_busy", false};
        const auto result = local(Action::Smile, animation == PettingAnimation::Grumpy ? 2000 : 1000);
        if (result.execute) {
            pettingCommand = true;
            pettingActive = true;
            pettingSecond = false;
            pettingEnthusiastic = false;
            pettingAnimation = animation;
            pettingSequenceStage = 0;
            pettingStartedAt = now;
            pettingCue = !sound ? NotificationCue::NotRequested :
                quiet ? NotificationCue::Quiet :
                !audio.available() ? NotificationCue::Unavailable : NotificationCue::Requested;
        }
        return result;
    }

    Admission petSequence(uint32_t now, bool sound, uint32_t randomValue = 0) {
        if (pettingCooldown) {
            if (pettingActive || now - pettingCooldownAt < 500) return {409, "petting_cooldown", false};
            pettingCooldown = false;
        }
        const uint8_t stage = pettingCount && now - pettingWindowAt < 15000 ?
            static_cast<uint8_t>(pettingCount + 1) : 1;
        const auto animation = stage == 1 ? pettingAnimationFromRandom(randomValue) :
            stage == 2 ? PettingAnimation::PetB : PettingAnimation::Grumpy;
        const auto result = pet(now, sound, animation);
        if (result.execute) {
            pettingSequenceStage = stage;
            pettingEnthusiastic = stage == 2;
            if (stage == 1) pettingWindowAt = now;
            if (stage == 3) {
                pettingCount = 0;
                pettingCooldown = true;
                pettingCooldownAt = now;
            } else pettingCount = stage;
        }
        return result;
    }

    void resetPettingSequence() {
        pettingCount = 0;
        pettingSequenceStage = 0;
        pettingCooldown = false;
    }

    NotificationCue pettingSound() const { return pettingCue; }
    PettingAnimationState pettingAnimationState() const {
        return {pettingActive, pettingAnimation, pettingStartedAt, pettingSequenceStage};
    }
    void cancelPetting(uint32_t now, bool soundOnly = false) {
        cancelPettingAudio(now);
        if (!pettingActive) return;
        pettingCue = NotificationCue::NotRequested;
        cancelNotificationSound();
        if (soundOnly) return;
        pettingCommand = pettingActive = false;
        avatar.cancel(now);
        ledger.cancel();
        started = false;
    }

    void tick(uint32_t now) {
        clock = now;
        tickPettingAudio(now);
        if (startupSoundPlayed && now - startupSoundAt >= 320) startupSoundPlayed = false;
        if (lastCue != Cue::None && now - lastNoteAt >= lastNoteDuration + 280U) lastCue = Cue::None;
        if (pettingActive && pettingCue == NotificationCue::Requested && !audio.available()) {
            pettingCue = NotificationCue::Unavailable;
            cancelNotificationSound();
        }
        // Petting retains the foreground audio lease across its two finite notes.
        if (notificationSoundActive && !pettingActive && now - notificationSoundAt >= 120)
            cancelNotificationSound();
        motion.debugContext(now, ledger.command().commandId);
        motion.tick(now);
        if (stopping) { stop(now, true); return; }
        if (pettingActive && motion.snapshot().state == MotionState::Fault) {
            cancelPetting(now);
            return;
        }
        if (!ledger.busy()) {
            tickCues(now);
            avatar.update(now);
            idleScheduler.update(now, avatar.restingExpression() != Expression::Sleep);
            updateIdleFrame();
            return;
        }
        idleScheduler.update(now, false);
        idleFrame.active = false;
        if (pendingFailure) { stop(now); return; }
        const Command& command = ledger.command();
        if (!started) {
            started = true;
            beganAt = now;
            record("executing");
            if (command.action == Action::Lock) {
                if (!motion.disarm(now)) { finish(now, motion.unavailableReason()); return; }
                finish(now, nullptr);
                return;
            } else if (command.action == Action::ReleaseTorque) {
                finish(now, motion.releaseTorque(now, command.commandId));
                return;
            } else if (command.action == Action::Arm) {
                if (!motion.arm(now)) { finish(now, motion.unavailableReason()); return; }
            } else if (requiresMotion(command.action)) {
                MotionOperation operation = MotionOperation::Nod;
                switch (command.action) {
                    case Action::MoveLeft: operation = MotionOperation::Left; break;
                    case Action::MoveRight: operation = MotionOperation::Right; break;
                    case Action::MoveUp: operation = MotionOperation::Up; break;
                    case Action::MoveDown: operation = MotionOperation::Down; break;
                    case Action::Home: operation = MotionOperation::Home; break;
                    default: break;
                }
                if (const char* error = motion.start(operation, now)) { finish(now, error); return; }
            }
            Expression expression;
            if (expressionFor(actionName(command.action), expression))
                avatar.apply(now, command.commandId, expression, command.durationMs);
            if (command.action == Action::Warning) indicator = Indicator::Warning;
            if (command.action == Action::Important || routineAction(command.action))
                indicator = Indicator::Important;
            if (routineAction(command.action))
                avatar.apply(now, command.commandId, Expression::Smile, command.durationMs);
            if (pettingCommand) {
                pettingCommand = false;
                if (pettingCue == NotificationCue::Requested) {
                    if (quiet) pettingCue = NotificationCue::Quiet;
                    else if (!audio.available()) pettingCue = NotificationCue::Unavailable;
                    else if (pettingSequenceStage == 3 && pettingAudio) {
                        const char* error = pettingAudio->request("pet-grumpy.wav", quiet, now);
                        if (!error) pettingAudioId = pettingAudio->id();
                        else {
                            pettingAudio->logRejection("pet-grumpy.wav", error, now);
                            pettingCue = NotificationCue::Failed;
                        }
                    } else if (!audio.playTone(pettingAnimation == PettingAnimation::Grumpy ? 330 :
                                               pettingEnthusiastic ? 784 : 659, 80)) {
                        pettingCue = NotificationCue::Failed;
                    } else {
                        notificationSoundActive = true;
                        notificationSoundAt = now;
                        pettingSecond = pettingAnimation != PettingAnimation::Grumpy;
                    }
                }
            }
            if (!quiet && (command.action == Action::Sound ||
                (routineAction(command.action) && audio.available()))) {
                const uint32_t soundMs = command.durationMs < 150 ? command.durationMs : 150;
                if (!audio.play(soundMs)) {
                    finish(now, "sound_failed");
                    return;
                }
            }
        }
        avatar.update(now);
        const uint32_t elapsed = now - beganAt;
        if (pettingActive && pettingSecond && elapsed >= 180) {
            pettingSecond = false;
            if (elapsed < 220 && !audio.playTone(880, 100))
                pettingCue = NotificationCue::Failed;
        }
        if (pettingActive && elapsed < command.durationMs)
            avatar.visible = elapsed < 120 ? Expression::Blink :
                elapsed < 400 ? Expression::Wink : Expression::Smile;
        if (command.action == Action::Sleep)
            avatar.visible = elapsed < command.durationMs / 3 ? Expression::Sleepy : Expression::Sleep;
        if (requiresMotion(command.action) && motion.snapshot().state == MotionState::Fault) {
            finish(now, motion.unavailableReason());
            return;
        }
        if (command.action == Action::Arm) {
            if (motion.snapshot().state == MotionState::Ready) finish(now, nullptr);
            else if (elapsed >= command.durationMs) finish(now, "motion_timeout");
            return;
        }
        if (routineAction(command.action) && elapsed < command.durationMs)
            avatar.visible = elapsed >= command.durationMs * 2 / 3 ? Expression::Smile :
                elapsed >= command.durationMs / 3 ? Expression::Wink : Expression::Smile;
        if (elapsed >= command.durationMs) {
            if (requiresMotion(command.action) && motion.snapshot().state != MotionState::Ready)
                finish(now, "motion_timeout");
            else finish(now, nullptr);
        }
    }

    bool stop(uint32_t now, bool continuing = false) {
        if (!continuing) ++stopAttempt;
        ++stopRevision;
        cancelNotificationSound();
        cancelPettingAudio(now);
        wifiCueSeen = true;
        cancelCues();
        clock = now;
        motion.debugContext(now, ledger.command().commandId);
        if (!stopping) record("stop_requested");
        const auto before = ledger.commandState();
        audio.stop();
        avatar.cancel(now);
        indicator = Indicator::None;
        started = false;
        pettingCommand = false;
        pettingActive = false;
        resetPettingSequence();
        const StopResult result = stopping ? motion.pollStop(now) : motion.requestStop(now);
        stopping = result != StopResult::Confirmed;
        if (result == StopResult::Failed) {
            if (requiresMotion(ledger.command().action)) ledger.fail("motion_stop_unconfirmed");
            else ledger.cancel();
        }
        if (!stopping) {
            if (pendingFailure) ledger.fail(pendingFailure);
            else ledger.cancel();
            pendingFailure = nullptr;
        }
        if (ledger.commandState() != before) record("result", ledger.error());
        return !stopping;
    }

    void emergencyStop(uint32_t now) {
        ++stopAttempt;
        ++stopRevision;
        clock = now;
        cancelNotificationSound();
        cancelPettingAudio(now);
        wifiCueSeen = true;
        cancelCues();
        record("stop_requested");
        const auto before = ledger.commandState();
        audio.stop();
        avatar.cancel(now);
        indicator = Indicator::None;
        started = pettingCommand = pettingActive = false;
        resetPettingSequence();
        pendingFailure = nullptr;
        stopping = false;
        motion.debugContext(now, ledger.command().commandId);
        motion.emergencyStop(now);
        if (requiresMotion(ledger.command().action)) ledger.fail("motion_stop_unconfirmed");
        else ledger.cancel();
        if (ledger.commandState() != before) record("result", ledger.error());
    }

    uint32_t stopEpoch() const { return stopRevision; }
    uint32_t stopGeneration() const { return stopAttempt; }

    void setQuiet(bool value) {
        quiet = value;
        if (quiet) {
            if (pettingActive && pettingCue == NotificationCue::Requested)
                pettingCue = NotificationCue::Quiet;
            cancelNotificationSound();
            cancelPettingAudio(clock);
            cancelCues();
            audio.stop();
            if (ledger.busy() && ledger.command().action == Action::ImportantMessage)
                pendingFailure = "routine_quiet";
        }
    }

    bool isQuiet() const { return quiet; }
    Snapshot snapshot() const {
        return {ledger.bootId(), ledger.command(), ledger.commandState(), ledger.error(), audio.available(),
            motion.snapshot(), quiet, motion.configurationRevision()};
    }
    Expression expression() const { return avatar.visible; }
    Indicator overlay() const { return indicator; }
    IdleFrame idle() const { return idleFrame; }

private:
    CommandLedger ledger;
    Audio& audio;
    AudioTest* pettingAudio = nullptr;
    uint32_t pettingAudioId = 0;
    MotionController blockedMotion;
    MotionController& motion;
    AvatarState avatar;
    IdleScheduler idleScheduler;
    IdleFrame idleFrame;
    Indicator indicator = Indicator::None;
    bool started = false;
    bool pettingCommand = false;
    bool pettingActive = false;
    bool pettingSecond = false;
    bool pettingEnthusiastic = false;
    PettingAnimation pettingAnimation = PettingAnimation::Current;
    uint32_t pettingStartedAt = 0;
    uint32_t pettingWindowAt = 0, pettingCooldownAt = 0;
    uint8_t pettingCount = 0;
    uint8_t pettingSequenceStage = 0;
    bool pettingCooldown = false;
    NotificationCue pettingCue = NotificationCue::NotRequested;
    bool quiet = false;
    bool stopping = false;
    uint32_t stopAttempt = 0;
    bool startupSoundAttempted = false;
    bool startupSoundPlayed = false;
    enum class Cue { None, Home, Capture, Wifi };
    bool wifiCueSeen = false;
    Cue lastCue = Cue::None;
    bool notificationSoundActive = false;
    uint32_t notificationSoundAt = 0;
    uint32_t startupSoundAt = 0, lastNoteAt = 0, lastNoteDuration = 0;
    uint8_t homeNotes = 0, captureNotes = 0, wifiNotes = 0;
    const char* wifiCueError = nullptr;
    const char* startupSoundError = nullptr;
    const char* pendingFailure = nullptr;
    uint32_t beganAt = 0;
    static constexpr uint8_t kLogCapacity = 16;
    RuntimeLogEvent logs[kLogCapacity]{};
    uint8_t logHead = 0, logCount = 0;
    uint32_t clock = 0, droppedLogs = 0;

    bool cueBusy(uint32_t now) const {
        return homeNotes || captureNotes || wifiNotes ||
            (startupSoundPlayed && now - startupSoundAt < 320) ||
            (lastCue != Cue::None && now - lastNoteAt < lastNoteDuration);
    }

    void cancelPettingAudio(uint32_t now) {
        if (pettingAudioId && pettingAudio && pettingAudio->id() == pettingAudioId &&
            pettingAudio->active()) {
            pettingAudio->cancel(now);
            audio.stop();
        }
        pettingAudioId = 0;
    }

    void tickPettingAudio(uint32_t now) {
        if (!pettingAudioId || !pettingAudio) return;
        if (pettingAudio->id() != pettingAudioId) {
            pettingAudioId = 0;
            return;
        }
        if (pettingAudio->active()) return;
        const char* error = pettingAudio->error();
        const bool fallback = pettingAudio->failed() && error && strcmp(error, "audio_busy") &&
            !quiet && audio.available();
        pettingAudioId = 0;
        if (fallback) {
            if (audio.playTone(330, 80)) {
                notificationSoundActive = true;
                notificationSoundAt = now;
            } else pettingCue = NotificationCue::Failed;
        } else if (pettingAudio->failed()) pettingCue = NotificationCue::Failed;
    }

    void cancelCues() {
        if (homeNotes || captureNotes || wifiNotes ||
            (lastCue != Cue::None && clock - lastNoteAt < lastNoteDuration)) audio.stop();
        homeNotes = captureNotes = wifiNotes = 0;
    }

    void tickCues(uint32_t now) {
        if ((!homeNotes && !captureNotes && !wifiNotes) || notificationSoundActive ||
            (pettingAudio && pettingAudio->active()) ||
            (startupSoundPlayed && now - startupSoundAt < 320)) return;
        const Cue cue = wifiNotes == 1 ? Cue::Wifi :
            captureNotes == 1 ? Cue::Capture :
            homeNotes ? Cue::Home : captureNotes ? Cue::Capture : Cue::Wifi;
        if (lastCue != Cue::None && now - lastNoteAt < lastNoteDuration + (cue == lastCue ? 100U : 280U))
            return;
        const bool home = cue != Cue::Wifi;
        auto& notes = cue == Cue::Home ? homeNotes : cue == Cue::Capture ? captureNotes : wifiNotes;
        const uint32_t duration = cue == Cue::Home ? 450 : cue == Cue::Capture ? 80 : 120;
        const char* error = !audio.available() ? (home ? "home_sound_unavailable" : "wifi_sound_unavailable") :
            !audio.play(duration) ? (home ? "home_sound_failed" : "wifi_sound_failed") : nullptr;
        if (error) {
            if (!home) wifiCueError = error;
            notes = 0;
            audio.stop();
            record("failure", error);
        } else {
            lastCue = cue;
            lastNoteAt = now;
            lastNoteDuration = duration;
            --notes;
            record(cue == Cue::Home ? "home_sound" : cue == Cue::Capture ? "home_capture_sound" : "wifi_sound");
        }
    }

    uint32_t stopRevision = 0;
    void record(const char* event, const char* error = nullptr, const Command* received = nullptr) {
        if (!motion.remoteYawBench() && !motion.twoAxisTest()) return;
        const auto& command = received ? *received : ledger.command();
        logs[logHead] = {clock, ledger.bootId(), command.commandId, event, actionName(command.action),
            stateName(ledger.commandState()), error};
        logHead = (logHead + 1) % kLogCapacity;
        if (logCount < kLogCapacity) ++logCount;
        else ++droppedLogs;
    }

    static Expression expressionForEyes(IdleEyes eyes) {
        switch (eyes) {
            case IdleEyes::Blink: return Expression::Blink;
            case IdleEyes::LookUp: return Expression::LookUp;
            case IdleEyes::LookLeft: return Expression::LookLeft;
            case IdleEyes::LookRight: return Expression::LookRight;
            default: return Expression::Neutral;
        }
    }

    void updateIdleFrame() {
        const uint8_t motionIndex = idleScheduler.motionIndex();
        const IdleStep& step = idleScheduler.current();
        idleFrame.active = avatar.restingExpression() != Expression::Sleep && !ledger.busy();
        idleFrame.motionIndex = motionIndex;
        idleFrame.eyes = step.eyes;
        if (idleFrame.active) avatar.visible = expressionForEyes(step.eyes);
    }

    void finish(uint32_t now, const char* error) {
        if (ledger.command().action != Action::Lock && ledger.command().action != Action::ReleaseTorque &&
            requiresMotion(ledger.command().action) &&
            motion.snapshot().state != MotionState::Ready) {
            pendingFailure = error ? error : "motion_unconfirmed";
            record("failure", pendingFailure);
            stop(now);
            return;
        }
        if (pettingActive && pettingAnimation == PettingAnimation::Grumpy)
            pettingCooldownAt = now;
        const bool petting = pettingActive;
        const bool ownedTone = notificationSoundActive;
        pettingSecond = false;
        notificationSoundActive = false;
        if ((petting && ownedTone) || (!petting && !(pettingAudio && pettingAudio->active())))
            audio.stop();
        avatar.cancel(now);
        indicator = Indicator::None;
        started = false;
        pettingActive = false;
        if (pendingFailure) { ledger.fail(pendingFailure); pendingFailure = nullptr; }
        else if (error) ledger.fail(error);
        else ledger.complete();
        record("result", ledger.error());
    }
};

}  // namespace lab

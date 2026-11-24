#pragma once

#include <atomic>
#include <cstring>
#include <optional>
#include <stdint.h>

namespace lab {

enum class HeadDirection : uint8_t {
    Left, Right, Up, Down, UpLeft, UpRight, DownLeft, DownRight, Center, Stop
};

struct HeadTarget {
    int x = 0;
    int y = 0;
};

enum class HeadCommandResult : uint8_t { NoCommand, Submitted, Stopped };

using MotionTestLog = void (*)(uint32_t, const char*, ...);

struct HomeReferenceSnapshot {
    int32_t openCode = 0, yawCode = 0, pitchCode = 0;
    int32_t yawRaw = 0, pitchRaw = 0;
    bool yawRead = false, yawValid = false, pitchRead = false, pitchValid = false;

    bool valid() const { return yawRead && yawValid && pitchRead && pitchValid; }
};

struct NoHomeReferenceReader {
    HomeReferenceSnapshot operator()() const { return {}; }
};

enum class HomeCaptureResult : uint8_t { None, Verified, Inconclusive, Failed };

inline bool directionDestination(HeadDirection direction, int step, HeadTarget current, HeadTarget& destination) {
    destination = current;
    switch (direction) {
        case HeadDirection::Left: destination.x += step; break;
        case HeadDirection::Right: destination.x -= step; break;
        case HeadDirection::Up: destination.y += step; break;
        case HeadDirection::Down: destination.y -= step; break;
        case HeadDirection::UpLeft: destination.x += step; destination.y += step; break;
        case HeadDirection::UpRight: destination.x -= step; destination.y += step; break;
        case HeadDirection::DownLeft: destination.x += step; destination.y -= step; break;
        case HeadDirection::DownRight: destination.x -= step; destination.y -= step; break;
        default: return false;
    }
    return destination.x >= -1280 && destination.x <= 1280 && destination.y >= 0 && destination.y <= 900;
}

template <typename Bsp, typename Clock>
HeadCommandResult moveHead(Bsp& bsp, HeadDirection direction, HeadTarget& target,
                           Clock now, MotionTestLog log, int step = 20) {
    constexpr int speed = 200;
    switch (direction) {
        case HeadDirection::Center:
            log(now(), "COMMAND operation=goHome speed=%d", speed);
            bsp.Motion.goHome(speed);
            log(now(), "COMMAND_RETURN operation=goHome");
            target = {0, 0};
            return HeadCommandResult::Submitted;
        case HeadDirection::Stop:
            log(now(), "COMMAND operation=Motion.stop");
            bsp.Motion.stop();
            log(now(), "COMMAND_RETURN operation=Motion.stop");
            log(now(), "COMMAND operation=setServoPowerEnabled value=false");
            bsp.setServoPowerEnabled(false);
            log(now(), "COMMAND_RETURN operation=setServoPowerEnabled");
            return HeadCommandResult::Stopped;
        default: break;
    }

    HeadTarget destination;
    if (!directionDestination(direction, step, target, destination)) return HeadCommandResult::NoCommand;
    const int x = destination.x;
    const int y = destination.y;

    if (x != target.x && y != target.y) {
        log(now(), "COMMAND operation=move x=%d y=%d speed=%d", x, y, speed);
        bsp.Motion.move(x, y, speed);
        log(now(), "COMMAND_RETURN operation=move");
    } else if (x != target.x) {
        log(now(), "COMMAND operation=moveX x=%d speed=%d", x, speed);
        bsp.Motion.moveX(x, speed);
        log(now(), "COMMAND_RETURN operation=moveX");
    } else {
        log(now(), "COMMAND operation=moveY y=%d speed=%d", y, speed);
        bsp.Motion.moveY(y, speed);
        log(now(), "COMMAND_RETURN operation=moveY");
    }
    target = {x, y};
    return HeadCommandResult::Submitted;
}

class BspMotionTest {
public:
    enum class Axis { Yaw, Pitch };
    enum class State { Idle, Preparing, Running, Complete, Stopped, Failed, Rejected, Uncertain };
    enum class Action : uint8_t {
        None, OriginalSmoke, Original035, HeadLeft, HeadRight, HeadUp, HeadDown,
        HeadUpLeft, HeadUpRight, HeadDownLeft, HeadDownRight, HeadCenter, HeadStop,
        HomeCapture, HomeGo, MoveX, MoveY, MoveXY, NodYes, ShakeNo, Dance, GrumpyShake
    };

    static constexpr int velocity = 300;
    static constexpr int defaultSpeed = 300, maxSpeed = 1000;
    static constexpr uint32_t smokeMs = 800, original035Ms = 100;
    static constexpr uint32_t animationSettleMs = 1500, powerSettleMs = 200;
    using Log = MotionTestLog;

    explicit BspMotionTest(std::atomic<bool>& stop) : stop(stop) {}
    State state() const { return state_.load(std::memory_order_acquire); }
    const char* failure() const { return failure_.load(std::memory_order_acquire); }
    int failureCode() const { return code_.load(std::memory_order_acquire); }
    bool gestureSubmitted() const { return gestureSubmitted_.load(std::memory_order_acquire); }
    Action action() const { return action_.load(std::memory_order_acquire); }
    uint32_t requestId() const { return requestId_.load(std::memory_order_acquire); }
    bool directionReferenceReady() const {
        return directionReferenceReady_.load(std::memory_order_acquire) && !stop.load(std::memory_order_acquire);
    }
    const char* directionReferenceError(Action action) const {
        return isDirection(action) && !directionReferenceReady() ? "direction_home_required" : nullptr;
    }

    static const char* actionName(Action action) {
        switch (action) {
            case Action::OriginalSmoke: return "original_smoke";
            case Action::Original035: return "original_035";
            case Action::HeadLeft: return "head_left";
            case Action::HeadRight: return "head_right";
            case Action::HeadUp: return "head_up";
            case Action::HeadDown: return "head_down";
            case Action::HeadUpLeft: return "head_up_left";
            case Action::HeadUpRight: return "head_up_right";
            case Action::HeadDownLeft: return "head_down_left";
            case Action::HeadDownRight: return "head_down_right";
            case Action::HeadCenter: return "head_center";
            case Action::HeadStop: return "head_stop";
            case Action::HomeCapture: return "home_capture";
            case Action::HomeGo: return "home_go";
            case Action::MoveX: return "move_x";
            case Action::MoveY: return "move_y";
            case Action::MoveXY: return "move_xy";
            case Action::NodYes: return "nod_yes";
            case Action::ShakeNo: return "shake_no";
            case Action::Dance: return "dance";
            case Action::GrumpyShake: return "grumpy_shake";
            default: return "none";
        }
    }

    static Action localMotionTestAction(Axis axis, int direction) {
        if (axis == Axis::Yaw)
            return direction > 0 ? Action::HeadLeft : Action::HeadRight;
        return direction > 0 ? Action::HeadUp : Action::HeadDown;
    }

    static bool isOffset(Action action) {
        return action == Action::MoveX || action == Action::MoveY || action == Action::MoveXY;
    }
    static bool isDirection(Action action) {
        return action >= Action::HeadLeft && action <= Action::HeadDownRight;
    }
    static int maxDirectionStep(Action action) {
        return action == Action::HeadLeft || action == Action::HeadRight ? 2560 : 900;
    }
    static bool supportsSpeed(Action action) { return isOffset(action) || action == Action::HomeGo; }
    static bool requiresHome(Action action) { return supportsSpeed(action) || isDirection(action) || gesture(action).count != 0; }
    static bool validOffset(Action action, double value) {
        return action == Action::MoveX ? value >= -1280 && value <= 1280 :
            action == Action::MoveY && value >= 0 && value <= 900;
    }

    const char* motionHomeError(const HomeReferenceSnapshot& home) const {
        if (!home.valid()) return action_ == Action::HomeGo ? "stored_references_unavailable" : "stored_home_required";
        if (isDirection(action_)) {
            HeadTarget destination;
            if (!directionDestination(headDirection(action_), offset_, target_, destination))
                return "direction_limit_reached";
            if (!rawTargetInRange(home.yawRaw, destination.x) || !rawTargetInRange(home.pitchRaw, destination.y))
                return "offset_exceeds_bsp_raw_envelope";
        }
        const auto sequence = gesture(action_);
        for (unsigned i = 0; i < sequence.count; ++i) {
            const auto point = sequence.points[i];
            if (!rawTargetInRange(home.yawRaw, point.x) || !rawTargetInRange(home.pitchRaw, point.y))
                return "offset_exceeds_bsp_raw_envelope";
        }
        if ((isYawOffset(action_) && !rawTargetInRange(home.yawRaw, offset_)) ||
            (isPitchOffset(action_) && !rawTargetInRange(home.pitchRaw, action_ == Action::MoveXY ? offsetY_ : offset_)))
            return "offset_exceeds_bsp_raw_envelope";
        return nullptr;
    }

    bool reserve(Action action, std::optional<int> offset = std::nullopt, std::optional<int> speed = std::nullopt) {
        if (action == Action::MoveXY) return false;
        return reserveOffsets(action, offset, std::nullopt, speed);
    }
    bool reserveMoveXY(int x, int y, std::optional<int> speed = std::nullopt) {
        return reserveOffsets(Action::MoveXY, x, y, speed);
    }
private:
    bool reserveOffsets(Action action, std::optional<int> x, std::optional<int> y,
                        std::optional<int> speed) {
        if (action == Action::None || stop.load(std::memory_order_acquire) ||
            requestId_.load(std::memory_order_acquire) == UINT32_MAX) return false;
        const bool combined = action == Action::MoveXY;
        if (combined ? (!x || !y || !validOffset(Action::MoveX, *x) || !validOffset(Action::MoveY, *y)) :
            (y.has_value() || (isDirection(action) ?
                (x && (*x < 1 || *x > maxDirectionStep(action))) :
                (isOffset(action) != x.has_value() || (x && !validOffset(action, *x)))))) return false;
        if (speed && (!supportsSpeed(action) || *speed < 0 || *speed > maxSpeed)) return false;
        auto expected = state();
        if (expected != State::Idle && expected != State::Complete && expected != State::Rejected &&
            !((expected == State::Failed || expected == State::Uncertain) && action == Action::HeadStop)) return false;
        if (!state_.compare_exchange_strong(expected, State::Preparing)) return false;
        gestureSubmitted_.store(false, std::memory_order_release);
        action_ = action;
        offset_ = x.value_or(isDirection(action) ? 20 : 0);
        offsetY_ = y.value_or(0);
        speed_ = speed.value_or(defaultSpeed);
        failure_.store(nullptr, std::memory_order_release);
        code_.store(0, std::memory_order_relaxed);
        requestId_.fetch_add(1, std::memory_order_release);
        return true;
    }
public:
    bool reserve(Axis axis, int direction) {
        if (direction != -1 && direction != 1) return false;
        return reserve(localMotionTestAction(axis, direction));
    }
    void fail(const char* stage, int code, bool = false) {
        directionReferenceReady_.store(false, std::memory_order_release);
        recordFailure(stage, code);
        state_.store(State::Failed, std::memory_order_release);
    }
    void cancelPending() {
        directionReferenceReady_.store(false, std::memory_order_release);
        auto expected = State::Preparing;
        state_.compare_exchange_strong(expected, State::Stopped);
    }
    void rejectPending(const char* reason) {
        if (!preservesDirectionReference(reason))
            directionReferenceReady_.store(false, std::memory_order_release);
        recordFailure(reason, 0);
        auto expected = State::Preparing;
        state_.compare_exchange_strong(expected, stop.load(std::memory_order_acquire) ? State::Stopped : State::Rejected);
    }

    template <typename Bsp, typename Clock, typename Wait, typename HomeRead = NoHomeReferenceReader>
    void run(Bsp& bsp, Clock now, Wait wait, Log log, uint32_t bootId = 0,
             bool startLogged = false, HomeRead readHome = {},
             void (*reportCapture)(HomeCaptureResult) = nullptr,
             void (*traceYawWrites)(bool, uint32_t, Log) = nullptr,
             bool freshBspInit = false) {
        auto expected = State::Preparing;
        if (!state_.compare_exchange_strong(expected, State::Running)) return;
        const Action current = action_;
        if (const char* error = directionReferenceError(current)) {
            recordFailure(error, 0);
            finish(now, log, State::Rejected);
            return;
        }
        if (current == Action::HomeGo || current == Action::HomeCapture ||
            current == Action::HeadCenter || current == Action::HeadStop ||
            current == Action::OriginalSmoke || current == Action::Original035)
            directionReferenceReady_.store(false, std::memory_order_release);
        if (!startLogged) {
            log(now(), "START experiment=%s bootId=%lu version=%s "
                "bsp=fb5e9f5697803ca64e45fe7ad8eaea3e4100f766 implementation=original physical=unverified",
                actionName(current), static_cast<unsigned long>(bootId), firmwareVersion());
        }

        if (current == Action::OriginalSmoke) {
            log(now(), "COMMAND operation=rotateX value=300");
            bsp.Motion.rotateX(300);
            log(now(), "COMMAND_RETURN operation=rotateX");
            const uint32_t elapsed = waitBounded(smokeMs, now, wait);
            log(now(), "WAIT requested_ms=%lu elapsed_ms=%lu",
                static_cast<unsigned long>(smokeMs), static_cast<unsigned long>(elapsed));
            if (stop.load(std::memory_order_acquire)) {
                stopMotion(bsp, now, log);
                finish(now, log, State::Stopped);
                return;
            }
            log(now(), "COMMAND operation=Motion.stop");
            bsp.Motion.stop();
            log(now(), "COMMAND_RETURN operation=Motion.stop");
            finish(now, log, State::Complete);
            return;
        }

        if (current == Action::Original035) {
            log(now(), "COMMAND operation=rotateX value=300");
            bsp.Motion.rotateX(300);
            log(now(), "COMMAND_RETURN operation=rotateX");
            const uint32_t elapsed = waitBounded(original035Ms, now, wait);
            log(now(), "WAIT requested_ms=%lu elapsed_ms=%lu",
                static_cast<unsigned long>(original035Ms), static_cast<unsigned long>(elapsed));
            log(now(), "COMMAND operation=rotateX value=0");
            bsp.Motion.rotateX(0);
            log(now(), "COMMAND_RETURN operation=rotateX");
            log(now(), "COMMAND operation=setServoPowerEnabled value=false");
            bsp.setServoPowerEnabled(false);
            log(now(), "COMMAND_RETURN operation=setServoPowerEnabled");
            powerOffRequested_ = true;
            log(now(), "COMMAND operation=Motion.stop");
            bsp.Motion.stop();
            log(now(), "COMMAND_RETURN operation=Motion.stop");
            finish(now, log, stop.load(std::memory_order_acquire) ? State::Stopped : State::Complete);
            return;
        }

        if (current == Action::HomeCapture) {
            // Read after BSP init so its default writes never count as a capture.
            const auto before = readHome();
            logHomeReference(now(), log, "before_capture", before);
            if (stop.load(std::memory_order_acquire)) {
                cutCapturePower(bsp, now, log);
                return;
            }
            const bool moving = bsp.Motion.isMoving();
            if (stop.load(std::memory_order_acquire)) {
                cutCapturePower(bsp, now, log);
                return;
            }
            if (moving) {
                log(now(), "HOME_CAPTURE REJECTED reason=motion_busy_or_unreadable physical=unverified");
                if (reportCapture) reportCapture(HomeCaptureResult::Inconclusive);
                finish(now, log, State::Complete);
                return;
            }
            log(now(), "COMMAND operation=setCurrentPostionAsHome");
            bsp.Motion.setCurrentPostionAsHome();
            log(now(), "COMMAND_RETURN operation=setCurrentPostionAsHome result=void");
            const auto after = readHome();
            logHomeReference(now(), log, "after_capture", after);

            const bool comparable = before.valid() && after.valid();
            const unsigned changedMask = comparable ?
                (before.yawRaw != after.yawRaw ? 1U : 0U) |
                (before.pitchRaw != after.pitchRaw ? 2U : 0U) : 0U;
            const char* changed = !comparable ? "unknown" :
                changedMask == 3 ? "both" : changedMask == 0 ? "none" : "partial";
            const auto outcome = !after.valid() ? HomeCaptureResult::Failed :
                comparable && changedMask == 3 ? HomeCaptureResult::Verified : HomeCaptureResult::Inconclusive;
            const char* verification = outcome == HomeCaptureResult::Verified ? "stored_references_changed" :
                outcome == HomeCaptureResult::Failed ? "stored_references_unavailable" : "inconclusive";
            log(now(), "HOME_CAPTURE references_changed=%s changed_mask=%u verification=%s "
                "physical=unverified", changed, changedMask, verification);
            if (stop.load(std::memory_order_acquire)) {
                cutCapturePower(bsp, now, log);
                return;
            }
            if (reportCapture) reportCapture(outcome);
            finish(now, log, State::Complete);
            return;
        }

        const bool homeGo = current == Action::HomeGo;
        const bool observePosition = supportsSpeed(current);
        const auto sequence = gesture(current);
        if (requiresHome(current)) {
            const auto reference = readHome();
            if (homeGo) logHomeReference(now(), log, "before_go", reference);
            if (const char* error = motionHomeError(reference)) {
                if (sequence.count) {
                    rejectGesture(now, log, error);
                    return;
                }
                log(now(), "%s reason=%s physical=unverified", homeGo ? "HOME_GO REJECTED" : "OFFSET_REJECTED", error);
                recordFailure(error, 0);
                finish(now, log, State::Rejected);
                return;
            }
            if (stop.load(std::memory_order_acquire)) {
                stopMotion(bsp, now, log);
                finish(now, log, State::Stopped);
                return;
            }
        }
        const HeadDirection direction = headDirection(current);
        if ((isOffset(current) || sequence.count || direction != HeadDirection::Stop) && powerOffRequested_) {
            log(now(), "COMMAND operation=setServoPowerEnabled value=true");
            bsp.setServoPowerEnabled(true);
            log(now(), "COMMAND_RETURN operation=setServoPowerEnabled");
            const uint32_t elapsed = waitBounded(powerSettleMs, now, wait);
            log(now(), "WAIT requested_ms=%lu elapsed_ms=%lu reason=servo_power_start",
                static_cast<unsigned long>(powerSettleMs), static_cast<unsigned long>(elapsed));
            if (stop.load(std::memory_order_acquire)) {
                stopMotion(bsp, now, log);
                finish(now, log, State::Stopped);
                return;
            }
            powerOffRequested_ = false;
        }

        if (sequence.count) {
            runGesture(bsp, now, wait, log, sequence, freshBspInit);
            return;
        }

        auto result = HeadCommandResult::Submitted;
        if (observePosition) {
            logPosition(bsp, now, log, "before", 0);
            if (stop.load(std::memory_order_acquire)) {
                stopMotion(bsp, now, log);
                finish(now, log, State::Stopped);
                return;
            }
            const char* operation = homeGo ? "goHome" : current == Action::MoveXY ? "move" :
                current == Action::MoveX ? "moveX" : "moveY";
            if (homeGo) {
                log(now(), "COMMAND operation=goHome requested_x=0 requested_y=0 units=tenths_of_degree speed=%d", speed_);
                bsp.Motion.goHome(speed_);
                target_ = {0, 0};
            } else if (current == Action::MoveXY) {
                log(now(), "COMMAND operation=move requested_x=%d requested_y=%d units=tenths_of_degree speed=%d",
                    offset_, offsetY_, speed_);
                bsp.Motion.move(offset_, offsetY_, speed_);
                target_ = {offset_, offsetY_};
            } else if (current == Action::MoveX) {
                log(now(), "COMMAND operation=%s requested_offset=%d units=tenths_of_degree speed=%d", operation, offset_, speed_);
                if (traceYawWrites) traceYawWrites(true, now(), log);
                bsp.Motion.moveX(offset_, speed_);
                target_.x = offset_;
            } else {
                log(now(), "COMMAND operation=%s requested_offset=%d units=tenths_of_degree speed=%d", operation, offset_, speed_);
                bsp.Motion.moveY(offset_, speed_);
                target_.y = offset_;
            }
            log(now(), "COMMAND_RETURN operation=%s result=void physical=unverified", operation);
        } else {
            result = moveHead(bsp, direction, target_, now, log, offset_);
        }
        if (result == HeadCommandResult::Stopped) {
            powerOffRequested_ = true;
            finish(now, log, State::Complete);
            return;
        }
        if (result == HeadCommandResult::NoCommand) {
            recordFailure("direction_limit_reached", 0);
            finish(now, log, State::Rejected);
            return;
        }

        const uint32_t submitted = now();
        const uint32_t elapsed = waitBounded(animationSettleMs, now, wait);
        log(now(), "ANIMATION_WAIT requested_ms=%lu elapsed_ms=%lu interval_elapsed=%u "
            "physical=unverified", static_cast<unsigned long>(animationSettleMs),
            static_cast<unsigned long>(elapsed),
            static_cast<unsigned>(elapsed >= animationSettleMs));
        const char* observedMotion = nullptr;
        if (observePosition && !stop.load(std::memory_order_acquire)) {
            logPosition(bsp, now, log, "after_wait", now() - submitted);
            waitBounded(animationSettleMs, now, wait);
            if (!stop.load(std::memory_order_acquire)) {
                const bool moving = bsp.Motion.isMoving();
                if (!stop.load(std::memory_order_acquire)) {
                    observedMotion = moving ? "busy_or_unreadable" : "stationary_reported";
                    logPosition(bsp, now, log, "later", now() - submitted, observedMotion);
                }
            }
        }
        if (!observePosition && !stop.load(std::memory_order_acquire)) {
            bool moving = bsp.Motion.isMoving();
            if (isDirection(current)) {
                constexpr uint32_t directionTimeoutMs = 20000, pollMs = 40;
                while (moving && !stop.load(std::memory_order_acquire) &&
                       now() - submitted < directionTimeoutMs) {
                    waitBounded(pollMs, now, wait);
                    if (!stop.load(std::memory_order_acquire)) moving = bsp.Motion.isMoving();
                }
            }
            observedMotion = moving ? "busy_or_unreadable" : "stationary_reported";
        }
        if (current == Action::MoveX && traceYawWrites)
            traceYawWrites(false, now(), stop.load(std::memory_order_acquire) ? nullptr : log);
        if (stop.load(std::memory_order_acquire)) {
            stopMotion(bsp, now, log);
            finish(now, log, State::Stopped);
        } else if (observedMotion && !std::strcmp(observedMotion, "busy_or_unreadable")) {
            recordFailure("motion_busy_or_unreadable", 0);
            finish(now, log, State::Uncertain);
        } else {
            finish(now, log, State::Complete, observedMotion);
        }
    }

private:
    struct Gesture {
        const HeadTarget* points = nullptr;
        unsigned count = 0;
    };

    static Gesture gesture(Action action) {
        static constexpr HeadTarget nod[] = {{0, 100}, {0, 0}, {0, 100}, {0, 0}};
        static constexpr HeadTarget shake[] = {{-150, 0}, {150, 0}, {-150, 0}, {0, 0}};
        static constexpr HeadTarget dance[] = {{120, 90}, {-120, 25}, {100, 90}, {-100, 25}, {0, 0}};
        static constexpr HeadTarget grumpy[] = {{-50, 0}, {50, 0}, {0, 0}};
        switch (action) {
            case Action::NodYes: return {nod, sizeof(nod) / sizeof(nod[0])};
            case Action::ShakeNo: return {shake, sizeof(shake) / sizeof(shake[0])};
            case Action::Dance: return {dance, sizeof(dance) / sizeof(dance[0])};
            case Action::GrumpyShake: return {grumpy, sizeof(grumpy) / sizeof(grumpy[0])};
            default: return {};
        }
    }

    static bool rawTargetInRange(int homeRaw, int offset) {
        // Match the pinned BSP's integer mapping from tenths of a degree to raw counts.
        const int raw = homeRaw + offset * 16 / 5 / 10;
        return raw >= 0 && raw <= 1000;
    }

    template <typename Clock>
    void rejectGesture(Clock now, Log log, const char* reason) {
        log(now(), "REJECTED source=http_test experiment=%s reason=%s physical=unverified", actionName(action_), reason);
        directionReferenceReady_.store(false, std::memory_order_release);
        recordFailure(reason, 0);
        state_.store(stop.load(std::memory_order_acquire) ? State::Stopped : State::Rejected, std::memory_order_release);
    }

    template <typename Bsp, typename Clock, typename Wait>
    void runGesture(Bsp& bsp, Clock now, Wait wait, Log log, Gesture sequence, bool freshBspInit) {
        constexpr int gestureSpeed = 450;
        constexpr uint32_t pollMs = 40, settleMs = 60, initSettleMs = 480, waypointTimeoutMs = 4000;
        const char* name = actionName(action_);
        const uint32_t started = now();
        bool moving = !stop.load(std::memory_order_acquire) && bsp.Motion.isMoving();
        // The BSP treats a failed moving-register read as busy, including right after begin().
        while (freshBspInit && moving && !stop.load(std::memory_order_acquire)) {
            const uint32_t elapsed = now() - started;
            if (elapsed >= initSettleMs) break;
            const uint32_t remaining = initSettleMs - elapsed;
            waitBounded(remaining < pollMs ? remaining : pollMs, now, wait);
            if (stop.load(std::memory_order_acquire)) break;
            moving = bsp.Motion.isMoving();
        }
        if (moving && !stop.load(std::memory_order_acquire)) {
            rejectGesture(now, log, "motion_busy_or_unreadable");
            return;
        }
        if (action_ == Action::GrumpyShake && !stop.load(std::memory_order_acquire))
            logGrumpyYaw(bsp, now, log, "initial_after_bsp_init");
        for (unsigned i = 0; i < sequence.count && !stop.load(std::memory_order_acquire); ++i) {
            const auto point = sequence.points[i];
            log(now(), "WAYPOINT experiment=%s index=%u requested_x=%d requested_y=%d units=tenths_of_degree speed=%d",
                name, i + 1, point.x, point.y, gestureSpeed);
            if (stop.load(std::memory_order_acquire)) break;
            if (action_ == Action::GrumpyShake) bsp.Motion.moveX(point.x, gestureSpeed);
            else bsp.Motion.move(point.x, point.y, gestureSpeed);
            gestureSubmitted_.store(true, std::memory_order_release);
            if (action_ == Action::GrumpyShake) target_.x = point.x;
            else target_ = point;
            const uint32_t submitted = now();
            waitBounded(settleMs, now, wait);
            bool stationary = false;
            while (!stop.load(std::memory_order_acquire) && now() - submitted < waypointTimeoutMs) {
                if (bsp.Motion.isMoving()) {
                    waitBounded(pollMs, now, wait);
                    continue;
                }
                // Allow the BSP's final target snap and confirm it has settled.
                waitBounded(settleMs, now, wait);
                if (!stop.load(std::memory_order_acquire) && now() - submitted < waypointTimeoutMs &&
                    !bsp.Motion.isMoving()) {
                    stationary = true;
                    break;
                }
            }
            if (stop.load(std::memory_order_acquire)) break;
            if (!stationary) {
                recordFailure("waypoint_timeout", i + 1);
                stopMotion(bsp, now, log);
                finish(now, log, State::Failed);
                return;
            }
        }
        if (action_ == Action::GrumpyShake && !stop.load(std::memory_order_acquire))
            logGrumpyYaw(bsp, now, log, "final");
        if (stop.load(std::memory_order_acquire)) {
            stopMotion(bsp, now, log);
            finish(now, log, State::Stopped);
        } else {
            finish(now, log, State::Complete);
        }
    }

    std::atomic<bool>& stop;
    std::atomic<State> state_{State::Idle};
    std::atomic<bool> gestureSubmitted_{false};
    std::atomic<const char*> failure_{nullptr};
    std::atomic<int> code_{0};
    std::atomic<Action> action_{Action::None};
    std::atomic<uint32_t> requestId_{0};
    std::atomic<bool> directionReferenceReady_{false};
    int offset_ = 0;
    int offsetY_ = 0;
    int speed_ = defaultSpeed;
    // This remains a logical target, not measured physical feedback.
    HeadTarget target_{};
    bool powerOffRequested_ = false;

    static bool isYawOffset(Action action) { return action == Action::MoveX || action == Action::MoveXY; }
    static bool isPitchOffset(Action action) { return action == Action::MoveY || action == Action::MoveXY; }
    bool preservesDirectionReference(const char* reason) const {
        return isDirection(action_) && reason && (!std::strcmp(reason, "direction_limit_reached") ||
            !std::strcmp(reason, "offset_exceeds_bsp_raw_envelope"));
    }
    template <typename Bsp, typename Clock>
    static void logGrumpyYaw(Bsp& bsp, Clock now, Log log, const char* phase) {
        const int yaw = bsp.Motion.getCurrentYawAngle();
        log(now(), "POSITION experiment=grumpy_shake phase=%s yaw=%d units=tenths_of_degree "
            "source=bsp_read_or_fallback physical=unverified", phase, yaw);
    }

    template <typename Bsp, typename Clock>
    static void logPosition(Bsp& bsp, Clock now, Log log, const char* phase, uint32_t elapsed,
                            const char* motion = "not_sampled") {
        const auto position = bsp.Motion.getCurrentAngles();
        log(now(), "POSITION phase=%s x=%d y=%d elapsed_ms=%lu units=tenths_of_degree "
            "source=bsp_read_or_fallback bsp_motion=%s physical=unverified",
            phase, position.x, position.y, static_cast<unsigned long>(elapsed), motion);
    }

    static const char* firmwareVersion() {
#ifdef NETBOT_FIRMWARE_VERSION
        return NETBOT_FIRMWARE_VERSION;
#else
        return "unknown";
#endif
    }

    static void logHomeReference(uint32_t now, Log log, const char* phase,
                                 const HomeReferenceSnapshot& reference) {
        log(now, "HOME_REFERENCE phase=%s namespace=servo nvs_open_code=%ld "
            "yaw_key=zero_pos_1 yaw_read=%u yaw_code=%ld yaw_raw=%ld yaw_valid=%u "
            "pitch_key=zero_pos_2 pitch_read=%u pitch_code=%ld pitch_raw=%ld pitch_valid=%u",
            phase, static_cast<long>(reference.openCode), static_cast<unsigned>(reference.yawRead),
            static_cast<long>(reference.yawCode), static_cast<long>(reference.yawRead ? reference.yawRaw : -1),
            static_cast<unsigned>(reference.yawValid), static_cast<unsigned>(reference.pitchRead),
            static_cast<long>(reference.pitchCode), static_cast<long>(reference.pitchRead ? reference.pitchRaw : -1),
            static_cast<unsigned>(reference.pitchValid));
    }

    static HeadDirection headDirection(Action action) {
        switch (action) {
            case Action::HeadLeft: return HeadDirection::Left;
            case Action::HeadRight: return HeadDirection::Right;
            case Action::HeadUp: return HeadDirection::Up;
            case Action::HeadDown: return HeadDirection::Down;
            case Action::HeadUpLeft: return HeadDirection::UpLeft;
            case Action::HeadUpRight: return HeadDirection::UpRight;
            case Action::HeadDownLeft: return HeadDirection::DownLeft;
            case Action::HeadDownRight: return HeadDirection::DownRight;
            case Action::HeadCenter: return HeadDirection::Center;
            default: return HeadDirection::Stop;
        }
    }

    template <typename Clock, typename Wait>
    uint32_t waitBounded(uint32_t duration, Clock now, Wait wait) {
        const uint32_t started = now();
        while (!stop.load(std::memory_order_acquire)) {
            const uint32_t elapsed = static_cast<uint32_t>(now() - started);
            if (elapsed >= duration) break;
            const uint32_t remaining = duration - elapsed;
            wait(remaining < 5 ? remaining : 5);
        }
        return static_cast<uint32_t>(now() - started);
    }

    template <typename Bsp, typename Clock>
    void stopMotion(Bsp& bsp, Clock now, Log log) {
        log(now(), "COMMAND operation=Motion.stop");
        bsp.Motion.stop();
        log(now(), "COMMAND_RETURN operation=Motion.stop");
        log(now(), "COMMAND operation=setServoPowerEnabled value=false");
        bsp.setServoPowerEnabled(false);
        log(now(), "COMMAND_RETURN operation=setServoPowerEnabled");
        powerOffRequested_ = true;
    }

    template <typename Bsp, typename Clock>
    void cutCapturePower(Bsp& bsp, Clock now, Log log) {
        log(now(), "STOP capture_rail_off_requested physical=unverified");
        bsp.setServoPowerEnabled(false);
        powerOffRequested_ = true;
        finish(now, log, State::Stopped);
    }

    void recordFailure(const char* stage, int code) {
        if (failure_.load(std::memory_order_acquire)) return;
        code_.store(code, std::memory_order_relaxed);
        failure_.store(stage, std::memory_order_release);
    }

    template <typename Clock>
    void finish(Clock now, Log log, State result, const char* observedMotion = nullptr) {
        if (stop.load(std::memory_order_acquire) && result != State::Failed) result = State::Stopped;
        if (result != State::Complete && !(result == State::Rejected && preservesDirectionReference(failure())))
            directionReferenceReady_.store(false, std::memory_order_release);
        else if (result == State::Complete) {
            const auto sequence = gesture(action_);
            const bool gestureHome = action_ != Action::GrumpyShake && sequence.count &&
                gestureSubmitted() && sequence.points[sequence.count - 1].x == 0 &&
                sequence.points[sequence.count - 1].y == 0;
            if ((action_ == Action::HomeGo && observedMotion &&
                 !std::strcmp(observedMotion, "stationary_reported")) || gestureHome)
                directionReferenceReady_.store(true, std::memory_order_release);
        }
        const char* state = result == State::Complete ? "complete" :
            result == State::Stopped ? "stopped" : result == State::Failed ? "failed" :
            result == State::Rejected ? "rejected" : result == State::Uncertain ? "uncertain" : "unknown";
        const bool observed = result == State::Complete && observedMotion;
        log(now(), "COMPLETE experiment=%s state=%s physical=unverified failure=%s code=%d%s%s",
            actionName(action_), state, failure() ? failure() : "none", failureCode(),
            observed ? " observation=window_ended bsp_motion=" : "", observed ? observedMotion : "");
        state_.store(result, std::memory_order_release);
    }
};

} // namespace lab

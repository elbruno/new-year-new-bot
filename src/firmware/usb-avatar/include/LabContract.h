// Goal: Define the firmware's action and command admission contract.
// Responsibilities: Represent actions, durations, command identities, and
// boot-scoped command state.
// How it works:
// 1. Action names are mapped to supported action identifiers.
// 2. The ledger validates each command against the current boot and prior IDs.
// 3. Callers receive admission results without replaying duplicate commands.
#pragma once

#include <stdint.h>
#include <string.h>

namespace lab {

constexpr uint32_t kVersion = 1;
constexpr uint32_t kMinimumDurationMs = 100;
constexpr uint32_t kMaximumDurationMs = 5000;
constexpr uint32_t kMaximumBodyBytes = 256;

enum class Action {
    Invalid, Neutral, Wink, Smile, LookUp, Sleep, Wake,
    Warning, Important, Sound, Nod, ImportantNotification,
    MoveLeft, MoveRight, MoveUp, MoveDown, Home, ImportantMessage, Arm, Lock, ReleaseTorque
};
enum class CommandState { None, Accepted, Completed, Cancelled, Failed };

inline Action actionFor(const char* name) {
    if (!name) return Action::Invalid;
    if (!strcmp(name, "neutral")) return Action::Neutral;
    if (!strcmp(name, "wink")) return Action::Wink;
    if (!strcmp(name, "smile")) return Action::Smile;
    if (!strcmp(name, "look_up")) return Action::LookUp;
    if (!strcmp(name, "sleep")) return Action::Sleep;
    if (!strcmp(name, "wake")) return Action::Wake;
    if (!strcmp(name, "warning")) return Action::Warning;
    if (!strcmp(name, "important")) return Action::Important;
    if (!strcmp(name, "sound")) return Action::Sound;
    if (!strcmp(name, "nod")) return Action::Nod;
    if (!strcmp(name, "important_notification")) return Action::ImportantNotification;
    if (!strcmp(name, "move_left")) return Action::MoveLeft;
    if (!strcmp(name, "move_right")) return Action::MoveRight;
    if (!strcmp(name, "move_up")) return Action::MoveUp;
    if (!strcmp(name, "move_down")) return Action::MoveDown;
    if (!strcmp(name, "home")) return Action::Home;
    if (!strcmp(name, "important_message")) return Action::ImportantMessage;
    if (!strcmp(name, "arm")) return Action::Arm;
    if (!strcmp(name, "lock")) return Action::Lock;
    if (!strcmp(name, "release_torque")) return Action::ReleaseTorque;
    return Action::Invalid;
}

inline bool motionAction(Action action) {
    return action == Action::Lock || action == Action::Arm || action == Action::ReleaseTorque ||
        action == Action::Nod || action == Action::MoveLeft || action == Action::MoveRight ||
        action == Action::MoveUp || action == Action::MoveDown || action == Action::Home;
}
inline bool routineAction(Action action) {
    return action == Action::ImportantMessage || action == Action::ImportantNotification;
}
inline bool requiresMotion(Action action) { return motionAction(action) || action == Action::ImportantMessage; }

inline const char* stateName(CommandState state) {
    switch (state) {
        case CommandState::Accepted: return "accepted";
        case CommandState::Completed: return "completed";
        case CommandState::Cancelled: return "cancelled";
        case CommandState::Failed: return "failed";
        default: return "none";
    }
}

struct Command {
    uint32_t bootId;
    uint32_t commandId;
    Action action;
    uint32_t durationMs;
    uint32_t configRevision = 0;
    bool hasConfigRevision = false;
};

struct Admission {
    int status;
    const char* code;
    bool execute;
};

// One ledger for local buttons and HTTP. A boot-scoped high-water mark prevents
// old IDs from executing again without retaining an unbounded command history.
class CommandLedger {
public:
    explicit CommandLedger(uint32_t bootId) : session(bootId) {}

    Admission admit(const Command& command, bool supported) {
        if (!session || !command.bootId || !command.commandId || command.durationMs < kMinimumDurationMs ||
            command.durationMs > kMaximumDurationMs)
            return {400, "invalid_command", false};
        if (command.bootId != session) return {409, "boot_changed", false};
        if (command.commandId == last.commandId) {
            if (command.action != last.action || command.durationMs != last.durationMs ||
                command.configRevision != last.configRevision || command.hasConfigRevision != last.hasConfigRevision)
                return {409, "id_conflict", false};
            return {busy() ? 202 : 200, "duplicate", false};
        }
        if (command.commandId < last.commandId) return {409, "stale_id", false};
        if (command.action == Action::Invalid || !supported)
            return {422, "unsupported_action", false};
        if (busy()) return {409, "busy", false};
        last = command;
        state = CommandState::Accepted;
        failure = nullptr;
        return {202, "accepted", true};
    }

    void complete() {
        if (busy()) state = CommandState::Completed;
    }

    void fail(const char* code) {
        if (busy()) {
            state = CommandState::Failed;
            failure = code;
        }
    }

    void cancel() {
        if (busy()) state = CommandState::Cancelled;
    }

    bool busy() const { return state == CommandState::Accepted; }
    uint32_t bootId() const { return session; }
    const Command& command() const { return last; }
    CommandState commandState() const { return state; }
    const char* error() const { return failure; }

private:
    uint32_t session;
    Command last{0, 0, Action::Invalid, 0};
    CommandState state = CommandState::None;
    const char* failure = nullptr;
};

}  // namespace lab

#pragma once

#include <atomic>
#include <mutex>
#include <stdint.h>
#include <string.h>
#include "LabContract.h"

namespace lab {

inline constexpr uint8_t kOperationalEventCapacity = 32;
inline constexpr uint8_t kOperationalEventPageSize = 8;
inline constexpr size_t kOperationalEventQueryBytes = 128;

enum class InteractionOrigin : uint8_t { Unknown, Physical, Remote, Count };
struct OperationalMetadata {
    InteractionOrigin origin = InteractionOrigin::Unknown;
    uint8_t valid = 0, requested = 0, before = 0, after = 0;
    static constexpr uint8_t Requested = 1, Before = 2, After = 4;
};
inline const char* interactionOriginName(InteractionOrigin origin) {
    switch (origin) {
        case InteractionOrigin::Physical: return "physical";
        case InteractionOrigin::Remote: return "remote";
        default: return "unknown";
    }
}

enum class OperationalEventKind : uint8_t { Boot, Ui, Action, Brightness, Stop, Notification, Touch, Count };
enum class OperationalStatus : uint8_t {
    Ready, Changed, Requested, Pending, Completed, Failed, Unavailable, Busy,
    Expired, Presented, Opened, Dismissed, Restored, Acknowledged, Observed, Count
};
enum class OperationalAction : uint8_t {
    None, Back, Demo, Motion, Network, Settings, Shutdown, Setup, ShowSetupKey,
    CancelSetup, Arm, Disarm, Quiet, Stop, PowerOff, Neutral, Smile, Wink, LookUp,
    Sleep, Wake, Warning, Important, ImportantNotification, ImportantMessage,
    Sound, Nod, MoveLeft, MoveRight, MoveUp, MoveDown, Lock, Home, BrightnessUp,
    BrightnessDown, OpenInCompanion, Discard, Acknowledge, Open, Menu, StatusStrip, Count
};
enum class OperationalError : uint8_t {
    None, Busy, Unavailable, OutOfRange, Conflict, Timeout, Failed, Unsupported, Count
};

struct OperationalEventEntry {
    uint32_t seq = 0, atMs = 0;
    OperationalEventKind event = OperationalEventKind::Boot;
    OperationalStatus status = OperationalStatus::Ready;
    OperationalAction action = OperationalAction::None;
    OperationalError error = OperationalError::None;
    uint32_t correlationId = 0, relatedId = 0, revision = 0;
    OperationalMetadata metadata{};
};

struct OperationalEventQuery {
    bool bound = false, filterEnabled = false;
    uint32_t bootId = 0, after = 0, limit = kOperationalEventPageSize;
    OperationalEventKind filter = OperationalEventKind::Boot;
};

struct OperationalEventPage {
    uint32_t bootId = 0, oldestSeq = 0, latestSeq = 0, nextAfter = 0;
    uint32_t overwritten = 0, dropped = 0;
    uint8_t count = 0;
    bool hasMore = false;
    OperationalEventEntry entries[kOperationalEventPageSize]{};
};

inline const char* operationalEventName(OperationalEventKind value) {
    switch (value) {
        case OperationalEventKind::Boot: return "boot";
        case OperationalEventKind::Ui: return "ui";
        case OperationalEventKind::Action: return "action";
        case OperationalEventKind::Brightness: return "brightness";
        case OperationalEventKind::Stop: return "stop";
        case OperationalEventKind::Notification: return "notification";
        case OperationalEventKind::Touch: return "touch";
        default: return "invalid";
    }
}

inline const char* operationalStatusName(OperationalStatus value) {
    switch (value) {
        case OperationalStatus::Ready: return "ready";
        case OperationalStatus::Changed: return "changed";
        case OperationalStatus::Requested: return "requested";
        case OperationalStatus::Pending: return "pending";
        case OperationalStatus::Completed: return "completed";
        case OperationalStatus::Failed: return "failed";
        case OperationalStatus::Unavailable: return "unavailable";
        case OperationalStatus::Busy: return "busy";
        case OperationalStatus::Expired: return "expired";
        case OperationalStatus::Presented: return "presented";
        case OperationalStatus::Opened: return "opened";
        case OperationalStatus::Dismissed: return "dismissed";
        case OperationalStatus::Restored: return "restored";
        case OperationalStatus::Acknowledged: return "acknowledged";
        case OperationalStatus::Observed: return "observed";
        default: return "invalid";
    }
}

inline const char* operationalActionName(OperationalAction value) {
    switch (value) {
        case OperationalAction::None: return "none";
        case OperationalAction::Back: return "back";
        case OperationalAction::Demo: return "demo";
        case OperationalAction::Motion: return "motion";
        case OperationalAction::Network: return "network";
        case OperationalAction::Settings: return "settings";
        case OperationalAction::Shutdown: return "shutdown";
        case OperationalAction::Setup: return "setup";
        case OperationalAction::ShowSetupKey: return "show_setup_key";
        case OperationalAction::CancelSetup: return "cancel_setup";
        case OperationalAction::Arm: return "arm";
        case OperationalAction::Disarm: return "disarm";
        case OperationalAction::Quiet: return "quiet";
        case OperationalAction::Stop: return "stop";
        case OperationalAction::PowerOff: return "power_off";
        case OperationalAction::Neutral: return "neutral";
        case OperationalAction::Smile: return "smile";
        case OperationalAction::Wink: return "wink";
        case OperationalAction::LookUp: return "look_up";
        case OperationalAction::Sleep: return "sleep";
        case OperationalAction::Wake: return "wake";
        case OperationalAction::Warning: return "warning";
        case OperationalAction::Important: return "important";
        case OperationalAction::ImportantNotification: return "important_notification";
        case OperationalAction::ImportantMessage: return "important_message";
        case OperationalAction::Sound: return "sound";
        case OperationalAction::Nod: return "nod";
        case OperationalAction::MoveLeft: return "move_left";
        case OperationalAction::MoveRight: return "move_right";
        case OperationalAction::MoveUp: return "move_up";
        case OperationalAction::MoveDown: return "move_down";
        case OperationalAction::Lock: return "lock";
        case OperationalAction::Home: return "home";
        case OperationalAction::BrightnessUp: return "brightness_up";
        case OperationalAction::BrightnessDown: return "brightness_down";
        case OperationalAction::OpenInCompanion: return "open_in_companion";
        case OperationalAction::Discard: return "discard";
        case OperationalAction::Acknowledge: return "acknowledge";
        case OperationalAction::Open: return "open";
        case OperationalAction::Menu: return "menu";
        case OperationalAction::StatusStrip: return "status_strip";
        default: return "invalid";
    }
}

inline const char* operationalErrorName(OperationalError value) {
    switch (value) {
        case OperationalError::None: return "none";
        case OperationalError::Busy: return "busy";
        case OperationalError::Unavailable: return "unavailable";
        case OperationalError::OutOfRange: return "out_of_range";
        case OperationalError::Conflict: return "conflict";
        case OperationalError::Timeout: return "timeout";
        case OperationalError::Failed: return "failed";
        case OperationalError::Unsupported: return "unsupported";
        default: return "invalid";
    }
}

inline OperationalAction operationalActionFor(Action action) {
    switch (action) {
        case Action::Neutral: return OperationalAction::Neutral;
        case Action::Wink: return OperationalAction::Wink;
        case Action::Smile: return OperationalAction::Smile;
        case Action::LookUp: return OperationalAction::LookUp;
        case Action::Sleep: return OperationalAction::Sleep;
        case Action::Wake: return OperationalAction::Wake;
        case Action::Warning: return OperationalAction::Warning;
        case Action::Important: return OperationalAction::Important;
        case Action::Sound: return OperationalAction::Sound;
        case Action::Nod: return OperationalAction::Nod;
        case Action::ImportantNotification: return OperationalAction::ImportantNotification;
        case Action::MoveLeft: return OperationalAction::MoveLeft;
        case Action::MoveRight: return OperationalAction::MoveRight;
        case Action::MoveUp: return OperationalAction::MoveUp;
        case Action::MoveDown: return OperationalAction::MoveDown;
        case Action::Home: return OperationalAction::Home;
        case Action::ImportantMessage: return OperationalAction::ImportantMessage;
        case Action::Arm: return OperationalAction::Arm;
        case Action::Lock: return OperationalAction::Lock;
        default: return OperationalAction::None;
    }
}

inline OperationalError operationalErrorFor(const char* code) {
    if (!code) return OperationalError::None;
    if (!strcmp(code, "busy") || !strcmp(code, "stopping") ||
        !strcmp(code, "brightness_busy")) return OperationalError::Busy;
    if (!strcmp(code, "brightness_unavailable") || !strcmp(code, "events_unavailable"))
        return OperationalError::Unavailable;
    if (!strcmp(code, "brightness_out_of_range") || !strcmp(code, "brightness_at_minimum") ||
        !strcmp(code, "brightness_at_maximum") || !strcmp(code, "out_of_range"))
        return OperationalError::OutOfRange;
    if (!strcmp(code, "boot_changed") || !strcmp(code, "id_conflict") ||
        !strcmp(code, "stale_id")) return OperationalError::Conflict;
    if (!strcmp(code, "stop_unconfirmed") || !strcmp(code, "timeout"))
        return OperationalError::Timeout;
    if (!strcmp(code, "unsupported_action")) return OperationalError::Unsupported;
    return OperationalError::Failed;
}

inline bool operationalEventFromName(const char* value, size_t length, OperationalEventKind& result) {
    for (uint8_t i = 0; i < static_cast<uint8_t>(OperationalEventKind::Count); ++i) {
        const auto candidate = static_cast<OperationalEventKind>(i);
        const char* name = operationalEventName(candidate);
        if (strlen(name) == length && !strncmp(value, name, length)) {
            result = candidate;
            return true;
        }
    }
    return false;
}

inline bool operationalUnsigned(const char* value, const char* end, uint32_t& result) {
    if (value >= end || (end - value > 1 && *value == '0')) return false;
    uint32_t number = 0;
    for (const char* digit = value; digit < end; ++digit) {
        if (*digit < '0' || *digit > '9' ||
            number > (UINT32_MAX - static_cast<uint32_t>(*digit - '0')) / 10u) return false;
        number = number * 10u + static_cast<uint32_t>(*digit - '0');
    }
    result = number;
    return true;
}

inline bool parseOperationalEventQuery(const char* uri, OperationalEventQuery& result) {
    result = {};
    const char* query = strchr(uri, '?');
    if (!query) return true;
    ++query;
    if (!*query || strlen(query) > kOperationalEventQueryBytes) return false;

    bool seenBoot = false, seenAfter = false, seenLimit = false, seenEvent = false;
    while (*query) {
        const char* end = strchr(query, '&');
        if (!end) end = query + strlen(query);
        const char* equals = strchr(query, '=');
        if (!equals || equals >= end || equals == query || equals + 1 == end) return false;
        const char* value = equals + 1;
        uint32_t parsed = 0;
        if (equals - query == 6 && !strncmp(query, "bootId", 6)) {
            if (seenBoot || !operationalUnsigned(value, end, parsed)) return false;
            result.bootId = parsed;
            seenBoot = true;
        } else if (equals - query == 5 && !strncmp(query, "after", 5)) {
            if (seenAfter || !operationalUnsigned(value, end, parsed)) return false;
            result.after = parsed;
            seenAfter = true;
        } else if (equals - query == 5 && !strncmp(query, "limit", 5)) {
            if (seenLimit || !operationalUnsigned(value, end, parsed) ||
                parsed < 1 || parsed > kOperationalEventPageSize) return false;
            result.limit = parsed;
            seenLimit = true;
        } else if (equals - query == 5 && !strncmp(query, "event", 5)) {
            if (seenEvent || !operationalEventFromName(value, static_cast<size_t>(end - value), result.filter))
                return false;
            result.filterEnabled = true;
            seenEvent = true;
        } else return false;
        if (!*end) break;
        query = end + 1;
        if (!*query) return false;
    }
    if (seenBoot != seenAfter || (seenBoot && !result.bootId)) return false;
    result.bound = seenBoot;
    return true;
}

class OperationalEventLog {
public:
    void reset(uint32_t value) {
        std::lock_guard<std::mutex> lock(mutex);
        bootId = value;
        head = count = 0;
        latest = overwritten = 0;
        dropped.store(0);
    }

    bool append(uint32_t atMs, OperationalEventKind event, OperationalStatus status,
                OperationalAction action, OperationalError error, uint32_t correlationId = 0,
                uint32_t relatedId = 0, uint32_t revision = 0, OperationalMetadata metadata = {}) {
        if (!valid(event) || !valid(status) || !valid(action) || !valid(error) ||
            !valid(metadata.origin) || (metadata.valid & ~7u) ||
            ((metadata.valid & OperationalMetadata::Requested) &&
                (metadata.requested < 32 || metadata.requested > 192))) {
            lose();
            return false;
        }
        std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
        if (!lock.owns_lock() || !bootId || latest == UINT32_MAX) {
            lose();
            return false;
        }
        auto& entry = entries[head];
        entry = {++latest, atMs, event, status, action, error, correlationId, relatedId, revision, metadata};
        head = (head + 1) % kOperationalEventCapacity;
        if (count < kOperationalEventCapacity) ++count;
        else if (overwritten < UINT32_MAX) ++overwritten;
        return true;
    }

    const char* read(const OperationalEventQuery& query, OperationalEventPage& page) {
        page = OperationalEventPage{};
        std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
        if (!lock.owns_lock()) return "events_unavailable";
        page.bootId = bootId;
        page.oldestSeq = count ? entries[oldest()].seq : 0;
        page.latestSeq = latest;
        page.overwritten = overwritten;
        page.dropped = dropped.load();
        if (!bootId) return "events_unavailable";
        if (latest == UINT32_MAX) return "event_sequence_exhausted";
        if (query.bound && query.bootId != bootId) return "boot_changed";
        if (query.after > latest) return "event_cursor_ahead";
        if (query.bound && count && query.after < page.oldestSeq - 1) return "event_cursor_expired";

        page.nextAfter = query.after;
        for (uint8_t i = 0; i < count; ++i) {
            const auto& entry = entries[(oldest() + i) % kOperationalEventCapacity];
            if (entry.seq <= query.after) continue;
            const bool matches = !query.filterEnabled || entry.event == query.filter;
            if (matches && page.count == query.limit) {
                page.hasMore = true;
                page.nextAfter = entry.seq - 1;
                return nullptr;
            }
            if (matches) page.entries[page.count++] = entry;
            page.nextAfter = entry.seq;
        }
        page.nextAfter = latest;
        return nullptr;
    }

private:
#ifdef NETBOT_TEST_BUILD
    friend struct OperationalEventTestAccess;
#endif
    std::mutex mutex;
    uint32_t bootId = 0, latest = 0, overwritten = 0;
    std::atomic<uint32_t> dropped{0};
    uint8_t head = 0, count = 0;
    OperationalEventEntry entries[kOperationalEventCapacity]{};

    uint8_t oldest() const { return (head + kOperationalEventCapacity - count) % kOperationalEventCapacity; }

    template <typename T>
    static bool valid(T value) { return static_cast<uint8_t>(value) < static_cast<uint8_t>(T::Count); }

    void lose() {
        auto value = dropped.load();
        while (!dropped.compare_exchange_weak(value, value == UINT32_MAX ? value : value + 1)) {}
    }
};

static_assert(sizeof(OperationalEventLog) + sizeof(OperationalEventPage) <= 4096,
              "Operational event ring exceeds its fixed RAM budget.");

} // namespace lab

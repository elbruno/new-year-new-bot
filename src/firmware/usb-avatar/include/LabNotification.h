// Goal: Manage the firmware's bounded notification lifecycle.
// Responsibilities: Define notification templates, identity, state, and receipts.
// How it works:
// 1. A request selects a supported template and notification identity.
// 2. Presentation and user choices advance the notification state.
// 3. Snapshots and receipts report progress or expiration.
#pragma once

#include "LabOperationalEvents.h"

namespace lab {

inline constexpr uint32_t kNotificationLifetimeMs = 60000;
inline constexpr size_t kNotificationBodyBytes = 512;
inline constexpr uint8_t kNotificationDemoVersion = 1;
struct NotificationTemplate {
    const char* id;
    const char* title;
    const char* detail;
};
inline constexpr NotificationTemplate kNotificationTemplates[] = {
    {"synthetic_review", "Synthetic notification", "Review the synthetic item in your companion."},
    {"synthetic_form", "Synthetic review form", "Source: Synthetic. Item: Demo review. Display only."},
    {"demo_important_email", "Important email", "Demo email: review the release notes."},
    {"demo_important_message", "Important message", "Demo message: please review the plan."},
    {"demo_github_action_failed", "GitHub Action failed", "Demo build: test stage failed."},
};
inline const NotificationTemplate* findNotificationTemplate(const char* id) {
    if (id) for (const auto& item : kNotificationTemplates)
        if (!strcmp(id, item.id)) return &item;
    return nullptr;
}

enum class NotificationState { None, Received, Presented, Opened, Dismissed, ActionRequested, Expired, Failed, Acknowledged };
enum class NotificationReceipt { None, Pending, Completed, Failed, Unknown };
enum class NotificationChoice { Open, Discard, OpenInCompanion, Acknowledge };
enum class NotificationCue { NotRequested, Requested, Quiet, Busy, Unavailable, Failed };
inline const char* notificationCueName(NotificationCue cue) {
    switch (cue) {
        case NotificationCue::Requested: return "requested";
        case NotificationCue::Quiet: return "quiet";
        case NotificationCue::Busy: return "busy";
        case NotificationCue::Unavailable: return "unavailable";
        case NotificationCue::Failed: return "failed";
        default: return "not_requested";
    }
}
struct NotificationIdentity {
    uint32_t bootId = 0, notificationId = 0, revision = 0;
};
struct NotificationSnapshot {
    NotificationIdentity identity{};
    const char* templateId = "";
    const char* title = "";
    const char* detail = "";
    NotificationState state = NotificationState::None;
    bool active = false, avatarRestored = true, restoreRequested = false;
    uint32_t remainingMs = 0, interactionId = 0, interactionRevision = 0;
    NotificationReceipt receipt = NotificationReceipt::None;
    NotificationCue arrivalCue = NotificationCue::NotRequested;
    const char* error = nullptr;
};

inline const char* notificationStateName(NotificationState state) {
    switch (state) {
        case NotificationState::Received: return "received";
        case NotificationState::Presented: return "presented";
        case NotificationState::Opened: return "opened";
        case NotificationState::Dismissed: return "dismissed";
        case NotificationState::ActionRequested: return "action_requested";
        case NotificationState::Expired: return "expired";
        case NotificationState::Failed: return "failed";
        case NotificationState::Acknowledged: return "acknowledged";
        default: return "none";
    }
}
inline const char* notificationReceiptName(NotificationReceipt state) {
    switch (state) {
        case NotificationReceipt::Pending: return "pending";
        case NotificationReceipt::Completed: return "completed";
        case NotificationReceipt::Failed: return "failed";
        case NotificationReceipt::Unknown: return "unknown";
        default: return "none";
    }
}
inline bool notificationTemplate(const char* value) {
    return findNotificationTemplate(value) != nullptr;
}

// The caller serializes the reducer with Device::mutex; telemetry is optional.
class Notification {
public:
    using Sink = void (*)(void*, const OperationalEventEntry&);
    void reset(uint32_t bootId, Sink callback = nullptr, void* context = nullptr) {
        value = {};
        value.identity.bootId = bootId;
        admittedAt = interactionHighWater = 0;
        acknowledged = false;
        sink = callback;
        sinkContext = context;
    }
    NotificationSnapshot snapshot(uint32_t now) const {
        auto result = value;
        if (value.active || value.receipt == NotificationReceipt::Pending)
            result.remainingMs = now - admittedAt < kNotificationLifetimeMs ?
                kNotificationLifetimeMs - (now - admittedAt) : 0;
        return result;
    }
    Admission receive(uint32_t bootId, uint32_t id, const char* templateId, uint32_t now) {
        if (!bootId || !id || !notificationTemplate(templateId)) return {400, "invalid_notification", false};
        if (bootId != value.identity.bootId) return {409, "boot_changed", false};
        if (!available()) return {503, "notification_unavailable", false};
        if (id == value.identity.notificationId)
            return {strcmp(templateId, value.templateId) ? 409 : 200,
                strcmp(templateId, value.templateId) ? "notification_id_conflict" : "duplicate", false};
        if (id < value.identity.notificationId) return {409, "stale_notification", false};
        if (value.active || value.receipt == NotificationReceipt::Pending || value.restoreRequested)
            return {409, "notification_busy", false};
        const uint32_t revision = value.identity.revision;
        value = {};
        acknowledged = false;
        value.identity = {bootId, id, revision};
        const auto& item = *findNotificationTemplate(templateId);
        value.templateId = item.id;
        value.title = item.title;
        value.detail = item.detail;
        value.active = true;
        value.avatarRestored = false;
        value.state = NotificationState::Received;
        admittedAt = now;
        emit(now, OperationalStatus::Ready);
        return {202, "accepted", true};
    }
    void advance(uint32_t now, bool receiverPresent) {
        if (!value.active && value.receipt != NotificationReceipt::Pending) return;
        if (now - admittedAt >= kNotificationLifetimeMs) {
            if (value.receipt == NotificationReceipt::Pending) {
                value.receipt = NotificationReceipt::Unknown;
                value.error = "timeout";
            }
            finish(now, NotificationState::Expired, OperationalStatus::Expired, OperationalError::Timeout);
        } else if (value.receipt == NotificationReceipt::Pending && !receiverPresent) {
            value.receipt = NotificationReceipt::Unknown;
            value.error = "receiver_disconnected";
            finish(now, NotificationState::Failed, OperationalStatus::Failed, OperationalError::Unavailable);
        }
    }
    Admission choose(const NotificationIdentity& identity, NotificationChoice choice, uint32_t now,
                     bool receiverPresent, InteractionOrigin origin = InteractionOrigin::Unknown) {
        advance(now, receiverPresent);
        if (!matches(identity) || !value.active) return {409, "stale_notification", false};
        if (!available()) return {503, "notification_unavailable", false};
        if (choice == NotificationChoice::Open) {
            if (value.state != NotificationState::Presented) return {409, "wrong_notification_state", false};
            value.state = NotificationState::Opened;
            emit(now, OperationalStatus::Opened, OperationalAction::Open, OperationalError::None, origin);
        } else if (choice == NotificationChoice::Discard) {
            finish(now, NotificationState::Dismissed, OperationalStatus::Dismissed,
                OperationalError::None, OperationalAction::Discard, origin);
        } else if (choice == NotificationChoice::Acknowledge) {
            if (value.state != NotificationState::Opened) return {409, "wrong_notification_state", false};
            finish(now, NotificationState::Acknowledged, OperationalStatus::Acknowledged,
                OperationalError::None, OperationalAction::Acknowledge, origin);
        } else {
            if (value.state != NotificationState::Opened) return {409, "wrong_notification_state", false};
            if (interactionHighWater == UINT32_MAX) return {503, "notification_unavailable", false};
            value.interactionId = ++interactionHighWater;
            value.receipt = receiverPresent ? NotificationReceipt::Pending : NotificationReceipt::Failed;
            value.error = receiverPresent ? nullptr : "receiver_unavailable";
            finish(now, receiverPresent ? NotificationState::ActionRequested : NotificationState::Failed,
                receiverPresent ? OperationalStatus::Requested : OperationalStatus::Failed,
                receiverPresent ? OperationalError::None : OperationalError::Unavailable,
                OperationalAction::OpenInCompanion, origin);
            value.interactionRevision = value.identity.revision;
        }
        return {200, "completed", true};
    }
    bool presented(const NotificationIdentity& identity, uint32_t now,
                   NotificationCue cue = NotificationCue::NotRequested) {
        if (!matches(identity) || value.state != NotificationState::Received) return false;
        value.state = NotificationState::Presented;
        value.arrivalCue = cue;
        emit(now, OperationalStatus::Presented);
        return true;
    }
    void restored(uint32_t now) {
        if (!value.restoreRequested) return;
        value.restoreRequested = false;
        value.avatarRestored = true;
        emit(now, OperationalStatus::Restored);
    }
    Admission acknowledge(uint32_t bootId, uint32_t notificationId, uint32_t interactionId,
                          uint32_t revision, bool completed, uint32_t now, bool receiverPresent) {
        advance(now, receiverPresent);
        if (bootId != value.identity.bootId) return {409, "boot_changed", false};
        if (!interactionId || notificationId != value.identity.notificationId ||
            interactionId != value.interactionId || revision != value.interactionRevision)
            return {409, "stale_interaction", false};
        const auto terminal = completed ? NotificationReceipt::Completed : NotificationReceipt::Failed;
        if (acknowledged && value.receipt == terminal) return {200, "duplicate", false};
        if (value.receipt != NotificationReceipt::Pending) return {409, "interaction_conflict", false};
        if (!available()) return {503, "notification_unavailable", false};
        value.receipt = terminal;
        acknowledged = true;
        value.error = completed ? nullptr : "receiver_failed";
        if (!completed) value.state = NotificationState::Failed;
        emit(now, completed ? OperationalStatus::Completed : OperationalStatus::Failed,
            OperationalAction::OpenInCompanion, completed ? OperationalError::None : OperationalError::Failed);
        return {200, "completed", true};
    }
    bool matches(const NotificationIdentity& identity) const {
        return identity.bootId == value.identity.bootId &&
            identity.notificationId == value.identity.notificationId && identity.revision == value.identity.revision;
    }

private:
    NotificationSnapshot value{};
    uint32_t admittedAt = 0, interactionHighWater = 0;
    bool acknowledged = false;
    Sink sink = nullptr;
    void* sinkContext = nullptr;
    bool available() const { return value.identity.bootId && value.identity.revision < UINT32_MAX - 8; }
    void finish(uint32_t now, NotificationState state, OperationalStatus status,
                OperationalError error = OperationalError::None, OperationalAction action = OperationalAction::None,
                InteractionOrigin origin = InteractionOrigin::Unknown) {
        value.state = state;
        value.active = false;
        value.restoreRequested = !value.avatarRestored;
        emit(now, status, action, error, origin);
    }
    void emit(uint32_t now, OperationalStatus status, OperationalAction action = OperationalAction::None,
              OperationalError error = OperationalError::None, InteractionOrigin origin = InteractionOrigin::Unknown) {
        if (value.identity.revision < UINT32_MAX) ++value.identity.revision;
        if (sink) sink(sinkContext, {0, now, OperationalEventKind::Notification, status, action, error,
            value.identity.notificationId, value.interactionId, value.identity.revision, {origin}});
    }
};
static_assert(sizeof(Notification) + sizeof(NotificationSnapshot) <= 1024,
              "Notification state and readback exceed the fixed RAM budget.");

} // namespace lab

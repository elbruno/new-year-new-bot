// Goal: Translate between firmware state and the local HTTP API's JSON.
// Responsibilities: Validate request fields and serialize supported responses.
// How it works:
// 1. Helpers reject invalid or oversized JSON and unexpected fields.
// 2. Parsers convert valid input into firmware request structures.
// 3. Response helpers encode runtime and device information for HTTP callers.
#pragma once

#include <cmath>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "DisplayBrightness.h"
#include "LabNetwork.h"
#include "LabRuntime.h"
#include "LabNotification.h"

#ifndef NETBOT_FIRMWARE_VERSION
#error "Build with release_metadata.py or PlatformIO to select a catalog version."
#endif

namespace lab {

struct ParsedRequest {
    bool valid = false;
    Command command{};
};

// The protocol accepts flat objects only. Reject nesting before calling the
// framework JSON parser, whose compiled-in recursion limit is much larger.
inline bool flatJson(const char* body, size_t length, size_t maximum = kMaximumBodyBytes) {
    if (!body || !length || length > maximum || memchr(body, 0, length)) return false;
    bool quoted = false, escaped = false, opened = false;
    for (size_t i = 0; i < length; ++i) {
        const char value = body[i];
        if (quoted) {
            if (escaped) { escaped = false; continue; }
            if (value == '\\') escaped = true;
            else if (value == '"') quoted = false;
        } else if (value == '"') quoted = true;
        else if (value == '[' || (value == '{' && opened)) return false;
        else if (value == '{') opened = true;
    }
    // cJSON strings are NUL-terminated; embedded decoded NUL must not truncate names.
    for (size_t i = 0; i + 6 <= length; ++i)
        if (!memcmp(body + i, "\\u0000", 6)) return false;
    return opened && !quoted;
}

inline bool unsignedField(const cJSON* root, const char* name, uint32_t& value, uint32_t minimum = 1) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) ||
        item->valuedouble < minimum || item->valuedouble > UINT32_MAX ||
        std::floor(item->valuedouble) != item->valuedouble) return false;
    value = static_cast<uint32_t>(item->valuedouble);
    return true;
}

inline bool exactKeys(const cJSON* root, const char* const* names, size_t count) {
    uint32_t seen = 0;
    for (const cJSON* item = root ? root->child : nullptr; item; item = item->next) {
        size_t i = 0;
        while (i < count && strcmp(item->string, names[i])) ++i;
        if (i == count || (seen & (1U << i))) return false;
        seen |= 1U << i;
    }
    return seen == (1U << count) - 1;
}

struct NotificationRequest {
    uint32_t bootId = 0, notificationId = 0;
    const char* templateId = nullptr;
};
struct NotificationAcknowledgement {
    uint32_t bootId = 0, notificationId = 0, interactionId = 0, revision = 0;
    bool completed = false;
};
inline bool notificationVersion(const cJSON* root) {
    uint32_t version = 0;
    return unsignedField(root, "schemaVersion", version) && version == 1;
}
inline bool jsonEnd(const char* end, const char* limit) {
    if (!end) return false;
    while (end < limit && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) ++end;
    return end == limit;
}
inline bool parseNotification(const char* body, size_t length, NotificationRequest& request) {
    if (!flatJson(body, length, kNotificationBodyBytes)) return false;
    const char* end = nullptr;
    cJSON* root = cJSON_ParseWithLengthOpts(body, length, &end, false);
    const char* const keys[] = {"schemaVersion", "bootId", "notificationId", "templateId"};
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, "templateId");
    const bool valid = cJSON_IsObject(root) && jsonEnd(end, body + length) && exactKeys(root, keys, 4) &&
        notificationVersion(root) && unsignedField(root, "bootId", request.bootId) &&
        unsignedField(root, "notificationId", request.notificationId) && cJSON_IsString(item) &&
        notificationTemplate(item->valuestring);
    if (valid) request.templateId = findNotificationTemplate(item->valuestring)->id;
    cJSON_Delete(root);
    return valid;
}
inline bool parseNotificationAck(const char* body, size_t length, NotificationAcknowledgement& request) {
    if (!flatJson(body, length, kNotificationBodyBytes)) return false;
    const char* end = nullptr;
    cJSON* root = cJSON_ParseWithLengthOpts(body, length, &end, false);
    const char* const keys[] = {"schemaVersion", "bootId", "notificationId", "interactionId", "revision", "outcome"};
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, "outcome");
    const bool valid = cJSON_IsObject(root) && jsonEnd(end, body + length) && exactKeys(root, keys, 6) &&
        notificationVersion(root) && unsignedField(root, "bootId", request.bootId) &&
        unsignedField(root, "notificationId", request.notificationId) &&
        unsignedField(root, "interactionId", request.interactionId) &&
        unsignedField(root, "revision", request.revision) && cJSON_IsString(item) &&
        (!strcmp(item->valuestring, "completed") || !strcmp(item->valuestring, "failed"));
    if (valid) request.completed = !strcmp(item->valuestring, "completed");
    cJSON_Delete(root);
    return valid;
}

constexpr size_t kMaximumConfigBodyBytes = 512;
struct ConfigurationRequest {
    enum class Operation { Preview, Apply, Reset, Observe, PrepareSingleAxis, ManualAction } operation = Operation::Preview;
    uint32_t bootId = 0, baseRevision = 0, observationId = 0, commandId = 0;
    MotionConfiguration value{};
    SingleAxisTestBounds singleAxis{};
    uint32_t singleAxisTravelMs = 0;
    const char* manualAction = nullptr;
    int manualDelta = 0;
    bool manualSupported = false;
};

inline bool parseConfiguration(const char* body, size_t length, ConfigurationRequest& result) {
    result = ConfigurationRequest{};
    if (!flatJson(body, length, kMaximumConfigBodyBytes)) return false;
    const char* end = nullptr;
    cJSON* root = cJSON_ParseWithLengthOpts(body, length, &end, false);
    if (!root) return false;
    while (end < body + length && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) ++end;
    const auto* op = cJSON_GetObjectItemCaseSensitive(root, "op");
    bool valid = end == body + length && cJSON_IsObject(root) && cJSON_IsString(op) &&
        unsignedField(root, "bootId", result.bootId) &&
        unsignedField(root, "baseRevision", result.baseRevision, 0);
    const char* const keys[] = {"op", "bootId", "baseRevision", "expectedYawRaw", "expectedPitchRaw",
        "homeYawRaw", "homePitchRaw", "yawMinRaw", "yawMaxRaw", "pitchMinRaw", "pitchMaxRaw",
        "stepRaw", "nodStepRaw", "travelMs"};
    const char* const observationKeys[] = {"op", "bootId", "baseRevision", "observationId"};
    const char* const singleKeys[] = {"op", "bootId", "baseRevision", "axis", "minimumRaw",
        "maximumRaw", "maximumStepRaw", "travelMs", "expectedYawRaw", "expectedPitchRaw", "reviewed"};
    const char* const manualKeys[] = {"op", "bootId", "baseRevision", "commandId", "deltaRaw"};
    const char* const closeKeys[] = {"op", "bootId", "baseRevision", "commandId", "supportConfirmed"};
    const char* const feedbackKeys[] = {"op", "bootId", "baseRevision", "commandId",
        "supportConfirmed", "expectedYawRaw", "expectedPitchRaw"};
    const bool observing = valid && !strcmp(op->valuestring, "observe");
    const bool reset = valid && !strcmp(op->valuestring, "reset");
    const bool single = valid && !strcmp(op->valuestring, "prepare-single-axis");
    const bool feedbackPower = valid && !strcmp(op->valuestring, "manual-feedback-power");
    const bool manual = valid && (feedbackPower || !strcmp(op->valuestring, "manual-power") ||
        !strcmp(op->valuestring, "manual-enable") || !strcmp(op->valuestring, "manual-move") ||
        !strcmp(op->valuestring, "manual-return") || !strcmp(op->valuestring, "manual-close"));
    if (manual) {
        const bool move = !strcmp(op->valuestring, "manual-move");
        const bool close = !strcmp(op->valuestring, "manual-close");
        valid = (feedbackPower ? exactKeys(root, feedbackKeys, 7) :
            close ? exactKeys(root, closeKeys, 5) : exactKeys(root, manualKeys, move ? 5 : 4)) &&
            unsignedField(root, "commandId", result.commandId);
        if (valid && (close || feedbackPower)) {
            valid = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "supportConfirmed"));
            result.manualSupported = valid;
        }
        if (valid && feedbackPower) {
            uint32_t yaw = 0, pitch = 0;
            valid = unsignedField(root, "expectedYawRaw", yaw, 0) && yaw <= 1000 &&
                unsignedField(root, "expectedPitchRaw", pitch, 0) && pitch <= 1000;
            result.singleAxis.expectedCalibration = {static_cast<int>(yaw), static_cast<int>(pitch)};
        }
        if (valid && move) {
            const auto* delta = cJSON_GetObjectItemCaseSensitive(root, "deltaRaw");
            valid = cJSON_IsNumber(delta) && std::isfinite(delta->valuedouble) &&
                std::floor(delta->valuedouble) == delta->valuedouble &&
                delta->valuedouble >= -1000 && delta->valuedouble <= 1000 && delta->valuedouble != 0;
            if (valid) result.manualDelta = static_cast<int>(delta->valuedouble);
        }
        if (valid) {
            result.operation = ConfigurationRequest::Operation::ManualAction;
            result.manualAction = feedbackPower ? "manual-feedback-power" : move ? "manual-move" :
                !strcmp(op->valuestring, "manual-power") ? "manual-power" :
                !strcmp(op->valuestring, "manual-enable") ? "manual-enable" :
                close ? "manual-close" : "manual-return";
        }
    } else if (single) {
        const auto* axis = cJSON_GetObjectItemCaseSensitive(root, "axis");
        const auto* reviewed = cJSON_GetObjectItemCaseSensitive(root, "reviewed");
        uint32_t minimum = 0, maximum = 0, step = 0, yaw = 0, pitch = 0;
        valid = exactKeys(root, singleKeys, 11) && cJSON_IsString(axis) &&
            (!strcmp(axis->valuestring, "yaw") || !strcmp(axis->valuestring, "pitch")) &&
            cJSON_IsTrue(reviewed) && unsignedField(root, "minimumRaw", minimum, 0) &&
            unsignedField(root, "maximumRaw", maximum, 0) &&
            unsignedField(root, "maximumStepRaw", step) &&
            unsignedField(root, "travelMs", result.singleAxisTravelMs) &&
            unsignedField(root, "expectedYawRaw", yaw, 0) &&
            unsignedField(root, "expectedPitchRaw", pitch, 0) &&
            minimum <= 1000 && maximum <= 1000 && step <= 1000 && yaw <= 1000 && pitch <= 1000;
        if (valid) {
            result.operation = ConfigurationRequest::Operation::PrepareSingleAxis;
            result.singleAxis = {true, !strcmp(axis->valuestring, "yaw") ? SingleAxis::Yaw : SingleAxis::Pitch,
                static_cast<int>(minimum), static_cast<int>(maximum), static_cast<int>(step),
                {static_cast<int>(yaw), static_cast<int>(pitch)}, 0};
        }
    } else if (observing) {
        valid = exactKeys(root, observationKeys, 4) &&
            unsignedField(root, "observationId", result.observationId);
        if (valid) result.operation = ConfigurationRequest::Operation::Observe;
    } else {
        valid = valid && exactKeys(root, keys, reset ? 3 : 14);
        if (valid) {
            result.operation = reset ? ConfigurationRequest::Operation::Reset :
                !strcmp(op->valuestring, "apply") ? ConfigurationRequest::Operation::Apply :
                ConfigurationRequest::Operation::Preview;
            valid = reset || !strcmp(op->valuestring, "apply") || !strcmp(op->valuestring, "preview");
        }
    }
    if (valid && !reset && !observing && !single && !manual) {
        int* fields[] = {&result.value.expectedYawRaw, &result.value.expectedPitchRaw,
            &result.value.homeYawRaw, &result.value.homePitchRaw, &result.value.yawMinRaw,
            &result.value.yawMaxRaw, &result.value.pitchMinRaw, &result.value.pitchMaxRaw,
            &result.value.stepRaw, &result.value.nodStepRaw};
        for (size_t i = 0; i < 10 && valid; ++i) {
            const auto* item = cJSON_GetObjectItemCaseSensitive(root, keys[i + 3]);
            valid = cJSON_IsNumber(item) && std::isfinite(item->valuedouble) &&
                item->valuedouble >= INT32_MIN && item->valuedouble <= INT32_MAX &&
                std::floor(item->valuedouble) == item->valuedouble;
            if (valid) *fields[i] = static_cast<int>(item->valuedouble);
        }
        valid = valid && unsignedField(root, "travelMs", result.value.travelMs, 0);
    }
    cJSON_Delete(root);
    return valid;
}

inline ParsedRequest parseRequest(const char* body, size_t length, bool stop) {
    ParsedRequest result;
    if (!flatJson(body, length)) return result;
    const char* end = nullptr;
    cJSON* root = cJSON_ParseWithLengthOpts(body, length, &end, false);
    if (!root) return result;
    while (end < body + length && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) ++end;
    const bool revision = cJSON_GetObjectItemCaseSensitive(root, "configRevision") != nullptr;
    const char* const keys[] = {"bootId", "commandId", "durationMs", "action", "configRevision"};
    bool valid = end == body + length && cJSON_IsObject(root) &&
        exactKeys(root, keys, stop ? 1 : revision ? 5 : 4) &&
        unsignedField(root, "bootId", result.command.bootId);
    if (!stop) {
        const cJSON* action = cJSON_GetObjectItemCaseSensitive(root, "action");
        valid = valid && unsignedField(root, "commandId", result.command.commandId) &&
            unsignedField(root, "durationMs", result.command.durationMs) && cJSON_IsString(action);
        if (revision) {
            result.command.hasConfigRevision = true;
            valid = valid && unsignedField(root, "configRevision", result.command.configRevision, 0);
        }
        if (valid) result.command.action = actionFor(action->valuestring);
    }
    result.valid = valid;
    cJSON_Delete(root);
    return result;
}

inline bool parseCompanionPresence(const char* body, size_t length, bool& connected) {
    if (!flatJson(body, length)) return false;
    const char* end = nullptr;
    cJSON* root = cJSON_ParseWithLengthOpts(body, length, &end, false);
    if (!root) return false;
    while (end < body + length && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) ++end;
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(root, "connected");
    const bool valid = end == body + length && cJSON_IsObject(root) &&
        cJSON_GetArraySize(root) == 1 && cJSON_IsBool(value);
    if (valid) connected = cJSON_IsTrue(value);
    cJSON_Delete(root);
    return valid;
}

enum class DisplayBrightnessRequestResult { Invalid, OutOfRange, Valid };

inline DisplayBrightnessRequestResult parseDisplayBrightnessRequest(
    const char* body, size_t length, uint8_t& brightness) {
    if (!flatJson(body, length)) return DisplayBrightnessRequestResult::Invalid;
    const char* end = nullptr;
    cJSON* root = cJSON_ParseWithLengthOpts(body, length, &end, false);
    if (!root) return DisplayBrightnessRequestResult::Invalid;
    while (end < body + length && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) ++end;
    const char* const keys[] = {"brightness"};
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, "brightness");
    const bool valid = end == body + length && cJSON_IsObject(root) &&
        exactKeys(root, keys, 1) && cJSON_IsNumber(item) && std::isfinite(item->valuedouble) &&
        std::floor(item->valuedouble) == item->valuedouble;
    if (!valid) {
        cJSON_Delete(root);
        return DisplayBrightnessRequestResult::Invalid;
    }
    if (item->valuedouble < kDisplayBrightnessMinimum ||
        item->valuedouble > kDisplayBrightnessMaximum) {
        cJSON_Delete(root);
        return DisplayBrightnessRequestResult::OutOfRange;
    }
    brightness = static_cast<uint8_t>(item->valuedouble);
    cJSON_Delete(root);
    return DisplayBrightnessRequestResult::Valid;
}

inline bool displayBrightnessJson(
    const DisplayBrightnessStatus& status, char* output, size_t capacity) {
    char requested[8] = "null";
    if (status.pending) {
        const int requestedCount = snprintf(requested, sizeof(requested), "%u",
            static_cast<unsigned>(status.requestedBrightness));
        if (requestedCount < 0 || static_cast<size_t>(requestedCount) >= sizeof(requested)) return false;
    }
    char error[64] = "null";
    if (status.error) {
        const int errorCount = snprintf(error, sizeof(error), "\"%s\"", status.error);
        if (errorCount < 0 || static_cast<size_t>(errorCount) >= sizeof(error)) return false;
    }
    const int count = snprintf(output, capacity,
        "{\"brightness\":%u,\"minimum\":%u,\"maximum\":%u,\"default\":%u,"
        "\"pending\":%s,\"requestedBrightness\":%s,\"persistent\":false,\"error\":%s}",
        static_cast<unsigned>(status.brightness),
        static_cast<unsigned>(kDisplayBrightnessMinimum),
        static_cast<unsigned>(kDisplayBrightnessMaximum),
        static_cast<unsigned>(kDisplayBrightnessDefault),
        status.pending ? "true" : "false", requested, error);
    return count >= 0 && static_cast<size_t>(count) < capacity;
}

inline bool versionJson(char* output, size_t capacity) {
    const int count = snprintf(output, capacity,
        "{\"firmwareVersion\":\"%s\",\"protocolVersion\":%lu}",
        NETBOT_FIRMWARE_VERSION, static_cast<unsigned long>(kVersion));
    return count >= 0 && static_cast<size_t>(count) < capacity;
}

inline bool statusJson(const Snapshot& status, char* output, size_t capacity) {
    char capabilities[512] = "\"neutral\",\"wink\",\"smile\",\"look_up\",\"sleep\",\"wake\","
        "\"warning\",\"important\",\"important_notification\"";
    if (status.sound) strcat(capabilities, ",\"sound\"");
    const bool remoteArmReady = status.motion.armAvailable;
    const bool motionReady = !status.motion.localYawBench &&
        (status.motion.state == MotionState::Ready || status.motion.state == MotionState::Moving);
    if (remoteArmReady) strcat(capabilities, ",\"arm\"");
    if ((status.motion.twoAxisTest || status.motion.checkedDefault) &&
        (status.motion.state == MotionState::Disarmed || status.motion.state == MotionState::Ready))
        strcat(capabilities, ",\"lock\"");
    if (status.motion.torqueReleaseAvailable) strcat(capabilities, ",\"release_torque\"");
    if (motionReady) {
        if (status.motion.remoteYawBench) strcat(capabilities, ",\"move_left\",\"move_right\"");
        else strcat(capabilities, ",\"move_left\",\"move_right\",\"move_up\",\"move_down\",\"home\",\"nod\"");
    }
    const bool messageReady = motionReady && status.sound && !status.quiet;
    if (messageReady) strcat(capabilities, ",\"important_message\"");
    char position[64] = "null";
    if (status.motion.hasPosition)
        snprintf(position, sizeof(position), "{\"yaw\":%d,\"pitch\":%d}", status.motion.position.yaw, status.motion.position.pitch);
    char motionError[80] = "null";
    if (status.motion.error) snprintf(motionError, sizeof(motionError), "\"%s\"", status.motion.error);
    char primaryError[80] = "null";
    if (status.motion.primaryError)
        snprintf(primaryError, sizeof(primaryError), "\"%s\"", status.motion.primaryError);
    char diagnostic[256] = "null";
    const auto& detail = status.motion.diagnostic;
    if (detail.stage) {
        if (!detail.cause) return false;
        const int count = snprintf(diagnostic, sizeof(diagnostic),
            "{\"stage\":\"%s\",\"cause\":\"%s\",\"servoId\":%u,\"register\":%u,"
            "\"transportError\":%d,\"deviceStatus\":%d,\"observed\":%d,\"expected\":%d}",
            detail.stage, detail.cause, static_cast<unsigned>(detail.servoId),
            static_cast<unsigned>(detail.reg), detail.transportError, detail.deviceStatus,
            detail.observed, detail.expected);
        if (count < 0 || static_cast<size_t>(count) >= sizeof(diagnostic)) return false;
    }
    char command[288] = "null";
    char revision[16] = "null";
    if (status.command.hasConfigRevision)
        snprintf(revision, sizeof(revision), "%lu", static_cast<unsigned long>(status.command.configRevision));
    char error[64] = "null";
    if (status.error) {
        const int count = snprintf(error, sizeof(error), "\"%s\"", status.error);
        if (count < 0 || static_cast<size_t>(count) >= sizeof(error)) return false;
    }
    if (status.state != CommandState::None) {
        const int count = snprintf(command, sizeof(command),
            "{\"commandId\":%lu,\"action\":\"%s\",\"durationMs\":%lu,\"state\":\"%s\",\"error\":%s,\"configRevision\":%s}",
            static_cast<unsigned long>(status.command.commandId), actionName(status.command.action),
            static_cast<unsigned long>(status.command.durationMs), stateName(status.state),
            error, revision);
        if (count < 0 || static_cast<size_t>(count) >= sizeof(command)) return false;
    }
    char stopReceipt[128] = "null";
    if (status.stopReceipt.outcome) {
        const int length = snprintf(stopReceipt, sizeof(stopReceipt),
            "{\"bootId\":%lu,\"stopId\":%lu,\"outcome\":\"%s\"}",
            static_cast<unsigned long>(status.stopReceipt.bootId),
            static_cast<unsigned long>(status.stopReceipt.stopId), status.stopReceipt.outcome);
        if (length < 0 || static_cast<size_t>(length) >= sizeof(stopReceipt)) return false;
    }
    const int count = snprintf(output, capacity,
        "{\"version\":1,\"firmwareVersion\":\"%s\",\"bootId\":%lu,\"capabilities\":[%s],\"command\":%s,"
        "\"motion\":{\"state\":\"%s\",\"position\":%s,\"positionUnits\":\"scs_raw\",\"error\":%s,\"mode\":\"%s\",\"diagnostic\":%s,"
        "\"primaryError\":%s,\"stopResult\":\"%s\",\"configRevision\":%lu},\"quiet\":%s,\"stopReceipt\":%s,"
        "\"routines\":[{\"name\":\"important_notification\",\"available\":true,\"requiresMotion\":false},"
        "{\"name\":\"important_message\",\"available\":%s,\"requiresMotion\":true}]}",
        NETBOT_FIRMWARE_VERSION, static_cast<unsigned long>(status.bootId), capabilities, command,
        motionStateName(status.motion.state), position, motionError,
        status.motion.twoAxisTest ? "test-two-axis" : status.motion.remoteYawBench ? "bench-remote-yaw" :
            status.motion.localYawBench ? "bench-local-yaw" : "normal", diagnostic, primaryError,
        status.motion.stopResult, static_cast<unsigned long>(status.configRevision), status.quiet ? "true" : "false",
        stopReceipt,
        messageReady ? "true" : "false");
    return count >= 0 && static_cast<size_t>(count) < capacity;
}

// Connection progress only: never the network name, address, password or setup key.
inline bool networkJson(NetworkState state, bool setup, const char* error, char* output, size_t capacity) {
    char code[48] = "null";
    if (error) {
        const int count = snprintf(code, sizeof(code), "\"%s\"", error);
        if (count < 0 || static_cast<size_t>(count) >= sizeof(code)) return false;
    }
    const int count = snprintf(output, capacity, "{\"state\":\"%s\",\"setup\":%s,\"error\":%s}",
        networkCode(state), setup ? "true" : "false", code);
    return count >= 0 && static_cast<size_t>(count) < capacity;
}

}  // namespace lab

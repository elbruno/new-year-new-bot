// Goal: Report firmware diagnostic events over the USB log sink.
// Responsibilities: Format bounded state, event, and bus observations.
// How it works:
// 1. Callers provide runtime events and snapshots.
// 2. The logger formats best-effort records for the sink.
// 3. Loss counters and optional retained logging expose dropped observations.
#pragma once

#include <stdio.h>
#include <string.h>
#include "LabRuntime.h"
#include "LabRemoteLog.h"

namespace lab {

// Best-effort application events, never ESP-IDF verbose output or USB-avatar protocol traffic.
template <typename Sink>
class UsbLog {
public:
    UsbLog(Sink& sink, const char* version, RemoteLog* retained = nullptr)
        : sink(sink), version(version), retained(retained) {}

    void diagnostic(uint32_t now, uint32_t boot, uint32_t command, const char* event, const char* detail) {
        char line[384];
        const int length = snprintf(line, sizeof(line),
            "NETBOT diagnostic ms=%lu version=%s boot=%lu cmd=%lu event=%s detail=%s dropped=%lu\n",
            ul(now), version, ul(boot), ul(command), event, code(detail), ul(dropped));
        send(line, length, sizeof(line));
    }
    void event(const RuntimeLogEvent& value, uint32_t queueLoss) {
        if (retained) retained->sourceLoss(queueLoss);
        char line[384];
        const int length = snprintf(line, sizeof(line),
            "NETBOT event=%s ms=%lu version=%s boot=%lu cmd=%lu action=%s state=%s error=%s queueLost=%lu dropped=%lu\n",
            value.event, ul(value.atMs), version, ul(value.bootId), ul(value.commandId),
            value.action, value.state, code(value.error), ul(queueLoss), ul(dropped));
        send(line, length, sizeof(line));
    }

    void observe(uint32_t now, const Snapshot& status, const MotionDebugSnapshot& detail,
                 const char* network, bool companion) {
        const auto& motion = status.motion;
        if (!seen || previous.motion.state != motion.state || previous.motion.error != motion.error ||
            previous.motion.stopResult != motion.stopResult || previous.quiet != status.quiet ||
            previousNetwork != network || previousCompanion != companion) {
            const auto& fault = motion.diagnostic;
            char line[512];
            const int length = snprintf(line, sizeof(line),
                "NETBOT state ms=%lu version=%s boot=%lu cmd=%lu motion=%s primary=%s stop=%s "
                "stage=%s cause=%s servo=%u register=%u transport=%d device=%d observed=%d expected=%d "
                "network=%s companion=%u quiet=%u dropped=%lu\n",
                ul(now), version, ul(status.bootId), ul(status.command.commandId), motionStateName(motion.state),
                code(motion.primaryError), motion.stopResult, code(fault.stage), code(fault.cause),
                static_cast<unsigned>(fault.servoId), static_cast<unsigned>(fault.reg),
                fault.transportError, fault.deviceStatus, fault.observed, fault.expected, network,
                companion ? 1u : 0u, status.quiet ? 1u : 0u, ul(dropped));
            send(line, length, sizeof(line));
            previous = status;
            previousNetwork = network;
            previousCompanion = companion;
            seen = true;
        }
        // Drain at most one retained bus record per loop, even after a burst in ARM.
        for (uint8_t i = 0; i < detail.count; ++i) {
            const auto& value = detail.trace[i];
            if (value.sequence <= lastSequence) continue;
            const auto skipped = value.sequence - lastSequence - 1;
            dropped += skipped;
            if (retained) retained->skipTrace(skipped);
            lastSequence = value.sequence;
            char line[384];
            const int length = snprintf(line, sizeof(line),
                "NETBOT bus ms=%lu version=%s boot=%lu cmd=%lu seq=%lu stage=%s op=%s "
                "servo=%u register=%u width=%u value=%d transport=%d device=%d ok=%u dropped=%lu\n",
                ul(value.atMs), version, ul(status.bootId), ul(value.commandId), ul(value.sequence),
                value.stage, value.operation, static_cast<unsigned>(value.servoId),
                static_cast<unsigned>(value.reg), static_cast<unsigned>(value.width), value.value,
                value.transportError, value.deviceStatus, value.ok ? 1u : 0u, ul(dropped));
            send(line, length, sizeof(line));
            break;
        }
    }

    uint32_t lost() const { return dropped; }

private:
    Sink& sink;
    const char* version;
    RemoteLog* retained;
    uint32_t dropped = 0, lastSequence = 0;
    Snapshot previous{};
    const char* previousNetwork = nullptr;
    bool seen = false, previousCompanion = false;
    static unsigned long ul(uint32_t value) { return static_cast<unsigned long>(value); }
    static const char* code(const char* value) { return value ? value : "none"; }
    void send(const char* line, int length, size_t capacity) {
        if (retained) {
            if (length >= 0 && static_cast<size_t>(length) < capacity)
                retained->append(line, static_cast<size_t>(length));
            else retained->lose();
        }
        if (length < 0 || static_cast<size_t>(length) >= capacity ||
            !sink || sink.availableForWrite() < length ||
            sink.write(reinterpret_cast<const uint8_t*>(line), static_cast<size_t>(length)) != static_cast<size_t>(length))
            ++dropped;
    }
};

} // namespace lab

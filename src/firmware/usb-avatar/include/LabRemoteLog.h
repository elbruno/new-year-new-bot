#pragma once

#include <atomic>
#include <mutex>
#include <stdint.h>
#include <string.h>

namespace lab {

inline constexpr uint8_t kRemoteLogCapacity = 32, kRemoteLogPageSize = 8;
inline constexpr size_t kRemoteLogLineBytes = 512;

struct RemoteLogEntry {
    uint32_t seq = 0;
    char line[kRemoteLogLineBytes]{};
};

struct RemoteLogQuery {
    bool bound = false;
    uint32_t bootId = 0, after = 0, limit = kRemoteLogPageSize;
};

struct RemoteLogPage {
    uint32_t bootId = 0, oldestSeq = 0, latestSeq = 0, nextAfter = 0;
    uint32_t overwritten = 0, dropped = 0, queueLost = 0, traceSkipped = 0;
    uint8_t count = 0;
    bool hasMore = false;
    RemoteLogEntry entries[kRemoteLogPageSize]{};
};

inline bool parseRemoteLogQuery(const char* uri, RemoteLogQuery& result) {
    result = {};
    const char* query = strchr(uri, '?');
    if (!query) return true;
    if (!*++query || strlen(query) > 128) return false;
    uint8_t seen = 0;
    while (*query) {
        const char* end = strchr(query, '&');
        if (!end) end = query + strlen(query);
        const char* digits;
        uint8_t field;
        if (!strncmp(query, "bootId=", 7)) { field = 1; digits = query + 7; }
        else if (!strncmp(query, "after=", 6)) { field = 2; digits = query + 6; }
        else if (!strncmp(query, "limit=", 6)) { field = 4; digits = query + 6; }
        else return false;
        if ((seen & field) || digits >= end || (end - digits > 1 && *digits == '0')) return false;
        uint32_t value = 0;
        for (const char* digit = digits; digit < end; ++digit) {
            if (*digit < '0' || *digit > '9' || value > (UINT32_MAX - (*digit - '0')) / 10u) return false;
            value = value * 10u + static_cast<uint32_t>(*digit - '0');
        }
        if (field == 1) result.bootId = value;
        else if (field == 2) result.after = value;
        else result.limit = value;
        seen |= field;
        if (!*end) break;
        query = end + 1;
        if (!*query) return false;
    }
    result.bound = true;
    return seen == 7 && result.bootId && result.limit >= 1 && result.limit <= kRemoteLogPageSize;
}

class RemoteLog {
public:
    // Called before starting HTTP, with the current boot, never by a log reader.
    void reset(uint32_t value) {
        std::lock_guard<std::mutex> lock(mutex);
        bootId = value;
        head = count = 0;
        latest = overwritten = 0;
        dropped.store(0);
        queueLost.store(0);
        traceSkipped.store(0);
    }

    void lose() { increment(dropped, 1); }
    void sourceLoss(uint32_t value) { queueLost.store(value); }
    void skipTrace(uint32_t value) { increment(traceSkipped, value); }

    void append(const char* line, size_t length) {
        if (length && line[length - 1] == '\n') --length;
        if (!length || length >= kRemoteLogLineBytes) { lose(); return; }
        for (size_t i = 0; i < length; ++i) {
            if (static_cast<unsigned char>(line[i]) < 32 ||
                static_cast<unsigned char>(line[i]) > 126) { lose(); return; }
        }
        // HTTP may copy a bounded page, but cannot make the control loop wait.
        std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
        if (!lock.owns_lock() || !bootId || latest == UINT32_MAX) { lose(); return; }
        auto& entry = entries[head];
        entry.seq = ++latest;
        memcpy(entry.line, line, length);
        entry.line[length] = '\0';
        head = (head + 1) % kRemoteLogCapacity;
        if (count < kRemoteLogCapacity) ++count;
        else if (overwritten < UINT32_MAX) ++overwritten;
    }

    const char* read(const RemoteLogQuery& query, RemoteLogPage& page) {
        page = {};
        std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
        if (!lock.owns_lock()) return "log_unavailable";
        page.bootId = bootId;
        page.oldestSeq = count ? entries[oldest()].seq : 0;
        page.latestSeq = latest;
        page.overwritten = overwritten;
        page.dropped = dropped.load();
        page.queueLost = queueLost.load();
        page.traceSkipped = traceSkipped.load();
        if (!bootId) return "log_unavailable";
        if (query.bound && query.bootId != bootId) return "boot_changed";
        if (latest == UINT32_MAX) return "log_sequence_exhausted";
        if (query.after > latest) return "log_cursor_ahead";
        if (query.bound && count && query.after < page.oldestSeq - 1) return "log_cursor_expired";
        for (uint8_t i = 0; i < count; ++i) {
            const auto& entry = entries[(oldest() + i) % kRemoteLogCapacity];
            if (entry.seq <= query.after) continue;
            if (page.count == query.limit) { page.hasMore = true; break; }
            page.entries[page.count++] = entry;
        }
        page.nextAfter = page.hasMore ? page.entries[page.count - 1].seq : latest;
        return nullptr;
    }

private:
    std::mutex mutex;
    uint32_t bootId = 0, latest = 0, overwritten = 0;
    std::atomic<uint32_t> dropped{0}, queueLost{0}, traceSkipped{0};
    uint8_t head = 0, count = 0;
    RemoteLogEntry entries[kRemoteLogCapacity]{};
    uint8_t oldest() const { return (head + kRemoteLogCapacity - count) % kRemoteLogCapacity; }
    static void increment(std::atomic<uint32_t>& counter, uint32_t amount) {
        auto value = counter.load();
        while (!counter.compare_exchange_weak(value, amount > UINT32_MAX - value ? UINT32_MAX : value + amount)) {}
    }
};

static_assert(sizeof(RemoteLog) + sizeof(RemoteLogPage) <= 24 * 1024, "Remote logs exceed the fixed RAM budget.");

} // namespace lab

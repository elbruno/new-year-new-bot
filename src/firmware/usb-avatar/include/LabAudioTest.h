#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "LabRemoteLog.h"

namespace lab {

inline bool audioTestFilename(const char* name) {
    if (!name) return false;
    const size_t length = strlen(name);
    if (length < 5 || length > 64 || strcmp(name + length - 4, ".wav") || strstr(name, ".."))
        return false;
    for (size_t i = 0; i < length - 4; ++i) {
        const char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
    }
    return name[0] != '.';
}

constexpr size_t kAudioFileLimit = 16;
constexpr size_t kAudioEntryLimit = 64;

struct AudioFileList {
    char names[kAudioFileLimit][65]{};
    size_t count = 0;
    size_t inspected = 0;
    bool truncated = false;
    const char* error = nullptr;

    bool add(const char* name, bool directory) {
        if (inspected == kAudioEntryLimit) { truncated = true; return false; }
        ++inspected;
        if (!directory && audioTestFilename(name)) {
            if (count == kAudioFileLimit) { truncated = true; return false; }
            strcpy(names[count++], name);
        }
        return true;
    }
};

inline uint16_t audioLe16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}
inline uint32_t audioLe32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
        (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
inline bool audioTestWav(const uint8_t* data, size_t size) {
    if (!data || size < 48 || size > 256 * 1024 ||
        memcmp(data, "RIFF", 4) || audioLe32(data + 4) != size - 8 ||
        memcmp(data + 8, "WAVEfmt ", 8) || audioLe32(data + 16) != 16 ||
        audioLe16(data + 20) != 1 || audioLe16(data + 22) != 2 ||
        audioLe32(data + 24) != 44100 || audioLe32(data + 28) != 176400 ||
        audioLe16(data + 32) != 4 || audioLe16(data + 34) != 16 ||
        memcmp(data + 36, "data", 4)) return false;
    const uint32_t bytes = audioLe32(data + 40);
    return bytes && bytes % 4 == 0 && bytes == size - 44;
}

class AudioTestBackend {
public:
    virtual ~AudioTestBackend() = default;
    virtual bool load(const char* name) = 0;
    virtual bool loaded(uint8_t*& data, size_t& size, const char*& error) = 0;
    virtual void cancelLoad() = 0;
    virtual void release(uint8_t* data) = 0;
    virtual bool busy() const = 0;
    virtual bool play(const uint8_t* data, size_t size) = 0;
    virtual bool released() const = 0;
    virtual void settleCanceled() = 0;
    virtual bool startFileList() { return false; }
    virtual bool fileListRunning() const { return false; }
    virtual bool takeFileList(AudioFileList&) { return false; }
};

// All methods except the speaker's release callback are called under Device::mutex.
class AudioTest {
public:
    explicit AudioTest(AudioTestBackend& backend) : backend(backend) {}
    RemoteLog* log = nullptr;

    bool active() const {
        return state == Queued || state == Loading || state == CancelingLoad ||
            state == Playing || state == Canceling;
    }
    const char* stateName() const {
        switch (state) {
            case Queued: return "queued";
            case Loading: return "loading";
            case CancelingLoad: return "canceling";
            case Playing: return "playing";
            case Canceling: return "canceling";
            case Complete: return "completed";
            case Canceled: return "canceled";
            case Failed: return "failed";
            default: return "idle";
        }
    }
    const char* error() const { return lastError; }
    uint32_t id() const { return requestId; }
    bool failed() const { return state == Failed; }
    bool resourceBusy() const { return active() || backend.fileListRunning(); }
    bool startFileList() { return backend.startFileList(); }
    bool fileListRunning() const { return backend.fileListRunning(); }
    bool takeFileList(AudioFileList& result) { return backend.takeFileList(result); }
    bool speakerBusy() const { return backend.busy(); }

    void logRejection(const char* value, const char* reason, uint32_t now) {
        if (!log) return;
        char line[192];
        const int n = snprintf(line, sizeof(line), "[%lu] AUDIO_TEST ERROR file=%s error=%s",
            static_cast<unsigned long>(now), value, reason);
        if (n > 0 && static_cast<size_t>(n) < sizeof(line)) log->append(line, static_cast<size_t>(n));
        else log->lose();
    }

    const char* request(const char* value, bool quiet, uint32_t now) {
        if (!audioTestFilename(value)) return "invalid_filename";
        if (quiet) return "quiet";
        if (active() || backend.busy()) return "audio_busy";
        if (requestId == UINT32_MAX) return "audio_ids_exhausted";
        ++requestId;
        strcpy(filename, value);
        lastError = nullptr;
        state = Queued;
        event(now, "REQUEST");
        return nullptr;
    }

    void cancel(uint32_t now) {
        if (!active()) return;
        if (state == Loading || state == CancelingLoad) {
            backend.cancelLoad();
            state = CancelingLoad;
            return;
        }
        if (state == Playing || state == Canceling) {
            state = Canceling;
            return;
        }
        cleanup();
        state = Canceled;
        event(now, "CANCELED");
    }

    void tick(uint32_t now) {
        if (state == Queued) {
            if (backend.busy()) return fail(now, "audio_busy");
            if (!backend.load(filename)) return fail(now, "audio_task_unavailable");
            state = Loading;
            event(now, "LOADING");
        }
        if (state == Loading || state == CancelingLoad) {
            const char* loadError = nullptr;
            if (!backend.loaded(bytes, length, loadError)) return;
            if (state == CancelingLoad) {
                cleanup();
                state = Canceled;
                event(now, "CANCELED");
                return;
            }
            if (loadError) return fail(now, loadError);
            if (!audioTestWav(bytes, length)) return fail(now, "wav_unsupported");
            if (backend.busy()) return fail(now, "audio_busy");
            if (!backend.play(bytes, length)) return fail(now, "playback_failed");
            state = Playing;
            event(now, "START");
        }
        if (state == Playing && !backend.busy() && backend.released()) {
            cleanup();
            state = Complete;
            event(now, "COMPLETE");
        } else if (state == Canceling && !backend.busy()) {
            if (!backend.released()) backend.settleCanceled();
            cleanup();
            state = Canceled;
            event(now, "CANCELED");
        }
    }

private:
    enum State { Idle, Queued, Loading, CancelingLoad, Playing, Canceling, Complete, Canceled, Failed } state = Idle;
    AudioTestBackend& backend;
    char filename[65]{};
    uint8_t* bytes = nullptr;
    size_t length = 0;
    uint32_t requestId = 0;
    const char* lastError = nullptr;

    void cleanup() {
        if (bytes) backend.release(bytes);
        bytes = nullptr;
        length = 0;
    }
    void fail(uint32_t now, const char* reason) {
        cleanup();
        state = Failed;
        lastError = reason;
        event(now, "ERROR");
    }
    void event(uint32_t now, const char* what) {
        if (!log) return;
        char line[192];
        const int n = snprintf(line, sizeof(line), "[%lu] AUDIO_TEST %s id=%lu file=%s%s%s",
            static_cast<unsigned long>(now), what, static_cast<unsigned long>(requestId), filename,
            lastError ? " error=" : "", lastError ? lastError : "");
        if (n > 0 && static_cast<size_t>(n) < sizeof(line)) log->append(line, static_cast<size_t>(n));
        else log->lose();
    }
};

} // namespace lab

// Goal: Coordinate display of supported images from device storage.
// Responsibilities: Validate image files and track image display requests.
// How it works:
// 1. File names and JPEG data are checked against supported limits.
// 2. A backend loads and displays an admitted image.
// 3. Request state and errors are exposed to callers.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace lab {

inline bool imageFilename(const char* name) {
    if (!name) return false;
    const size_t length = strlen(name);
    if (length < 5 || length > 64 || strcmp(name + length - 4, ".jpg") ||
        name[0] == '.' || strstr(name, "..")) return false;
    for (size_t i = 0; i < length - 4; ++i) {
        const char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
    }
    return true;
}

constexpr size_t kImageFileLimit = 16;
constexpr size_t kImageEntryLimit = 64;
constexpr size_t kImageByteLimit = 256 * 1024;

struct ImageFileList {
    char names[kImageFileLimit][65]{};
    size_t count = 0, inspected = 0;
    bool truncated = false;
    const char* error = nullptr;

    bool add(const char* name, bool directory, bool compatible) {
        if (inspected == kImageEntryLimit) { truncated = true; return false; }
        ++inspected;
        if (!directory && compatible && imageFilename(name)) {
            if (count == kImageFileLimit) { truncated = true; return false; }
            strcpy(names[count++], name);
        }
        return true;
    }
};

inline bool imageJpeg(const uint8_t* bytes, size_t size) {
    if (!bytes || size < 12 || size > kImageByteLimit ||
        bytes[0] != 0xff || bytes[1] != 0xd8 ||
        bytes[size - 2] != 0xff || bytes[size - 1] != 0xd9) return false;
    bool frame = false;
    size_t offset = 2;
    while (offset + 3 < size) {
        if (bytes[offset++] != 0xff) return false;
        while (offset < size && bytes[offset] == 0xff) ++offset;
        if (offset >= size) return false;
        const uint8_t marker = bytes[offset++];
        if (marker == 0xd9 || marker == 0xd8 || marker == 0 || marker == 0x01 ||
            (marker >= 0xd0 && marker <= 0xd7)) return false;
        if (offset + 2 > size) return false;
        const size_t length = (static_cast<size_t>(bytes[offset]) << 8) | bytes[offset + 1];
        if (length < 2 || length > size - offset) return false;
        if (marker == 0xc0) {
            if (frame || length < 11 || bytes[offset + 2] != 8 ||
                bytes[offset + 3] != 0 || bytes[offset + 4] != 240 ||
                bytes[offset + 5] != 1 || bytes[offset + 6] != 64 ||
                (bytes[offset + 7] != 1 && bytes[offset + 7] != 3) ||
                length != static_cast<size_t>(8 + 3 * bytes[offset + 7])) return false;
            frame = true;
        } else if (marker >= 0xc1 && marker <= 0xcf && marker != 0xc4) {
            return false;
        }
        if (marker == 0xda) return frame && offset + length < size - 2;
        offset += length;
    }
    return false;
}

class ImageDisplayBackend {
public:
    virtual ~ImageDisplayBackend() = default;
    virtual bool startLoad(const char* name) = 0;
    virtual bool takeLoad(const char*& error) = 0;
    virtual void cancelLoad() = 0;
    virtual bool present() = 0;
    virtual void release() = 0;
    virtual bool startFileList() = 0;
    virtual bool fileListRunning() const = 0;
    virtual bool takeFileList(ImageFileList& result) = 0;
};

class ImageDisplay {
public:
    explicit ImageDisplay(ImageDisplayBackend& backend) : backend(backend) {}
    bool active() const { return state == Queued || state == Loading ||
        state == Canceling || state == Ready || state == Rendering || state == Displaying; }
    bool resourceBusy() const { return active() || backend.fileListRunning(); }
    bool fileListRunning() const { return backend.fileListRunning(); }
    bool startFileList() { return backend.startFileList(); }
    bool takeFileList(ImageFileList& result) { return backend.takeFileList(result); }
    uint32_t id() const { return requestId; }
    const char* error() const { return lastError; }
    const char* stateName() const {
        switch (state) {
            case Queued: return "queued";
            case Loading: case Ready: case Rendering: return "loading";
            case Canceling: return "canceling";
            case Displaying: return "displaying";
            case Complete: return "completed";
            case Canceled: return "canceled";
            case Failed: return "failed";
            default: return "idle";
        }
    }

    const char* request(const char* name, bool eligible, bool busy, uint32_t stopEpoch) {
        if (!imageFilename(name)) return "invalid_filename";
        if (!eligible) return "display_unavailable";
        if (busy || resourceBusy()) return "image_busy";
        if (requestId == UINT32_MAX) return "image_ids_exhausted";
        ++requestId;
        strcpy(filename, name);
        requestStopEpoch = stopEpoch;
        lastError = nullptr;
        state = Queued;
        return nullptr;
    }

    // Called with the device lock; present() runs separately without it.
    bool tick(uint32_t now, bool eligible, uint32_t stopEpoch) {
        if (active() && (!eligible || stopEpoch != requestStopEpoch)) cancel();
        if (state == Queued) {
            if (!backend.startLoad(filename)) { fail("image_task_unavailable"); return false; }
            state = Loading;
        }
        if (state == Loading || state == Canceling) {
            const char* loadError = nullptr;
            if (!backend.takeLoad(loadError)) return false;
            if (state == Canceling) {
                backend.release();
                state = Canceled;
                return false;
            }
            if (loadError) { backend.release(); fail(loadError); return false; }
            state = Ready;
        }
        if (state == Ready) { state = Rendering; return true; }
        if (state == Displaying && static_cast<uint32_t>(now - displayedAt) >= 5000) {
            backend.release();
            state = Complete;
            restore = true;
        }
        return false;
    }

    bool present() { return backend.present(); }
    void presented(uint32_t now, bool success, bool eligible, uint32_t stopEpoch) {
        if (state != Rendering) return;
        if (!eligible || stopEpoch != requestStopEpoch) {
            backend.release();
            state = Canceled;
            restore = true;
        } else if (!success) {
            backend.release();
            fail("image_render_failed");
            restore = true;
        } else {
            displayedAt = now;
            state = Displaying;
        }
    }
    bool ownsDisplay() const { return state == Displaying; }
    bool takeRestore() { const bool value = restore; restore = false; return value; }

private:
    enum State { Idle, Queued, Loading, Canceling, Ready, Rendering, Displaying,
                 Complete, Canceled, Failed } state = Idle;
    ImageDisplayBackend& backend;
    char filename[65]{};
    uint32_t requestId = 0, requestStopEpoch = 0, displayedAt = 0;
    const char* lastError = nullptr;
    bool restore = false;

    void cancel() {
        if (state == Loading || state == Canceling) {
            backend.cancelLoad();
            state = Canceling;
        } else if (state == Ready || state == Displaying) {
            if (state == Displaying) restore = true;
            backend.release();
            state = Canceled;
        } else if (state == Queued) {
            state = Canceled;
        }
    }
    void fail(const char* error) { lastError = error; state = Failed; }
};

} // namespace lab

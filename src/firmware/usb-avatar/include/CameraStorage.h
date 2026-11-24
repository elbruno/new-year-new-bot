// Goal: Save captured camera bytes and verify their storage.
// Responsibilities: Handle bounded writes, cancellation, and readback.
// How it works:
// 1. The helper creates a file and writes the image in chunks.
// 2. It syncs and closes the output, preserving uncertain failures.
// 3. It reads the file back and compares it with the original bytes.
#pragma once
#include "CameraProducer.h"

namespace lab {
template<class Files>
CameraSave saveCameraBytes(Files& files, const char* path, const uint8_t* bytes, size_t length,
                           const std::atomic<bool>& cancelled) {
    if (cancelled.load()) return {"sd_write_failed", false};
    const int output = files.create(path);
    if (output < 0) return {files.full() ? "sd_full" : "sd_write_failed", !files.knownRefusal()};
    const char* error = nullptr;
    bool cancelledWrite = false;
    for (size_t offset = 0; offset < length && !error;) {
        const size_t count = std::min<size_t>(1024, length - offset);
        if (cancelled.load()) { error = "sd_write_failed"; cancelledWrite = true; }
        else {
            const int written = files.write(output, bytes + offset, count);
            if (written != static_cast<int>(count))
                error = written < 0 && files.full() ? "sd_full" : "sd_write_failed";
            else offset += count;
        }
        files.yield();
    }
    if (cancelledWrite || (!error && cancelled.load()))
        return {"sd_write_failed", !files.close(output)};
    if (error || !files.sync(output)) {
        files.retain(output); // FatFS close would sync again after a failed/partial write.
        return {error ? error : "sd_write_failed", true};
    }
    if (!files.close(output)) return {error ? error : "sd_write_failed", true};
    if (cancelled.load()) return {"sd_readback_failed", false};
    const int input = files.open(path);
    if (input < 0) return {"sd_readback_failed", false};
    uint8_t block[1024];
    for (size_t offset = 0; offset < length && !error;) {
        const size_t count = std::min<size_t>(sizeof(block), length - offset);
        if (cancelled.load() || files.read(input, block, count) != static_cast<int>(count) ||
                memcmp(bytes + offset, block, count))
            error = "sd_readback_failed";
        else offset += count;
        files.yield();
    }
    if (!error && files.read(input, block, 1) != 0) error = "sd_readback_failed";
    if (!files.close(input)) return {"sd_readback_failed", true};
    return {error, false};
}
} // namespace lab

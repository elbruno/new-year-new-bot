#pragma once
#include <esp_camera.h>
#include <img_converters.h>
#include "CameraProducer.h"

namespace lab {
inline CameraImage encodeCameraFrame(camera_fb_t* frame, const std::atomic<bool>& cancelled) {
    CameraImage output;
    if (!frame) return {nullptr, 0, "capture_failed", false};
    const bool valid = frame->buf && frame->format == PIXFORMAT_RGB565 &&
        frame->width == 320 && frame->height == 240 && frame->len == 320 * 240 * 2;
    bool encoded = false;
    if (valid && !cancelled.load()) encoded = frame2jpg(frame, 80, &output.bytes, &output.length);
    esp_camera_fb_return(frame);
    if (!valid) output.error = "jpeg_invalid";
    else if (cancelled.load() || !encoded) output.error = "capture_failed";
    else if (output.length >= kCameraJpegCapacity) output.error = "jpeg_capacity";
    else if (!output.bytes || output.length < 4 || output.bytes[0] != 0xFF ||
             output.bytes[1] != 0xD8 || output.bytes[output.length - 2] != 0xFF ||
             output.bytes[output.length - 1] != 0xD9) output.error = "jpeg_invalid";
    return output;
}
} // namespace lab

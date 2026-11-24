// Goal: Connect camera capture and storage to device hardware.
// Responsibilities: Implement the camera backend and file operations.
// How it works:
// 1. The backend initializes the board and captures a camera frame.
// 2. Camera helpers encode the image and save it through storage.
// 3. Background task helpers run work for the camera producer.
#pragma once
#include <SD.h>
#include <SPI.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <esp_vfs_fat.h>
#include "CameraEncoding.h"
#include "CameraSccb.h"
#include "CameraStorage.h"

namespace lab {
class CameraFiles {
public:
    int unresolved = -1;
    int create(const char* path) { return ::open(path, O_WRONLY | O_CREAT | O_EXCL, 0600); }
    int open(const char* path) { return ::open(path, O_RDONLY); }
    int write(int fd, const uint8_t* data, size_t count) { return ::write(fd, data, count); }
    int read(int fd, uint8_t* data, size_t count) { return ::read(fd, data, count); }
    bool sync(int fd) { return fsync(fd) == 0; }
    bool close(int fd) {
        if (::close(fd) == 0) return true;
        unresolved = fd;
        return false;
    }
    bool full() const { return errno == ENOSPC; }
    bool knownRefusal() const { return errno == ENOSPC || errno == EEXIST || errno == EMFILE || errno == ENFILE; }
    void retain(int fd) { unresolved = fd; }
    void yield() { vTaskDelay(1); }
};

class CameraHardware : public CameraBackend {
public:
    CameraImage capture(const std::atomic<bool>& cancelled) override {
        if (!board()) return {nullptr, 0, "camera_unavailable", false};
        cameraSccb.begin(cancelled);
        if (!cameraSccb.supply(4, 0x94) || !cameraSccb.reset(false)) return unavailable(cancelled);
        vTaskDelay(pdMS_TO_TICKS(10));
        if (!cameraSccb.reset(true)) return unavailable(cancelled);
        vTaskDelay(pdMS_TO_TICKS(10));
        camera_config_t config{};
        config.pin_pwdn = config.pin_reset = config.pin_xclk = -1;
        config.pin_sccb_sda = config.pin_sccb_scl = -1;
        config.sccb_i2c_port = M5.In_I2C.getPort();
        config.pin_d0 = 39; config.pin_d1 = 40; config.pin_d2 = 41; config.pin_d3 = 42;
        config.pin_d4 = 15; config.pin_d5 = 16; config.pin_d6 = 48; config.pin_d7 = 47;
        config.pin_vsync = 46; config.pin_href = 38; config.pin_pclk = 45;
        config.xclk_freq_hz = 20000000;
        config.ledc_timer = LEDC_TIMER_0;
        config.ledc_channel = LEDC_CHANNEL_0;
        config.pixel_format = PIXFORMAT_RGB565;
        config.frame_size = FRAMESIZE_QVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_PSRAM;
        config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
        if (!cameraSccb.ready()) return unavailable(cancelled);
        cameraUnsettled = true;
        if (esp_camera_init(&config) != ESP_OK) {
            // The selected archive discards its internal deinit result on this failure tail.
            return unavailable(cancelled);
        }
        CameraImage result;
        sensor_t* sensor = esp_camera_sensor_get();
        if (!sensor || sensor->id.PID != 0x9B || cameraSccb.failed || cancelled.load())
            result.error = "capture_failed";
        else result = encodeCameraFrame(esp_camera_fb_get(), cancelled);
        if (esp_camera_deinit() == ESP_OK) cameraUnsettled = false;
        else result.error = "capture_failed";
        if (cameraSccb.failed) result.error = "capture_failed";
        result.uncertain = cameraSccb.uncertain || cameraUnsettled;
        cameraSccb.end();
        return result;
    }
    CameraSave save(uint32_t boot, uint32_t capture, const uint8_t* bytes, size_t length,
                    const std::atomic<bool>& cancelled) override {
        if (!board() || cancelled.load()) return {"sd_missing", false};
        cameraSccb.begin(cancelled);
        const bool powered = cameraSccb.supply(8, 0x95);
        const bool uncertain = cameraSccb.uncertain;
        cameraSccb.end();
        if (!powered) return {"sd_missing", uncertain};
        if (!SD.begin(4, SPI, 4000000, "/sdcard", 2, false)) return {"sd_missing", false};
        uint64_t total = 0, available = 0;
        if (esp_vfs_fat_info("/sdcard", &total, &available) != ESP_OK)
            return {"sd_write_failed", false};
        if (available < length) return {"sd_full", false};
        char path[64];
        snprintf(path, sizeof(path), "/sdcard/netbot-%08lx-%08lx.jpg",
            static_cast<unsigned long>(boot), static_cast<unsigned long>(capture));
        return saveCameraBytes(files, path, bytes, length, cancelled);
    }
private:
    bool cameraUnsettled = false;
    CameraFiles files;
    static bool board() {
        const auto type = M5.getBoard();
        return type == m5::board_t::board_M5StackCoreS3 || type == m5::board_t::board_M5StackChan;
    }
    CameraImage unavailable(const std::atomic<bool>& cancelled) {
        const bool unknown = cameraSccb.uncertain || cameraUnsettled;
        cameraSccb.end();
        return {nullptr, 0, cancelled.load() ? "capture_failed" : "camera_unavailable", unknown};
    }
};

inline bool launchCameraTask(void (*job)(void*), void* context) {
    return xTaskCreate(job, "camera-still", 8192, context, 1, nullptr) == pdPASS;
}
inline void finishCameraTask() { vTaskDelete(nullptr); }
} // namespace lab

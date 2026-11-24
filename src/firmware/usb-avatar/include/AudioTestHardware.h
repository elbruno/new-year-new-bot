// Goal: Connect the audio test to the device's storage and speaker.
// Responsibilities: Load and list audio files and provide playback resources.
// How it works:
// 1. Background tasks inspect storage or load a requested file.
// 2. The audio test consumes the result through its backend interface.
// 3. Playback and resource cleanup are coordinated with the device.
#pragma once

#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>
#include <esp_heap_caps.h>
#include <atomic>
#include "LabAudioTest.h"

namespace lab {
class AudioTestHardware : public AudioTestBackend {
public:
    bool (*otherResourceBusy)() = nullptr;
    void initialize() {
        M5.Speaker.setBufferReleaseCallback(this, [](void* context, const void* data, uint8_t) {
            auto& self = *static_cast<AudioTestHardware*>(context);
            if (data == self.expected.load(std::memory_order_acquire))
                self.bufferReleased.store(true, std::memory_order_release);
        });
    }
    bool load(const char* name) override {
        strcpy(filename, name);
        resultBytes = nullptr;
        resultSize = 0;
        resultError = nullptr;
        canceled.store(false, std::memory_order_relaxed);
        ready.store(false, std::memory_order_relaxed);
        return xTaskCreate(loadTask, "audio-sd", 4096, this, 1, nullptr) == pdPASS;
    }
    bool loaded(uint8_t*& data, size_t& size, const char*& error) override {
        if (!ready.load(std::memory_order_acquire)) return false;
        data = resultBytes;
        size = resultSize;
        error = resultError;
        resultBytes = nullptr;
        return true;
    }
    void cancelLoad() override { canceled.store(true, std::memory_order_release); }
    void release(uint8_t* data) override {
        expected.store(nullptr, std::memory_order_release);
        free(data);
    }
    bool busy() const override {
        return M5.Speaker.isPlaying() || fileListRunning() ||
            (otherResourceBusy && otherResourceBusy());
    }
    bool play(const uint8_t* data, size_t size) override {
        if (!M5.Speaker.isEnabled() || !M5.Speaker.isRunning()) return false;
        bufferReleased.store(false, std::memory_order_release);
        expected.store(data + 44, std::memory_order_release);
        return M5.Speaker.playWav(data, size, 1, -1, false);
    }
    bool released() const override { return bufferReleased.load(std::memory_order_acquire); }
    void settleCanceled() override {
        // A stopped request discarded before playback has no release callback.
        // end() joins the speaker task, so it cannot retain the WAV pointer.
        M5.Speaker.end();
        M5.Speaker.begin();
    }
    bool startFileList() override {
        int expectedState = 0;
        if (!listState.compare_exchange_strong(expectedState, 1)) return false;
        if (xTaskCreate(listTask, "audio-list", 4096, this, 1, nullptr) == pdPASS) return true;
        listState.store(0, std::memory_order_release);
        return false;
    }
    bool fileListRunning() const override { return listState.load(std::memory_order_acquire) == 1; }
    bool takeFileList(AudioFileList& result) override {
        if (listState.load(std::memory_order_acquire) != 2) return false;
        result = fileList;
        listState.store(0, std::memory_order_release);
        return true;
    }
private:
    char filename[65]{};
    uint8_t* resultBytes = nullptr;
    size_t resultSize = 0;
    const char* resultError = nullptr;
    std::atomic<bool> ready{false}, canceled{false};
    std::atomic<const void*> expected{nullptr};
    std::atomic<bool> bufferReleased{false};
    std::atomic<int> listState{0};
    AudioFileList fileList{};

    static void listTask(void* context) {
        auto& self = *static_cast<AudioTestHardware*>(context);
        self.readFileList();
        self.listState.store(2, std::memory_order_release);
        vTaskDelete(nullptr);
    }
    void readFileList() {
        fileList = {};
        const auto board = M5.getBoard();
        if (board != m5::board_t::board_M5StackCoreS3 && board != m5::board_t::board_M5StackChan) {
            fileList.error = "sd_unavailable"; return;
        }
        SPI.begin(36, 35, 37, 4);
        if (!SD.begin(4, SPI, 4000000, "/sdcard", 2, false) || SD.cardType() == CARD_NONE) {
            fileList.error = "sd_unavailable"; return;
        }
        if (!SD.exists("/audio")) return;
        File directory = SD.open("/audio", FILE_READ);
        if (!directory || !directory.isDirectory()) {
            directory.close(); fileList.error = "audio_directory_unavailable"; return;
        }
        while (true) {
            File entry = directory.openNextFile();
            if (!entry) break;
            const char* name = entry.name();
            const char* slash = name ? strrchr(name, '/') : nullptr;
            const bool more = fileList.add(slash ? slash + 1 : name, entry.isDirectory());
            entry.close();
            if (!more) break;
            vTaskDelay(1);
        }
        directory.close();
    }
    static void loadTask(void* context) {
        auto& self = *static_cast<AudioTestHardware*>(context);
        self.readFile();
        self.ready.store(true, std::memory_order_release);
        vTaskDelete(nullptr);
    }
    void readFile() {
        const auto board = M5.getBoard();
        if (board != m5::board_t::board_M5StackCoreS3 && board != m5::board_t::board_M5StackChan) {
            resultError = "sd_unavailable"; return;
        }
        SPI.begin(36, 35, 37, 4);
        if (!SD.begin(4, SPI, 4000000, "/sdcard", 2, false) || SD.cardType() == CARD_NONE) {
            resultError = "sd_unavailable"; return;
        }
        char path[72];
        snprintf(path, sizeof(path), "/audio/%s", filename);
        if (!SD.exists(path)) { resultError = "wav_not_found"; return; }
        File file = SD.open(path, FILE_READ);
        if (!file || file.isDirectory()) { file.close(); resultError = "audio_read_failed"; return; }
        const size_t size = file.size();
        if (size < 48 || size > 256 * 1024) {
            file.close(); resultError = "wav_unsupported"; return;
        }
        uint8_t* data = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!data) { file.close(); resultError = "audio_memory_unavailable"; return; }
        size_t offset = 0;
        while (offset < size && !canceled.load(std::memory_order_acquire)) {
            const size_t remaining = size - offset;
            const size_t count = remaining < 4096 ? remaining : 4096;
            if (file.read(data + offset, count) != count) { resultError = "audio_read_failed"; break; }
            offset += count;
            vTaskDelay(1);
        }
        file.close();
        if (resultError || canceled.load(std::memory_order_acquire)) { free(data); return; }
        resultBytes = data;
        resultSize = size;
    }
};
} // namespace lab

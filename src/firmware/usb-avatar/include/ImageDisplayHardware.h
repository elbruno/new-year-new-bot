// Goal: Connect image display requests to device storage and display hardware.
// Responsibilities: List and load image files for the display backend.
// How it works:
// 1. Background tasks mount storage and inspect or load files.
// 2. Loaded image data is provided to the image display state machine.
// 3. The backend coordinates drawing and cleanup.
#pragma once

#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>
#include <esp_heap_caps.h>
#include <atomic>
#include "LabImageDisplay.h"

namespace lab {

class ImageDisplayHardware : public ImageDisplayBackend {
public:
    ImageDisplayHardware() : sprite(&M5.Display) {}

    bool startLoad(const char* name) override {
        strcpy(filename, name);
        resultError = nullptr;
        canceled.store(false, std::memory_order_relaxed);
        ready.store(false, std::memory_order_relaxed);
        return xTaskCreate(loadTask, "image-sd", 8192, this, 1, nullptr) == pdPASS;
    }
    bool takeLoad(const char*& error) override {
        if (!ready.load(std::memory_order_acquire)) return false;
        error = resultError;
        return true;
    }
    void cancelLoad() override { canceled.store(true, std::memory_order_release); }
    bool present() override {
        if (!sprite.getBuffer()) return false;
        sprite.pushSprite(0, 0);
        return true;
    }
    void release() override { sprite.deleteSprite(); }

    bool startFileList() override {
        int expected = 0;
        if (!listState.compare_exchange_strong(expected, 1)) return false;
        if (xTaskCreate(listTask, "image-list", 8192, this, 1, nullptr) == pdPASS) return true;
        listState.store(0, std::memory_order_release);
        return false;
    }
    bool fileListRunning() const override { return listState.load(std::memory_order_acquire) == 1; }
    bool takeFileList(ImageFileList& result) override {
        if (listState.load(std::memory_order_acquire) != 2) return false;
        result = fileList;
        listState.store(0, std::memory_order_release);
        return true;
    }

private:
    M5Canvas sprite;
    char filename[65]{};
    const char* resultError = nullptr;
    std::atomic<bool> ready{false}, canceled{false};
    std::atomic<int> listState{0};
    ImageFileList fileList{};

    static bool mount() {
        const auto board = M5.getBoard();
        if (board != m5::board_t::board_M5StackCoreS3 && board != m5::board_t::board_M5StackChan)
            return false;
        SPI.begin(36, 35, 37, 4);
        return SD.begin(4, SPI, 4000000, "/sdcard", 2, false) && SD.cardType() != CARD_NONE;
    }
    static uint8_t* readFile(File& file, size_t& size, const std::atomic<bool>* stop, const char*& error) {
        size = file.size();
        if (size < 12 || size > kImageByteLimit) { error = "image_unsupported"; return nullptr; }
        auto* bytes = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!bytes) { error = "image_memory_unavailable"; return nullptr; }
        for (size_t offset = 0; offset < size;) {
            if (stop && stop->load(std::memory_order_acquire)) { free(bytes); error = "image_canceled"; return nullptr; }
            const size_t count = size - offset < 4096 ? size - offset : 4096;
            if (file.read(bytes + offset, count) != count) { free(bytes); error = "image_read_failed"; return nullptr; }
            offset += count;
            vTaskDelay(1);
        }
        return bytes;
    }
    static void loadTask(void* context) {
        auto& self = *static_cast<ImageDisplayHardware*>(context);
        self.load();
        self.ready.store(true, std::memory_order_release);
        vTaskDelete(nullptr);
    }
    void load() {
        if (!mount()) { resultError = "sd_unavailable"; return; }
        char path[73];
        snprintf(path, sizeof(path), "/images/%s", filename);
        File file = SD.open(path, FILE_READ);
        if (!file) { resultError = "image_not_found"; return; }
        if (file.isDirectory()) { file.close(); resultError = "image_not_found"; return; }
        size_t size = 0;
        uint8_t* bytes = readFile(file, size, &canceled, resultError);
        file.close();
        if (!bytes) return;
        if (!imageJpeg(bytes, size)) { free(bytes); resultError = "image_unsupported"; return; }
        sprite.setPsram(true);
        sprite.setColorDepth(16);
        if (!sprite.createSprite(320, 240)) {
            free(bytes); resultError = "image_memory_unavailable"; return;
        }
        const bool decoded = !canceled.load(std::memory_order_acquire) && sprite.drawJpg(bytes, size);
        free(bytes);
        if (!decoded) resultError = "image_unsupported";
    }
    static void listTask(void* context) {
        auto& self = *static_cast<ImageDisplayHardware*>(context);
        self.list();
        self.listState.store(2, std::memory_order_release);
        vTaskDelete(nullptr);
    }
    void list() {
        fileList = {};
        if (!mount()) { fileList.error = "sd_unavailable"; return; }
        if (!SD.exists("/images")) return;
        File directory = SD.open("/images", FILE_READ);
        if (!directory || !directory.isDirectory()) {
            directory.close(); fileList.error = "image_directory_unavailable"; return;
        }
        while (true) {
            File entry = directory.openNextFile();
            if (!entry) break;
            const char* name = entry.name();
            const char* slash = name ? strrchr(name, '/') : nullptr;
            name = slash ? slash + 1 : name;
            const bool isDirectory = entry.isDirectory();
            bool compatible = false;
            if (!isDirectory && imageFilename(name)) {
                if (fileList.count == kImageFileLimit) {
                    fileList.truncated = true;
                    entry.close();
                    break;
                }
                size_t size = 0;
                const char* error = nullptr;
                uint8_t* bytes = readFile(entry, size, nullptr, error);
                if (error && strcmp(error, "image_unsupported")) {
                    entry.close(); directory.close(); fileList.error = error; return;
                }
                if (bytes && imageJpeg(bytes, size)) {
                    M5Canvas candidate;
                    candidate.setPsram(true);
                    candidate.setColorDepth(16);
                    if (!candidate.createSprite(320, 240)) {
                        free(bytes); entry.close(); directory.close();
                        fileList.error = "image_memory_unavailable"; return;
                    }
                    compatible = candidate.drawJpg(bytes, size);
                }
                free(bytes);
            }
            const bool more = fileList.add(name, isDirectory, compatible);
            entry.close();
            if (!more) break;
            vTaskDelay(1);
        }
        directory.close();
    }
};

} // namespace lab

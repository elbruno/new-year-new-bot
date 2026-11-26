// Goal: Demonstrate one bounded CoreS3 microphone recording.
// Responsibilities: Capture mono PCM16 into PSRAM and release audio resources.
// How it works: M5Unified signals completion through its buffer-release callback.
#include <M5Unified.h>
#include <esp_heap_caps.h>

#include <atomic>
#include <stdint.h>

namespace {

constexpr uint32_t kSampleRate = 16000;
constexpr uint32_t kDurationMs = 3000;
constexpr size_t kSampleCount = static_cast<size_t>(kSampleRate) * kDurationMs / 1000;
constexpr size_t kBufferBytes = kSampleCount * sizeof(int16_t);
constexpr uint32_t kCaptureTimeoutMs = kDurationMs + 2000;
static_assert(kSampleCount == 48000 && kBufferBytes == 96000);

int16_t* samples = nullptr;
std::atomic<bool> captureFinished{false};
std::atomic<bool> captureValid{false};

void onBufferReleased(void*, void* data, size_t count) {
    captureValid.store(data == samples && count == kSampleCount, std::memory_order_relaxed);
    captureFinished.store(true, std::memory_order_release);
}

bool hasCoreS3MicrophonePins(const m5::mic_config_t& config) {
    return config.pin_mck == GPIO_NUM_0 &&
        config.pin_bck == GPIO_NUM_34 &&
        config.pin_ws == GPIO_NUM_33 &&
        config.pin_data_in == GPIO_NUM_14 &&
        config.i2s_port == I2S_NUM_1;
}

void releaseSamples() {
    if (samples) {
        heap_caps_free(samples);
        samples = nullptr;
    }
}

} // namespace

void setup() {
    Serial.begin(115200);
    M5.begin(M5.config());

    if (M5.getBoard() != m5::board_t::board_M5StackCoreS3) {
        Serial.println("This example requires an M5Stack CoreS3.");
        return;
    }

    auto micConfig = M5.Mic.config();
    if (!hasCoreS3MicrophonePins(micConfig)) {
        Serial.println("M5Unified did not provide the expected CoreS3 microphone pins.");
        return;
    }
    micConfig.sample_rate = kSampleRate;
    M5.Mic.config(micConfig);

    if (M5.Mic.isRunning() || M5.Speaker.isPlaying()) {
        Serial.println("Audio is busy. Stop playback before starting the capture.");
        return;
    }

    const bool speakerWasRunning = M5.Speaker.isRunning();
    if (speakerWasRunning) {
        M5.Speaker.end();
        if (M5.Speaker.isRunning()) {
            Serial.println("Speaker shutdown is uncertain; microphone capture was not started.");
            return;
        }
    }

    samples = static_cast<int16_t*>(
        heap_caps_malloc(kBufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!samples) {
        Serial.println("Could not allocate the 96,000-byte capture buffer in PSRAM.");
        if (speakerWasRunning) M5.Speaker.begin();
        return;
    }

    captureFinished.store(false, std::memory_order_relaxed);
    captureValid.store(false, std::memory_order_relaxed);
    M5.Mic.setBufferReleaseCallback(nullptr, onBufferReleased);
    M5.netbotResetMicResults();

    Serial.println("Recording 3 seconds of mono PCM16 at 16 kHz...");
    const bool started = M5.Mic.begin();
    const bool queued = started && M5.Mic.record(samples, kSampleCount, kSampleRate, false);
    if (!started) {
        Serial.println(M5.netbotMicEnableOk()
            ? "M5Unified microphone initialization failed."
            : "CoreS3 microphone codec initialization failed.");
    } else if (!queued) {
        Serial.println("M5Unified did not accept the microphone buffer.");
    } else {
        const uint32_t start = millis();
        while (!captureFinished.load(std::memory_order_acquire) &&
               static_cast<uint32_t>(millis() - start) < kCaptureTimeoutMs) {
            delay(1);
        }
    }

    M5.Mic.end();
    if (M5.Mic.isRunning()) {
        Serial.println("Microphone shutdown is uncertain; retaining its PSRAM buffer until reboot.");
        return;
    }
    M5.Mic.setBufferReleaseCallback(nullptr, nullptr);
    if (!M5.netbotMicDisableOk()) {
        Serial.println("Microphone codec cleanup is uncertain; retaining its PSRAM buffer until reboot.");
        return;
    }

    if (speakerWasRunning) {
        M5.Speaker.begin();
        if (!M5.Speaker.isRunning())
            Serial.println("Capture ended, but the speaker did not resume.");
    }

    const bool completed = captureFinished.load(std::memory_order_acquire);
    const bool valid = captureValid.load(std::memory_order_relaxed);
    releaseSamples();
    if (!queued || !completed || !valid) {
        Serial.println("Capture did not complete with the expected buffer and sample count.");
        return;
    }

    Serial.printf("Capture complete: %u samples, %u PCM bytes. Buffer released.\n",
        static_cast<unsigned>(kSampleCount), static_cast<unsigned>(kBufferBytes));
}

void loop() {
    M5.update();
    delay(10);
}

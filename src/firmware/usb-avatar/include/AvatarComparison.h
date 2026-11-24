#pragma once

#include <math.h>
#include <stdint.h>

namespace lab {

enum class AvatarComparisonSample : uint8_t {
    BreathingA,
    BreathingB,
    PettingC,
    PettingD
};

struct AvatarComparisonPose {
    AvatarComparisonSample sample;
    float breathing;
    float petting;
};

struct AvatarComparisonStatus {
    bool selected;
    AvatarComparisonSample sample;
    bool active;
    uint32_t remainingMs;
};

inline const char* avatarComparisonId(AvatarComparisonSample sample) {
    switch (sample) {
        case AvatarComparisonSample::BreathingA: return "A";
        case AvatarComparisonSample::BreathingB: return "B";
        case AvatarComparisonSample::PettingC: return "C";
        case AvatarComparisonSample::PettingD: return "D";
        default: return "";
    }
}

inline bool avatarComparisonIsVariantB(AvatarComparisonSample sample) {
    return sample == AvatarComparisonSample::BreathingB ||
        sample == AvatarComparisonSample::PettingD;
}

inline bool avatarComparisonIsPetting(AvatarComparisonSample sample) {
    return sample == AvatarComparisonSample::PettingC ||
        sample == AvatarComparisonSample::PettingD;
}

inline const char* avatarComparisonLabel(AvatarComparisonSample sample) {
    switch (sample) {
        case AvatarComparisonSample::BreathingA: return "A: Breath A";
        case AvatarComparisonSample::BreathingB: return "B: Breath B";
        case AvatarComparisonSample::PettingC: return "C: Pet A";
        case AvatarComparisonSample::PettingD: return "D: Pet B";
        default: return "Avatar sample";
    }
}

inline float avatarComparisonEase(float value) {
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    return value * value * (3.0f - 2.0f * value);
}

inline float avatarComparisonBreathEnvelope(uint32_t elapsedMs) {
    const uint32_t phase = elapsedMs % 3200;
    if (phase < 1280) return avatarComparisonEase(static_cast<float>(phase) / 1280.0f);
    return 1.0f - avatarComparisonEase(static_cast<float>(phase - 1280) / 1920.0f);
}

inline float avatarComparisonPetEnvelope(uint32_t elapsedMs) {
    const uint32_t phase = elapsedMs % 1000;
    if (phase < 120) return 0.0f;
    if (phase < 360) return avatarComparisonEase(static_cast<float>(phase - 120) / 240.0f);
    if (phase < 720) return 1.0f;
    return 1.0f - avatarComparisonEase(static_cast<float>(phase - 720) / 240.0f);
}

struct AvatarComparisonRow {
    float sourceY;
    float width;
    float nuzzle;

    float sourceX(float destinationX) const {
        return 160.0f + (destinationX - 160.0f - nuzzle) / width;
    }
};

inline AvatarComparisonRow avatarComparisonRow(const AvatarComparisonPose& pose, float destinationY) {
    const bool variantB = avatarComparisonIsVariantB(pose.sample);
    const float height = variantB ?
        1.0f + 0.006f * pose.breathing - 0.025f * pose.petting :
        1.0f + 0.018f * pose.breathing - 0.05f * pose.petting;
    const float width = variantB ?
        1.0f + 0.018f * pose.breathing + 0.028f * pose.petting :
        1.0f - 0.009f * pose.breathing + 0.05f * pose.petting;
    float nuzzle = 0.0f;
    if (variantB && destinationY < 205.0f) {
        const float distance = (destinationY - 127.0f) / 40.0f;
        nuzzle = 3.2f * pose.petting * expf(-(distance * distance));
    }
    return {205.0f + (destinationY - 205.0f) / height, width, nuzzle};
}

class AvatarComparison {
public:
    static constexpr uint32_t kPreviewDurationMs = 5000;
    static constexpr uint32_t kFrameIntervalMs = 80;

    bool start(AvatarComparisonSample sample, uint32_t now) {
        switch (sample) {
            case AvatarComparisonSample::BreathingA:
            case AvatarComparisonSample::BreathingB:
            case AvatarComparisonSample::PettingC:
            case AvatarComparisonSample::PettingD:
                break;
            default:
                return false;
        }
        currentSample = sample;
        startedAt = lastFrameAt = now;
        selected = true;
        running = true;
        return true;
    }

    void cancel() { running = false; }
    bool active() const { return running; }
    bool expired(uint32_t now) const { return running && now - startedAt >= kPreviewDurationMs; }
    AvatarComparisonStatus snapshot(uint32_t now) const {
        const uint32_t elapsed = now - startedAt;
        const bool current = running && elapsed < kPreviewDurationMs;
        return {selected, currentSample, current,
            current ? kPreviewDurationMs - elapsed : 0};
    }
    uint32_t remainingMs(uint32_t now) const {
        if (!running) return 0;
        const uint32_t elapsed = now - startedAt;
        return elapsed >= kPreviewDurationMs ? 0 : kPreviewDurationMs - elapsed;
    }
    AvatarComparisonSample sample() const { return currentSample; }

    bool frameDue(uint32_t now) {
        if (!running || now - lastFrameAt < kFrameIntervalMs) return false;
        lastFrameAt = now;
        return true;
    }

    AvatarComparisonPose pose(uint32_t now) const {
        const uint32_t elapsed = now - startedAt;
        return {
            currentSample,
            avatarComparisonIsPetting(currentSample) ? 0.0f : avatarComparisonBreathEnvelope(elapsed),
            avatarComparisonIsPetting(currentSample) ? avatarComparisonPetEnvelope(elapsed) : 0.0f
        };
    }

private:
    AvatarComparisonSample currentSample = AvatarComparisonSample::BreathingA;
    uint32_t startedAt = 0;
    uint32_t lastFrameAt = 0;
    bool selected = false;
    bool running = false;
};

}  // namespace lab

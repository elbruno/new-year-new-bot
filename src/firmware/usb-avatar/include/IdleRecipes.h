// Goal: Schedule idle avatar expressions.
// Responsibilities: Define gaze recipes, blink timing, and scheduler state.
// How it works:
// 1. Recipes describe timed sequences of gaze directions.
// 2. The scheduler advances steps and selects blink timing.
// 3. Callers use the resulting eye state for rendering.
#pragma once

#include <assert.h>
#include <stdint.h>

namespace lab {

constexpr uint8_t kIdleMotionFrames = 24;
constexpr uint32_t kIdleTickMs = 80;
constexpr uint8_t kBlinkTicks = 2;
constexpr uint8_t kMinimumBlinkGapTicks = 38;
constexpr uint8_t kMaximumBlinkGapTicks = 75;
constexpr uint16_t kMinimumRecipeTicks = 375;
constexpr uint16_t kMaximumRecipeTicks = 750;
constexpr uint32_t kBlinkSeed = 0x2468ACE1;
constexpr uint32_t kRecipeSeed = 0x13579C03;
enum class IdleRecipe : uint8_t { Calm, Curious, Reflective };
enum class IdleGaze : uint8_t { Neutral, LookUp, LookLeft, LookRight };
enum class IdleEyes : uint8_t { Neutral, Blink, LookUp, LookLeft, LookRight };

struct IdleSegment {
    IdleGaze gaze;
    uint16_t ticks;
};

struct IdleStep {
    IdleRecipe recipe;
    IdleGaze gaze;
    IdleEyes eyes;
};

constexpr IdleSegment kCalmSegments[] = {
    {IdleGaze::Neutral, 90}, {IdleGaze::LookUp, 18}, {IdleGaze::Neutral, 108},
};

constexpr IdleSegment kCuriousSegments[] = {
    {IdleGaze::Neutral, 30}, {IdleGaze::LookLeft, 18}, {IdleGaze::Neutral, 14},
    {IdleGaze::LookRight, 18}, {IdleGaze::Neutral, 66}, {IdleGaze::LookRight, 18},
    {IdleGaze::Neutral, 14}, {IdleGaze::LookLeft, 18}, {IdleGaze::Neutral, 44},
};

constexpr IdleSegment kReflectiveSegments[] = {
    {IdleGaze::Neutral, 40}, {IdleGaze::LookUp, 30}, {IdleGaze::Neutral, 10},
    {IdleGaze::LookLeft, 24}, {IdleGaze::Neutral, 60}, {IdleGaze::LookUp, 30},
    {IdleGaze::Neutral, 10}, {IdleGaze::LookRight, 24}, {IdleGaze::Neutral, 60},
};

class XorShift32 {
public:
    explicit XorShift32(uint32_t seed) : state(seed) { assert(seed != 0); }

    uint32_t next() {
        uint32_t value = state;
        value ^= value << 13;
        value ^= value >> 17;
        value ^= value << 5;
        state = value;
        return value;
    }

    uint32_t between(uint32_t low, uint32_t high) {
        return low + next() % (high - low + 1);
    }

private:
    uint32_t state;
};

class IdleScheduler {
public:
    explicit IdleScheduler(uint32_t blinkSeed = kBlinkSeed, uint32_t recipeSeed = kRecipeSeed)
        : blinkRandom(blinkSeed), recipeRandom(recipeSeed), blinkSeedValue(blinkSeed),
          recipeSeedValue(recipeSeed) {
        reset(0);
    }

    void reset(uint32_t now) {
        blinkRandom = XorShift32(blinkSeedValue);
        recipeRandom = XorShift32(recipeSeedValue);
        currentRecipe = IdleRecipe::Calm;
        recipeTicks = 0;
        switchAfter = recipeRandom.between(kMinimumRecipeTicks, kMaximumRecipeTicks);
        segment = 0;
        segmentLeft = segments(currentRecipe)[0].ticks;
        blinkLeft = 0;
        blinkWait = static_cast<uint8_t>(
            blinkRandom.between(kMinimumBlinkGapTicks, kMaximumBlinkGapTicks));
        motionFrame = 0;
        lastTick = now;
        currentStep = next();
    }

    const IdleStep& current() const { return currentStep; }
    uint8_t motionIndex() const { return motionFrame; }

    bool update(uint32_t now, bool enabled) {
        if (!enabled) {
            lastTick = now;
            return false;
        }
        const uint32_t elapsed = now - lastTick;
        uint32_t due = elapsed / kIdleTickMs;
        if (due == 0) return false;
        if (due > kIdleMotionFrames) {
            lastTick = now - elapsed % kIdleTickMs;
            due = kIdleMotionFrames;
        } else {
            lastTick += due * kIdleTickMs;
        }
        while (due-- > 0) {
            currentStep = next();
            motionFrame = static_cast<uint8_t>((motionFrame + 1) % kIdleMotionFrames);
        }
        return true;
    }

    IdleStep step() {
        currentStep = next();
        motionFrame = static_cast<uint8_t>((motionFrame + 1) % kIdleMotionFrames);
        return currentStep;
    }

private:
    XorShift32 blinkRandom;
    XorShift32 recipeRandom;
    uint32_t blinkSeedValue;
    uint32_t recipeSeedValue;
    IdleRecipe currentRecipe = IdleRecipe::Calm;
    uint16_t recipeTicks = 0;
    uint16_t switchAfter = 0;
    uint8_t segment = 0;
    uint16_t segmentLeft = 0;
    uint8_t blinkLeft = 0;
    uint8_t blinkWait = 0;
    uint8_t motionFrame = 0;
    uint32_t lastTick = 0;
    IdleStep currentStep{IdleRecipe::Calm, IdleGaze::Neutral, IdleEyes::Neutral};

    static const IdleSegment* segments(IdleRecipe recipe) {
        switch (recipe) {
            case IdleRecipe::Curious: return kCuriousSegments;
            case IdleRecipe::Reflective: return kReflectiveSegments;
            default: return kCalmSegments;
        }
    }

    static uint8_t segmentCount(IdleRecipe recipe) {
        switch (recipe) {
            case IdleRecipe::Curious: return sizeof(kCuriousSegments) / sizeof(kCuriousSegments[0]);
            case IdleRecipe::Reflective:
                return sizeof(kReflectiveSegments) / sizeof(kReflectiveSegments[0]);
            default: return sizeof(kCalmSegments) / sizeof(kCalmSegments[0]);
        }
    }

    static IdleEyes eyesFor(IdleGaze gaze) {
        switch (gaze) {
            case IdleGaze::LookUp: return IdleEyes::LookUp;
            case IdleGaze::LookLeft: return IdleEyes::LookLeft;
            case IdleGaze::LookRight: return IdleEyes::LookRight;
            default: return IdleEyes::Neutral;
        }
    }

    IdleStep next() {
        const IdleRecipe recipe = currentRecipe;
        const IdleGaze gaze = segments(recipe)[segment].gaze;
        IdleEyes eyes;
        if (blinkLeft == 0 && blinkWait == 0) blinkLeft = kBlinkTicks;
        if (blinkLeft > 0) {
            eyes = IdleEyes::Blink;
            --blinkLeft;
            if (blinkLeft == 0) {
                blinkWait = static_cast<uint8_t>(
                    blinkRandom.between(kMinimumBlinkGapTicks, kMaximumBlinkGapTicks));
            }
        } else {
            eyes = eyesFor(gaze);
            if (blinkWait > 0) --blinkWait;
        }

        ++recipeTicks;
        --segmentLeft;
        if (segmentLeft == 0) {
            ++segment;
            if (segment == segmentCount(recipe)) {
                segment = 0;
                if (recipeTicks >= switchAfter) switchRecipe();
            }
            segmentLeft = segments(currentRecipe)[segment].ticks;
        }
        return {recipe, gaze, eyes};
    }

    void switchRecipe() {
        const uint32_t choice = recipeRandom.between(0, 1);
        if (currentRecipe == IdleRecipe::Calm)
            currentRecipe = choice == 0 ? IdleRecipe::Curious : IdleRecipe::Reflective;
        else if (currentRecipe == IdleRecipe::Curious)
            currentRecipe = choice == 0 ? IdleRecipe::Calm : IdleRecipe::Reflective;
        else
            currentRecipe = choice == 0 ? IdleRecipe::Calm : IdleRecipe::Curious;
        recipeTicks = 0;
        switchAfter = recipeRandom.between(kMinimumRecipeTicks, kMaximumRecipeTicks);
    }
};

inline const char* idleRecipeName(IdleRecipe recipe) {
    switch (recipe) {
        case IdleRecipe::Curious: return "curious";
        case IdleRecipe::Reflective: return "reflective";
        default: return "calm";
    }
}

inline char idleEyesCode(IdleEyes eyes) {
    switch (eyes) {
        case IdleEyes::Blink: return 'B';
        case IdleEyes::LookUp: return 'U';
        case IdleEyes::LookLeft: return 'L';
        case IdleEyes::LookRight: return 'R';
        default: return 'N';
    }
}

inline uint8_t idleRecipeCode(IdleRecipe recipe) {
    return static_cast<uint8_t>(recipe);
}

}  // namespace lab

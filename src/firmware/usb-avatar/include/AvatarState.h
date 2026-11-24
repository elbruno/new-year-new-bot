// Goal: Track the avatar's visible expression over time.
// Responsibilities: Represent expressions and manage timed avatar commands.
// How it works:
// 1. Callers apply or configure an expression.
// 2. Updates advance timed and automatic state.
// 3. Renderers read the visible expression to choose an image.
#pragma once

#include <stdint.h>
#include <string.h>

enum class Expression { Neutral, Blink, Wink, Smile, LookUp, Sleep, Sleepy, LookLeft, LookRight };

inline bool expressionFor(const char* name, Expression& expression) {
    if (!strcmp(name, "neutral") || !strcmp(name, "wake")) expression = Expression::Neutral;
    else if (!strcmp(name, "wink")) expression = Expression::Wink;
    else if (!strcmp(name, "smile")) expression = Expression::Smile;
    else if (!strcmp(name, "look_up")) expression = Expression::LookUp;
    else if (!strcmp(name, "sleep")) expression = Expression::Sleep;
    else return false;
    return true;
}

class AvatarState {
public:
    Expression visible = Expression::Neutral;
    bool automatic = true;
    uint32_t blinkInterval = 4000;
    uint32_t glanceInterval = 11000;

    bool busy() const { return activeId != 0; }
    Expression restingExpression() const { return resting; }

    void cancel(uint32_t now) {
        activeId = 0;
        idleAnimation = false;
        visible = resting;
        idleStarted = now;
    }

    void reset(uint32_t now) {
        *this = AvatarState();
        idleStarted = lastBlink = lastGlance = now;
    }

    void configure(uint32_t now, uint32_t blinkMs, uint32_t glanceMs, bool autoMode) {
        blinkInterval = blinkMs;
        glanceInterval = glanceMs;
        automatic = autoMode;
        idleStarted = lastBlink = lastGlance = now;
        if (!busy()) {
            idleAnimation = false;
            visible = resting;
        }
    }

    void apply(uint32_t now, uint32_t id, Expression expression, uint32_t duration) {
        visible = expression;
        started = now;
        holdMs = duration;
        activeId = id;
        idleAnimation = false;
        if (expression == Expression::Sleep || expression == Expression::Neutral) {
            resting = expression;
            holdMs = 0;
        }
    }

    uint32_t update(uint32_t now) {
        // Unsigned subtraction keeps timers correct across millis() rollover.
        if (activeId != 0) {
            if (now - started < holdMs) return 0;
            const uint32_t completed = activeId;
            activeId = 0;
            visible = resting;
            idleStarted = now;
            return completed;
        }
        if (idleAnimation) {
            if (now - started >= holdMs) {
                idleAnimation = false;
                visible = resting;
            }
            return 0;
        }
        if (!automatic || resting == Expression::Sleep) return 0;
        if (now - lastGlance >= glanceInterval && now - idleStarted >= 1000) {
            visible = Expression::LookUp;
            lastGlance = now;
            started = now;
            holdMs = 900;
            idleAnimation = true;
        } else if (now - lastBlink >= blinkInterval && now - idleStarted >= 300) {
            visible = Expression::Blink;
            lastBlink = now;
            started = now;
            holdMs = 120;
            idleAnimation = true;
        }
        return 0;
    }

private:
    Expression resting = Expression::Neutral;
    uint32_t activeId = 0;
    uint32_t started = 0;
    uint32_t holdMs = 0;
    uint32_t idleStarted = 0;
    uint32_t lastBlink = 0;
    uint32_t lastGlance = 0;
    bool idleAnimation = false;
};

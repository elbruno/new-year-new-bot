// Goal: Provide local touchscreen controls for checked motion.
// Responsibilities: Draw the motion menu and translate touch into actions.
// How it works:
// 1. Menu state determines which controls are available.
// 2. Touch events select an action or close the menu.
// 3. Actions are forwarded to the motion controller and reflected on screen.
#pragma once

#include <M5Unified.h>
#include <stdio.h>
#include "LabMotion.h"
#include "MenuColors.h"

namespace lab {

enum class LocalMotionEvent { None, Opened, Closed, Enable, Lock, Stop, ShutDown };
struct LocalMotionButton {
    const char* label;
    int x, y, w, h;
    uint16_t color;
    LocalMotionEvent event;
};
inline constexpr LocalMotionButton kLocalMotionButtons[] = {
    {"ENABLE", 8, 24, 148, 56, menu_colors::action, LocalMotionEvent::Enable},
    {"LOCK", 164, 24, 148, 56, menu_colors::action, LocalMotionEvent::Lock},
    {"SHUT DOWN", 8, 88, 148, 56, menu_colors::danger, LocalMotionEvent::ShutDown},
    {"BACK", 8, 152, 148, 56, menu_colors::navigation, LocalMotionEvent::Closed},
    {"STOP", 164, 152, 148, 56, menu_colors::danger, LocalMotionEvent::Stop},
};

class LocalMotionMenu {
public:
    static constexpr uint32_t kIgnoreMs = 400, kTimeoutMs = 8000;
    static constexpr uint16_t background = 0x1083, disabled = 0x4208;

    explicit LocalMotionMenu(bool lesson = false) : lesson(lesson), open(lesson) {}
    bool isOpen() const { return open; }
    void show(uint32_t now, const char* error = nullptr) {
        open = true;
        openedAt = now;
        actionError = error;
    }
    bool enabled(LocalMotionEvent event, const MotionController& controller) const {
        const auto state = controller.snapshot().state;
        if (event == LocalMotionEvent::Enable) return state == MotionState::Disarmed;
        if (event == LocalMotionEvent::Lock)
            return state == MotionState::Ready || state == MotionState::Moving ||
                state == MotionState::Arming || state == MotionState::Disarmed;
        return true;
    }
    LocalMotionEvent tap(uint32_t now, int x, int y) {
        if (!open) {
            show(now);
            return LocalMotionEvent::Opened;
        }
        for (const auto& button : kLocalMotionButtons) {
            if (lesson && (button.event == LocalMotionEvent::Closed || button.event == LocalMotionEvent::ShutDown))
                continue;
            if (x < button.x || x >= button.x + button.w || y < button.y || y >= button.y + button.h)
                continue;
            // Stop is independent of the menu's double-tap suppression and timeout.
            if (button.event != LocalMotionEvent::Stop && now - openedAt < kIgnoreMs)
                return LocalMotionEvent::None;
            openedAt = now;
            if (button.event == LocalMotionEvent::Closed || button.event == LocalMotionEvent::ShutDown)
                open = false;
            return button.event;
        }
        return LocalMotionEvent::None;
    }
    LocalMotionEvent update(uint32_t now) {
        if (lesson || !open || now - openedAt < kTimeoutMs) return LocalMotionEvent::None;
        open = false;
        return LocalMotionEvent::Closed;
    }
    void perform(LocalMotionEvent event, MotionController& controller, uint32_t now) {
        actionError = nullptr;
        if (event == LocalMotionEvent::Enable) {
            if (!enabled(event, controller) || !controller.arm(now))
                actionError = controller.unavailableReason();
        } else if (event == LocalMotionEvent::Lock) {
            if (!controller.disarm(now)) actionError = controller.unavailableReason();
        } else if (event == LocalMotionEvent::Stop) {
            if (controller.emergencyStop(now) == StopResult::Failed) actionError = "motion_stop_unconfirmed";
        }
    }
    void draw(MotionController& controller, const char* title = nullptr) {
        const auto snapshot = controller.snapshot();
        drawnState = snapshot.state;
        drawnError = snapshot.error;
        M5.Display.fillScreen(background);
        M5.Display.setFont(&fonts::Font0);
        M5.Display.setTextSize(1.0f);
        M5.Display.setTextDatum(textdatum_t::middle_center);
        M5.Display.setTextColor(0xFFFF, background);
        char status[96];
        snprintf(status, sizeof(status), "%s%s%s", title ? title : "Motion", title ? " | " : ": ",
            motionStateName(snapshot.state));
        M5.Display.drawString(status, 160, 6);
        M5.Display.drawString(actionError ? actionError : snapshot.error ? snapshot.error : "Checked control", 160, 18);
        M5.Display.setFont(&fonts::Font2);
        for (const auto& button : kLocalMotionButtons) {
            if (lesson && (button.event == LocalMotionEvent::Closed || button.event == LocalMotionEvent::ShutDown))
                continue;
            const auto color = enabled(button.event, controller) ? button.color : disabled;
            M5.Display.fillRoundRect(button.x, button.y, button.w, button.h, 8, color);
            M5.Display.setTextColor(0xFFFF, color);
            M5.Display.drawString(button.label, button.x + button.w / 2, button.y + button.h / 2);
        }
    }
    bool changed(const MotionController& controller) const {
        const auto snapshot = controller.snapshot();
        return snapshot.state != drawnState || snapshot.error != drawnError;
    }

private:
    bool lesson, open;
    uint32_t openedAt = 0;
    MotionState drawnState = MotionState::Blocked;
    const char* drawnError = nullptr;
    const char* actionError = nullptr;
};

} // namespace lab

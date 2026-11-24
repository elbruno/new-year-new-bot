#pragma once

#include <stdint.h>
#include "MenuColors.h"

// What the main loop must do after a touch or a timer tick.
// The menu only decides; main.cpp draws and powers off.
enum class MenuEvent { None, Opened, Closed, ShutDown };

struct MenuItem {
    const char* label;
    int x, y, w, h;
    uint16_t color;  // RGB565
    MenuEvent event;
};

// The same rectangles are drawn and hit-tested. A new option is a new row.
constexpr MenuItem kMenuItems[] = {
    {"SHUT DOWN", 40, 35, 240, 70, menu_colors::danger, MenuEvent::ShutDown},
    {"CANCEL", 40, 135, 240, 70, menu_colors::navigation, MenuEvent::Closed},
};

class TouchMenu {
public:
    static constexpr uint32_t kIgnoreMs = 400;    // swallow a double tap right after opening
    static constexpr uint32_t kTimeoutMs = 8000;  // close when nobody touches the screen

    bool isOpen() const { return open; }

    MenuEvent tap(uint32_t now, int x, int y) {
        if (!open) {
            open = true;
            openedAt = now;
            return MenuEvent::Opened;
        }
        // Unsigned subtraction keeps timers correct across millis() rollover.
        if (now - openedAt < kIgnoreMs) return MenuEvent::None;
        open = false;
        for (const MenuItem& item : kMenuItems)
            if (contains(item, x, y)) return item.event;
        return MenuEvent::Closed;
    }

    MenuEvent update(uint32_t now) {
        if (!open || now - openedAt < kTimeoutMs) return MenuEvent::None;
        open = false;
        return MenuEvent::Closed;
    }

private:
    bool open = false;
    uint32_t openedAt = 0;

    static bool contains(const MenuItem& item, int x, int y) {
        return x >= item.x && x < item.x + item.w && y >= item.y && y < item.y + item.h;
    }
};

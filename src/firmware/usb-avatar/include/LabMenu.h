// Goal: Define and navigate the firmware's touchscreen menus.
// Responsibilities: Describe pages, buttons, choices, and navigation state.
// How it works:
// 1. Page definitions associate screen regions with menu choices.
// 2. Touch input resolves to a button or navigation event.
// 3. The firmware uses the resulting choice to update the UI or runtime.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "LabContract.h"
#include "MenuColors.h"

namespace lab {

enum class Page {
    Face, Main, Demo, Expressions, Expressions2, Expressions3, Expressions4, DemoTouch,
    Motion, MotionMove, MotionMove2, MotionMove3, BspYaw, More, Network, NetworkStatus,
    SetupKey, Settings, DisplayStatus, Shutdown, Notification, NotificationDetail,
    NotificationActions, Sensors, SensorReadings, Nfc, NfcResult, Rgb, Petting, AvatarTest,
    ExpressionPreview, PhysicalExpressions
};
enum class MenuChoice {
    None, Back, Demo, Motion, Arm, Disarm, Network, Setup, ShowSetupKey, CancelSetup,
    Quiet, Settings, BrightnessDown, BrightnessUp, Shutdown, PowerOff, Stop, Action,
    NotificationOpen, NotificationDiscard, NotificationAction, StatusStrip, NotificationAcknowledge,
    Sensors, Rgb, Nfc, NfcRead, Navigate, Petting, PettingSound, AvatarTest,
    AvatarSampleA, AvatarSampleB, AvatarSampleC, AvatarSampleD, BspYawPositive, BspYawNegative,
    BspPitchUp, BspPitchDown, BspSetHome, ExpressionPreview,
    PreviewGrumpy, PreviewNodYes, PreviewShakeNo, PreviewDance,
    PhysicalNodYes, PhysicalShakeNo, PhysicalDance
};
inline constexpr uint16_t buttonColor(MenuChoice choice) {
    switch (choice) {
        case MenuChoice::Back:
        case MenuChoice::Demo:
        case MenuChoice::Sensors:
        case MenuChoice::Nfc:
        case MenuChoice::Motion:
        case MenuChoice::Network:
        case MenuChoice::Settings:
        case MenuChoice::CancelSetup:
        case MenuChoice::Shutdown:
        case MenuChoice::AvatarTest:
        case MenuChoice::Navigate:
            return menu_colors::navigation;
        case MenuChoice::Stop:
        case MenuChoice::PowerOff:
            return menu_colors::danger;
        default:
            return menu_colors::action;
    }
}

struct Button {
    const char* label;
    int x, y, w, h;
    MenuChoice choice;
    Action action = Action::Invalid;
    const char* id = nullptr;
    Page target = Page::Face;
};

#define LAB_BUTTON(label, id, col, row, choice) \
    {label, 8 + (col) * 156, 24 + (row) * 64, 148, 56, MenuChoice::choice, Action::Invalid, id}
#define LAB_ACTION(label, id, col, row, action) \
    {label, 8 + (col) * 156, 24 + (row) * 64, 148, 56, MenuChoice::Action, Action::action, id}
#define LAB_NAV(label, id, col, row, target) \
    {label, 8 + (col) * 156, 24 + (row) * 64, 148, 56, MenuChoice::Navigate, Action::Invalid, id, Page::target}
#define LAB_BACK { "Back", 8, 152, 148, 56, MenuChoice::Back, Action::Invalid, "btn-back" }
#define LAB_STOP { "STOP", 164, 152, 148, 56, MenuChoice::Stop, Action::Invalid, "btn-stop" }
#define LAB_QUIET { "Quiet", 8, 152, 148, 56, MenuChoice::Quiet, Action::Invalid, "btn-quiet" }

inline constexpr Button faceButtons[] = {
    { "STOP", 248, 152, 64, 56, MenuChoice::Stop, Action::Invalid, "btn-stop" }
};
inline constexpr Button mainButtons[] = {
    LAB_BUTTON("Demo", "btn-demo", 0, 0, Demo),
    LAB_BUTTON("Motion", "btn-motion", 1, 0, Motion),
    LAB_BUTTON("Settings", "btn-settings", 0, 1, Settings),
    LAB_NAV("More", "btn-more", 1, 1, More), LAB_BACK, LAB_STOP,
};
inline constexpr Button demoButtons[] = {
    LAB_NAV("Expressions", "btn-expressions", 0, 0, Expressions),
    LAB_BUTTON("Sensors", "btn-sensors", 1, 0, Sensors),
    LAB_NAV("Screen touch", "btn-screen-touch", 0, 1, DemoTouch), LAB_BACK, LAB_STOP,
    LAB_NAV("Petting", "btn-petting", 1, 1, Petting),
};
inline constexpr Button expressionButtons[] = {
    LAB_ACTION("Wink", "btn-wink", 0, 0, Wink), LAB_ACTION("Smile", "btn-smile", 1, 0, Smile),
    LAB_NAV("Next", "btn-next", 1, 1, Expressions2), LAB_BACK, LAB_STOP,
};
inline constexpr Button expression2Buttons[] = {
    LAB_ACTION("Look up", "btn-look-up", 0, 0, LookUp), LAB_ACTION("Sleep", "btn-sleep", 1, 0, Sleep),
    LAB_NAV("Previous", "btn-previous", 0, 1, Expressions), LAB_NAV("Next", "btn-next", 1, 1, Expressions3),
    LAB_BACK, LAB_STOP,
};
inline constexpr Button expression3Buttons[] = {
    LAB_ACTION("Wake", "btn-wake", 0, 0, Wake), LAB_ACTION("Warning", "btn-warning", 1, 0, Warning),
    LAB_NAV("Previous", "btn-previous", 0, 1, Expressions2), LAB_NAV("Next", "btn-next", 1, 1, Expressions4),
    LAB_BACK, LAB_STOP,
};
inline constexpr Button expression4Buttons[] = {
    LAB_ACTION("Sound", "btn-sound", 0, 0, Sound),
    LAB_ACTION("Notify", "btn-notify", 1, 0, ImportantNotification),
    LAB_NAV("Previous", "btn-previous", 0, 1, Expressions3), LAB_BACK, LAB_STOP,
};
inline constexpr Button demoTouchButtons[] = { LAB_BACK, LAB_STOP };
inline constexpr Button sensorButtons[] = {
    LAB_NAV("Readings", "btn-readings", 0, 0, SensorReadings),
    LAB_NAV("RGB durations", "btn-rgb", 1, 0, Rgb),
    LAB_BUTTON("NFC", "btn-nfc", 0, 1, Nfc),
    LAB_BACK, LAB_STOP,
};
inline constexpr Button rgbButtons[] = {
    LAB_BUTTON("RGB 1 second", "btn-rgb-1-second", 0, 0, Rgb),
    LAB_BUTTON("RGB 5 seconds", "btn-rgb-5-seconds", 1, 0, Rgb),
    LAB_BUTTON("RGB 10 seconds", "btn-rgb-10-seconds", 0, 1, Rgb),
    LAB_BUTTON("Quiet", "btn-quiet", 1, 1, Quiet), LAB_BACK, LAB_STOP,
};
inline constexpr Button pettingButtons[] = {
    LAB_BUTTON("Toggle petting", "btn-toggle-petting", 0, 0, Petting),
    LAB_BUTTON("Toggle sound", "btn-toggle-petting-sound", 1, 0, PettingSound),
    LAB_BACK, LAB_STOP,
};
inline uint32_t rgbDuration(const Button& button) {
    if (button.choice != MenuChoice::Rgb || !button.id) return 0;
    if (!strcmp(button.id, "btn-rgb-1-second")) return 1000;
    if (!strcmp(button.id, "btn-rgb-5-seconds")) return 5000;
    if (!strcmp(button.id, "btn-rgb-10-seconds")) return 10000;
    return 0;
}
inline bool visibleButton(Page page, bool faceStopVisible) {
    return page != Page::Face || faceStopVisible;
}
inline constexpr Button sensorReadingsButtons[] = { LAB_BACK, LAB_STOP };
inline constexpr Button nfcButtons[] = {
    LAB_BUTTON("Read phone", "btn-read-phone", 0, 0, NfcRead),
    LAB_NAV("Read result", "btn-read-result", 1, 0, NfcResult), LAB_BACK, LAB_STOP,
};
inline constexpr Button nfcResultButtons[] = { LAB_BACK, LAB_STOP };
inline constexpr Button motionButtons[] = {
    LAB_NAV("Movement", "btn-movement", 0, 0, MotionMove),
    LAB_BUTTON("Enable", "btn-enable", 1, 0, Arm), LAB_BUTTON("Lock", "btn-lock", 0, 1, Disarm),
    LAB_ACTION("Message", "btn-message", 1, 1, ImportantMessage), LAB_BACK, LAB_STOP,
};
inline constexpr Button bspYawButtons[] = {
    {"HEAD LEFT", 8, 58, 148, 40, MenuChoice::BspYawPositive, Action::Invalid, "btn-bsp-yaw-positive"},
    {"HEAD RIGHT", 164, 58, 148, 40, MenuChoice::BspYawNegative, Action::Invalid, "btn-bsp-yaw-negative"},
    {"HEAD UP", 8, 104, 148, 40, MenuChoice::BspPitchUp, Action::Invalid, "btn-bsp-pitch-up"},
    {"HEAD DOWN", 164, 104, 148, 40, MenuChoice::BspPitchDown, Action::Invalid, "btn-bsp-pitch-down"},
#if defined(NETBOT_WIFI_LAB) && !defined(NETBOT_LESSON_EXAMPLES) && \
    !defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC) && !defined(NETBOT_TWO_AXIS_TEST) && !defined(NETBOT_YAW_BENCH)
    {"Back", 8, 152, 70, 56, MenuChoice::Back, Action::Invalid, "btn-back"},
    {"Set Home", 86, 152, 70, 56, MenuChoice::BspSetHome, Action::Invalid, "btn-set-home"},
    LAB_STOP,
#else
    LAB_BACK, LAB_STOP,
#endif
};
inline constexpr bool kLocalHomeSetup = [] {
    for (const auto& button : bspYawButtons)
        if (button.choice == MenuChoice::BspSetHome) return true;
    return false;
}();
inline constexpr Button motionMoveButtons[] = {
    LAB_ACTION("Left", "btn-left", 0, 0, MoveLeft), LAB_ACTION("Right", "btn-right", 1, 0, MoveRight),
    LAB_NAV("Head controller", "btn-bsp-yaw", 0, 1, BspYaw),
    LAB_NAV("Next", "btn-next", 1, 1, MotionMove2), LAB_BACK, LAB_STOP,
};
inline constexpr Button motionMove2Buttons[] = {
    LAB_ACTION("Up", "btn-up", 0, 0, MoveUp), LAB_ACTION("Down", "btn-down", 1, 0, MoveDown),
    LAB_NAV("Previous", "btn-previous", 0, 1, MotionMove), LAB_NAV("Next", "btn-next", 1, 1, MotionMove3),
    LAB_BACK, LAB_STOP,
};
inline constexpr Button motionMove3Buttons[] = {
    LAB_ACTION("Nod", "btn-nod", 0, 0, Nod), LAB_ACTION("Home", "btn-home", 1, 0, Home),
    LAB_NAV("Previous", "btn-previous", 0, 1, MotionMove2), LAB_BACK, LAB_STOP,
    LAB_NAV("Physical faces", "btn-physical-expressions", 1, 1, PhysicalExpressions),
};
inline constexpr Button physicalExpressionButtons[] = {
    LAB_BUTTON("Nod Yes", "btn-physical-nod-yes", 0, 0, PhysicalNodYes),
    LAB_BUTTON("Shake No", "btn-physical-shake-no", 1, 0, PhysicalShakeNo),
    LAB_BUTTON("Dance", "btn-physical-dance", 0, 1, PhysicalDance), LAB_BACK, LAB_STOP,
};
inline constexpr Button moreButtons[] = {
    LAB_BUTTON("Network", "btn-network", 0, 0, Network), LAB_BUTTON("Quiet", "btn-quiet", 1, 0, Quiet),
    LAB_BUTTON("Shutdown", "btn-shutdown", 0, 1, Shutdown),
    LAB_BUTTON("TEST", "btn-avatar-test", 1, 1, AvatarTest), LAB_BACK, LAB_STOP,
};
inline constexpr Button avatarTestButtons[] = {
    {"Back", 8, 0, 64, 24, MenuChoice::Back, Action::Invalid, "btn-back"},
    {"STOP", 248, 0, 64, 24, MenuChoice::Stop, Action::Invalid, "btn-stop"},
    {"Breath A", 8, 32, 148, 80, MenuChoice::AvatarSampleA, Action::Invalid, "btn-avatar-sample-a"},
    {"Breath B", 164, 32, 148, 80, MenuChoice::AvatarSampleB, Action::Invalid, "btn-avatar-sample-b"},
    {"Pet A", 8, 120, 148, 80, MenuChoice::AvatarSampleC, Action::Invalid, "btn-avatar-sample-c"},
    {"Pet B", 164, 120, 148, 80, MenuChoice::AvatarSampleD, Action::Invalid, "btn-avatar-sample-d"},
    {"Expressions", 82, 0, 156, 24, MenuChoice::ExpressionPreview, Action::Invalid, "btn-expression-preview"},
};
inline constexpr Button expressionPreviewButtons[] = {
    {"Back", 8, 0, 64, 24, MenuChoice::Back, Action::Invalid, "btn-back"},
    {"STOP", 248, 0, 64, 24, MenuChoice::Stop, Action::Invalid, "btn-stop"},
    {"Grumpy Shake", 8, 32, 148, 80, MenuChoice::PreviewGrumpy, Action::Invalid, "btn-preview-grumpy"},
    {"Nod Yes", 164, 32, 148, 80, MenuChoice::PreviewNodYes, Action::Invalid, "btn-preview-nod-yes"},
    {"Shake No V2", 8, 120, 148, 80, MenuChoice::PreviewShakeNo, Action::Invalid, "btn-preview-shake-no"},
    {"Dance", 164, 120, 148, 80, MenuChoice::PreviewDance, Action::Invalid, "btn-preview-dance"},
};
inline constexpr Button networkButtons[] = {
    LAB_BUTTON("Setup", "btn-setup", 0, 0, Setup),
    LAB_BUTTON("Show setup key", "btn-show-setup-key", 1, 0, ShowSetupKey),
    LAB_BUTTON("End setup", "btn-end-setup", 0, 1, CancelSetup),
    LAB_NAV("Network status", "btn-network-status", 1, 1, NetworkStatus), LAB_BACK, LAB_STOP,
};
inline constexpr Button networkStatusButtons[] = { LAB_BACK, LAB_STOP };
inline constexpr Button settingsButtons[] = {
    LAB_BUTTON("Brightness down", "btn-brightness-down", 0, 0, BrightnessDown),
    LAB_BUTTON("Brightness up", "btn-brightness-up", 1, 0, BrightnessUp),
    LAB_BUTTON("Toggle status bar", "btn-toggle-status-bar", 0, 1, StatusStrip),
    LAB_NAV("Display status", "btn-display-status", 1, 1, DisplayStatus), LAB_BACK, LAB_STOP,
};
inline constexpr Button displayStatusButtons[] = { LAB_BACK, LAB_STOP };
inline constexpr Button setupKeyButtons[] = { LAB_BACK, LAB_STOP };
inline constexpr Button shutdownButtons[] = {
    LAB_BUTTON("SHUT DOWN", "btn-shut-down", 0, 0, PowerOff),
    {"CANCEL", 8, 152, 148, 56, MenuChoice::Back, Action::Invalid, "btn-cancel"}, LAB_STOP,
};
inline constexpr Button notificationButtons[] = {
    LAB_BUTTON("Open", "btn-open", 0, 1, NotificationOpen),
    LAB_BUTTON("Discard", "btn-discard", 1, 1, NotificationDiscard), LAB_QUIET, LAB_STOP,
};
inline constexpr Button notificationDetailButtons[] = {
    LAB_NAV("Actions", "btn-actions", 0, 1, NotificationActions), LAB_QUIET, LAB_STOP,
};
inline constexpr Button notificationActionsButtons[] = {
    LAB_BUTTON("Acknowledge", "btn-acknowledge", 0, 0, NotificationAcknowledge),
    LAB_BUTTON("Discard", "btn-discard", 1, 0, NotificationDiscard),
    LAB_BUTTON("Open Companion", "btn-review-in-companion", 0, 1, NotificationAction),
    LAB_NAV("Previous", "btn-previous", 1, 1, NotificationDetail), LAB_QUIET, LAB_STOP,
};
#undef LAB_BUTTON
#undef LAB_ACTION
#undef LAB_NAV
#undef LAB_BACK
#undef LAB_STOP
#undef LAB_QUIET
inline bool notificationPage(Page page) {
    return page == Page::Notification || page == Page::NotificationDetail || page == Page::NotificationActions;
}

// Both labels and bounds are separate from the stable remote identities.
struct Screen {
    const char* id;
    const char* title;
    Page page;
};
inline constexpr Screen screens[] = {
    {"main", "Avatar face", Page::Face},
    {"menu", "Main menu", Page::Main},
    {"demo", "Demo", Page::Demo},
    {"expressions", "Expressions 1/4", Page::Expressions},
    {"expressions-2", "Expressions 2/4", Page::Expressions2},
    {"expressions-3", "Expressions 3/4", Page::Expressions3},
    {"expressions-4", "Expressions 4/4", Page::Expressions4},
    {"demo-touch", "Screen touch", Page::DemoTouch},
    {"motion", "Motion", Page::Motion},
    {"motion-move", "Movement 1/3", Page::MotionMove},
    {"motion-move-2", "Movement 2/3", Page::MotionMove2},
    {"motion-move-3", "Movement 3/3", Page::MotionMove3},
    {"bsp-yaw", "HEAD CONTROLLER", Page::BspYaw},
    {"more", "More", Page::More},
    {"network", "Network", Page::Network},
    {"network-status", "Network status", Page::NetworkStatus},
    {"settings", "Settings", Page::Settings},
    {"display-status", "Display status", Page::DisplayStatus},
    {"setup-key", "Setup key", Page::SetupKey},
    {"shutdown", "Shutdown", Page::Shutdown},
    {"notification", "Notification", Page::Notification},
    {"notification-detail", "Notification detail", Page::NotificationDetail},
    {"notification-actions", "Notification actions", Page::NotificationActions},
    {"sensors", "Demo sensors", Page::Sensors},
    {"sensor-readings", "Sensor readings", Page::SensorReadings},
    {"nfc", "NFC text", Page::Nfc},
    {"nfc-result", "NFC result", Page::NfcResult},
    {"rgb", "RGB durations", Page::Rgb},
    {"petting", "Petting settings", Page::Petting},
    {"avatar-test", "Avatar TEST", Page::AvatarTest},
    {"expression-preview", "Expression Preview", Page::ExpressionPreview},
    {"physical-expressions", "Physical Expressions", Page::PhysicalExpressions},
};
inline const Screen* findScreen(const char* id) {
    for (const auto& screen : screens) if (!strcmp(screen.id, id)) return &screen;
    return nullptr;
}
inline const Screen* findScreen(Page page) {
    for (const auto& screen : screens) if (screen.page == page) return &screen;
    return nullptr;
}
inline bool buttonId(const Button& button, char* output, size_t capacity) {
    if (!button.id || strlen(button.id) + 1 > capacity) return false;
    strcpy(output, button.id);
    return true;
}
// Motion, ARM, power and setup controls remain unavailable to remote UI.
inline bool remoteBlocked(const Button& button) {
    switch (button.choice) {
        case MenuChoice::Arm: case MenuChoice::Disarm: case MenuChoice::PowerOff:
        case MenuChoice::Setup: case MenuChoice::ShowSetupKey: case MenuChoice::CancelSetup:
        case MenuChoice::BspYawPositive:
        case MenuChoice::BspYawNegative:
        case MenuChoice::BspPitchUp:
        case MenuChoice::BspPitchDown:
        case MenuChoice::BspSetHome:
        case MenuChoice::ExpressionPreview: case MenuChoice::PreviewGrumpy:
        case MenuChoice::PreviewNodYes: case MenuChoice::PreviewShakeNo: case MenuChoice::PreviewDance:
        case MenuChoice::PhysicalNodYes: case MenuChoice::PhysicalShakeNo: case MenuChoice::PhysicalDance:
            return true;
        case MenuChoice::Navigate: return button.target == Page::PhysicalExpressions;
        case MenuChoice::Action: return requiresMotion(button.action);
        default: return false;
    }
}
inline bool remoteScreenBlocked(Page page) {
    return page == Page::SetupKey || page == Page::ExpressionPreview || page == Page::PhysicalExpressions;
}
inline bool isMotionElement(const Button& button) {
    return button.choice == MenuChoice::Arm || button.choice == MenuChoice::Disarm ||
        button.choice == MenuChoice::BspYawPositive || button.choice == MenuChoice::BspYawNegative ||
        button.choice == MenuChoice::BspPitchUp || button.choice == MenuChoice::BspPitchDown ||
        button.choice == MenuChoice::BspSetHome ||
        button.choice == MenuChoice::PhysicalNodYes || button.choice == MenuChoice::PhysicalShakeNo ||
        button.choice == MenuChoice::PhysicalDance ||
        (button.choice == MenuChoice::Action && requiresMotion(button.action));
}

inline const Button* pageButtons(Page page, size_t& count) {
#define LAB_PAGE(pageName, buttonsName) \
    case Page::pageName: count = sizeof(buttonsName) / sizeof(Button); return buttonsName
    switch (page) {
        LAB_PAGE(Face, faceButtons); LAB_PAGE(Main, mainButtons); LAB_PAGE(Demo, demoButtons);
        LAB_PAGE(Expressions, expressionButtons); LAB_PAGE(Expressions2, expression2Buttons);
        LAB_PAGE(Expressions3, expression3Buttons); LAB_PAGE(Expressions4, expression4Buttons);
        LAB_PAGE(DemoTouch, demoTouchButtons); LAB_PAGE(Sensors, sensorButtons);
        LAB_PAGE(SensorReadings, sensorReadingsButtons); LAB_PAGE(Nfc, nfcButtons);
        LAB_PAGE(NfcResult, nfcResultButtons);
        LAB_PAGE(Rgb, rgbButtons); LAB_PAGE(Petting, pettingButtons);
        LAB_PAGE(Motion, motionButtons); LAB_PAGE(MotionMove, motionMoveButtons);
        LAB_PAGE(MotionMove2, motionMove2Buttons); LAB_PAGE(MotionMove3, motionMove3Buttons);
        LAB_PAGE(BspYaw, bspYawButtons);
        LAB_PAGE(More, moreButtons); LAB_PAGE(Network, networkButtons);
        LAB_PAGE(NetworkStatus, networkStatusButtons); LAB_PAGE(Settings, settingsButtons);
        LAB_PAGE(DisplayStatus, displayStatusButtons); LAB_PAGE(SetupKey, setupKeyButtons);
        LAB_PAGE(Shutdown, shutdownButtons); LAB_PAGE(Notification, notificationButtons);
        LAB_PAGE(NotificationDetail, notificationDetailButtons);
        LAB_PAGE(NotificationActions, notificationActionsButtons);
        LAB_PAGE(AvatarTest, avatarTestButtons);
        LAB_PAGE(ExpressionPreview, expressionPreviewButtons);
        LAB_PAGE(PhysicalExpressions, physicalExpressionButtons);
        default: count = 0; return nullptr;
    }
#undef LAB_PAGE
}

inline Page parentPage(Page page) {
    switch (page) {
        case Page::Main: return Page::Face;
        case Page::Demo: case Page::Motion: case Page::Settings: case Page::More: return Page::Main;
        case Page::Expressions: case Page::Expressions2: case Page::Expressions3:
        case Page::Expressions4: case Page::DemoTouch: case Page::Sensors: return Page::Demo;
        case Page::MotionMove: case Page::MotionMove2: case Page::MotionMove3:
        case Page::BspYaw: return Page::Motion;
        case Page::SensorReadings: case Page::Nfc: case Page::Rgb: return Page::Sensors;
        case Page::Petting: return Page::Demo;
        case Page::NfcResult: return Page::Nfc;
        case Page::Network: case Page::Shutdown: return Page::More;
        case Page::AvatarTest: return Page::More;
        case Page::ExpressionPreview: return Page::AvatarTest;
        case Page::PhysicalExpressions: return Page::MotionMove3;
        case Page::NetworkStatus: case Page::SetupKey: return Page::Network;
        case Page::DisplayStatus: return Page::Settings;
        default: return Page::Face;
    }
}

inline bool motionPage(Page page) {
    return page == Page::Motion || page == Page::MotionMove ||
        page == Page::MotionMove2 || page == Page::MotionMove3 || page == Page::PhysicalExpressions;
}

class Menu {
public:
    static constexpr uint32_t kTimeoutMs = 30000;
    Page page = Page::Face;
    bool statusStripVisible = true;
    bool faceProtected = false;
    bool faceStopVisible() const { return statusStripVisible || faceProtected; }
    uint32_t revision = 0;
    void touch(uint32_t now) { touchedAt = now; }

    const Button* buttons(size_t& count) const { return pageButtons(page, count); }

    const Button* tap(uint32_t now, int x, int y) {
        if (page == Page::Face) {
            const Button& stop = faceButtons[0];
            if (faceStopVisible() && x >= stop.x && x < stop.x + stop.w && y >= stop.y && y < stop.y + stop.h) {
                touch(now);
                return &stop;
            }
            show(Page::Main, now);
            return nullptr;
        }
        touchedAt = now;
        size_t count;
        const Button* items = buttons(count);
        for (size_t i = 0; i < count; ++i) {
            const Button& item = items[i];
            if (x >= item.x && x < item.x + item.w && y >= item.y && y < item.y + item.h)
                return now - changedAt >= 400 || item.choice == MenuChoice::Stop ? &item : nullptr;
        }
        return nullptr;
    }

    void show(Page value, uint32_t now) {
        page = value;
        ++revision;
        changedAt = touchedAt = now;
    }
    void tick(uint32_t now) {
        if (notificationPage(page)) return;
        if (page == Page::SetupKey && now - changedAt >= 10000) show(Page::Network, now);
        if (page != Page::Face && now - touchedAt >= kTimeoutMs) show(Page::Face, now);
    }

private:
    uint32_t changedAt = 0, touchedAt = 0;
};

// Own the original contact, not M5Unified's movable flick base.
class StatusStripGesture {
public:
    bool update(uint32_t now, bool eligible, uint32_t pageRevision, uint32_t notificationRevision,
                int count, int id, int x, int y, bool began, bool pressed, bool released,
                bool& toggle) {
        toggle = false;
        if (began || (!pressed && !released)) releaseSeen = false;
        if (!tracking && released) return releaseSeen;
        if (!tracking && began) {
            tracking = true;
            faceContact = eligible;
            candidate = eligible && y >= 220 && y < 240;
            cancelled = count != 1 || x < 0 || x >= 320 || y < 0 || y >= 240;
            originX = x;
            originY = y;
            pointId = id;
            startedAt = now;
            page = pageRevision;
            notification = notificationRevision;
        }
        if (!tracking) return false;
        if ((faceContact && !eligible) || page != pageRevision || notification != notificationRevision ||
            ((pressed || released) && count != 1) || id != pointId ||
            x < 0 || x >= 320 || y < 0 || y >= 240 ||
            now - startedAt > 1500)
            cancelled = true;
        const int dx = x > originX ? x - originX : originX - x;
        const int up = originY - y;
        // A cancelled or moved contact cannot become a tap in a different context.
        const bool consume = cancelled || dx > 8 || up > 8 || up < -8;
        if (released) {
            toggle = candidate && !cancelled && up > 50 && up > dx;
            tracking = false;
            releaseSeen = true;
            return consume || toggle;
        }
        if (!pressed) {
            tracking = false;
            return true;
        }
        return consume;
    }

private:
    bool tracking = false, cancelled = false, candidate = false, releaseSeen = false, faceContact = false;
    int originX = 0, originY = 0, pointId = 0;
    uint32_t startedAt = 0, page = 0, notification = 0;
};

}  // namespace lab

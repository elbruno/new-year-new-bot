// Goal: Run the default USB-controlled avatar firmware.
// Responsibilities: Initialize the device, draw expressions, and handle USB
// commands and local touch controls.
// How it works:
// 1. Setup initializes hardware and displays the current avatar.
// 2. The loop advances commands, touch input, and runtime state.
// 3. Changed expressions are drawn from their RGB565 image arrays.
// The Wi-Fi lab uses a separate Arduino entry point.
#ifndef NETBOT_WIFI_LAB
#include <M5Unified.h>
#include <errno.h>
#include <stdlib.h>
#include "AvatarState.h"
#include "DisplayBrightness.h"
#include "LocalMotionMenu.h"
#include "StackChanDefaultMotion.h"
#include "avatar_assets.h"

// AvatarState tracks the visible expression and timed commands.
AvatarState avatar;
lab::LocalMotionMenu menu;
char input[96];
size_t inputLength = 0;
bool inputOverflow = false;
Expression drawn = Expression::Sleep;
uint32_t lastId = 0;
bool lastCompleted = false;
lab::StackChanDefaultMotion defaultMotion;
static constexpr const char* motionSafetyMode = lab::kCheckedDefaultSafetyMode;

// Map an expression to its compiled RGB565 image, with neutral as the fallback.
const uint16_t* pixelsFor(Expression expression) {
    switch (expression) {
        case Expression::Blink:
        case Expression::Sleep: return avatar_blink;
        case Expression::Wink: return avatar_wink;
        case Expression::Smile: return avatar_smile;
        case Expression::LookUp: return avatar_look_up;
        default: return avatar_neutral;
    }
}

constexpr uint16_t kMenuBackground = 0x1083;  // RGB565 dark grey
constexpr uint16_t kMenuText = 0xFFFF;

void showAvatar() {
    // Draw the selected 320x240 pixel array at the display origin, then overlay Stop.
    M5.Display.pushImage(0, 0, 320, 240, pixelsFor(avatar.visible));
    drawn = avatar.visible;
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextSize(2.0f);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    const auto& stop = lab::kLocalMotionButtons[4];
    M5.Display.fillRoundRect(stop.x, stop.y, stop.w, stop.h, 8, stop.color);
    M5.Display.setTextColor(kMenuText, stop.color);
    M5.Display.drawString(stop.label, stop.x + stop.w / 2, stop.y + stop.h / 2);
}

void drawMenu() {
    menu.draw(defaultMotion.controller);
}

void shutDown() {
    const auto stop = defaultMotion.controller.requestStop(millis());
    if (stop != lab::StopResult::Confirmed) {
        menu.show(millis(), stop == lab::StopResult::Pending ?
            "shutdown_waiting_for_stop" : "motion_stop_unconfirmed");
        drawMenu();
        return;
    }
    M5.Display.fillScreen(kMenuBackground);
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextSize(2.0f);
    M5.Display.setTextColor(kMenuText, kMenuBackground);
    M5.Display.drawString("Shutting down...", 160, 120);
    delay(600);
    // M5Unified sleeps the display, asks the PMIC to cut power, and deep-sleeps if power stays on.
    M5.Power.powerOff();
}

void handleMenu(lab::LocalMotionEvent event) {
    switch (event) {
        case lab::LocalMotionEvent::Opened: drawMenu(); break;
        case lab::LocalMotionEvent::Closed: showAvatar(); break;
        case lab::LocalMotionEvent::ShutDown: shutDown(); break;
        case lab::LocalMotionEvent::Enable:
        case lab::LocalMotionEvent::Lock:
        case lab::LocalMotionEvent::Stop:
            menu.perform(event, defaultMotion.controller, millis());
            if (menu.isOpen()) drawMenu();
            else showAvatar();
            break;
        default: break;
    }
}

bool number(const char* text, uint32_t minimum, uint32_t maximum, uint32_t& result) {
    if (!text || !*text || *text == '-' || *text == '+') return false;
    for (const char* digit = text; *digit; ++digit)
        if (*digit < '0' || *digit > '9') return false;
    char* end;
    errno = 0;
    const unsigned long value = strtoul(text, &end, 10);
    if (errno || *end || value < minimum || value > maximum) return false;
    result = static_cast<uint32_t>(value);
    return true;
}

void reply(const char* state, uint32_t id) {
    Serial.printf("%s\t%lu\n", state, static_cast<unsigned long>(id));
}

// Parse the bounded USB protocol and avoid repeating a command with the same ID.
void handleCommand(char* line) {
    if (!strcmp(line, "MOTION")) {
        const auto status = defaultMotion.controller.snapshot();
        Serial.printf("MOTION\t%s\t%s\t%s\n", motionSafetyMode, lab::motionStateName(status.state),
            status.error ? status.error : "none");
        return;
    }
    if (!strcmp(line, "STOP")) {
        defaultMotion.controller.emergencyStop(millis());
        Serial.println("STOP\tunconfirmed");
        return;
    }
#ifdef NETBOT_FIRMWARE_VERSION
    if (!strcmp(line, "VERSION")) {
        Serial.printf("VERSION\t%s\n", NETBOT_FIRMWARE_VERSION);
        return;
    }
#endif
    if (!strcmp(line, "HELLO")) {
        avatar.reset(millis());
        lastId = 0;
        lastCompleted = false;
        Serial.println("AVATAR\t1");
        return;
    }
    const size_t length = strlen(line);
    if (length == 0 || line[0] == '\t' || line[length - 1] == '\t' || strstr(line, "\t\t")) {
        Serial.println("ERR\t0\tinvalid");
        return;
    }
    char* fields[6];
    size_t count = 0;
    char* cursor = nullptr;
    for (char* part = strtok_r(line, "\t", &cursor); part && count < 6;
         part = strtok_r(nullptr, "\t", &cursor)) fields[count++] = part;

    uint32_t id = 0, duration = 0;
    if (count == 4 && !strcmp(fields[0], "CFG")) {
        uint32_t blink, glance, automatic;
        if (avatar.busy()) { Serial.println("ERR\t0\tbusy"); return; }
        if (number(fields[1], 1000, 60000, blink) &&
            number(fields[2], 2000, 120000, glance) &&
            number(fields[3], 0, 1, automatic)) {
            avatar.configure(millis(), blink, glance, automatic != 0);
            Serial.println("CONFIGURED");
            return;
        }
    } else if (count == 4 && !strcmp(fields[0], "SET") &&
               number(fields[1], 1, UINT32_MAX, id)) {
        Expression expression;
        if (!expressionFor(fields[2], expression) || !number(fields[3], 100, 5000, duration)) {
            Serial.printf("ERR\t%lu\tinvalid\n", static_cast<unsigned long>(id));
            return;
        }
        if (id == lastId) {
            reply(lastCompleted ? "DONE" : "OK", id);
            return;
        }
        if (avatar.busy()) {
            Serial.printf("ERR\t%lu\tbusy\n", static_cast<unsigned long>(id));
            return;
        }
        lastId = id;
        lastCompleted = false;
        avatar.apply(millis(), id, expression, duration);
        reply("OK", id);
        return;
    }
    Serial.printf("ERR\t%lu\tinvalid\n", static_cast<unsigned long>(id));
}

void readCommands() {
    // Bound work per loop so a busy USB stream cannot starve the display.
    for (int budget = 0; budget < 96 && Serial.available(); ++budget) {
        const char value = static_cast<char>(Serial.read());
        if (value == '\n') {
            if (inputOverflow) Serial.println("ERR\t0\ttoo_long");
            else {
                input[inputLength] = '\0';
                handleCommand(input);
            }
            inputLength = 0;
            inputOverflow = false;
        } else if (value != '\r') {
            if (value != '\t' && (value < ' ' || value > '~')) inputOverflow = true;
            else if (inputLength + 1 < sizeof(input)) input[inputLength++] = value;
            else inputOverflow = true;
        }
    }
}

void setup() {
    // Initialize hardware and draw the first frame before the recurring loop starts.
    auto config = M5.config();
    M5.begin(config);
    M5.Display.setBrightness(lab::kDisplayBrightnessDefault);
    Serial.begin(115200);
    defaultMotion.begin(millis());
    M5.Display.setRotation(1);
    M5.Display.setSwapBytes(true);
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextSize(2.0f);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    showAvatar();
}

void loop() {
    // Service input, timed state, motion and touch; redraw only when needed.
    M5.update();
    readCommands();
    const uint32_t now = millis();
    defaultMotion.controller.tick(now);
    const uint32_t completed = avatar.update(now);
    const auto touch = M5.Touch.getDetail();
    if (touch.wasClicked()) {
        const auto& stop = lab::kLocalMotionButtons[4];
        if (!menu.isOpen() && touch.x >= stop.x && touch.x < stop.x + stop.w &&
            touch.y >= stop.y && touch.y < stop.y + stop.h)
            handleMenu(lab::LocalMotionEvent::Stop);
        else handleMenu(menu.tap(now, touch.x, touch.y));
    }
    handleMenu(menu.update(now));
    if (menu.isOpen() && menu.changed(defaultMotion.controller)) drawMenu();
    if (!menu.isOpen() && avatar.visible != drawn) showAvatar();
    if (completed != 0) {
        lastCompleted = true;
        reply("DONE", completed);
    }
}
#endif

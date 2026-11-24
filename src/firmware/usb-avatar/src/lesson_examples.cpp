#ifdef NETBOT_LESSON_EXAMPLES
#include <M5Unified.h>
#include <M5StackChan.h>
#include <WiFi.h>
#include "DisplayBrightness.h"
#include "StackChanDefaultMotion.h"
#include "LocalMotionMenu.h"

#ifndef NETBOT_FIRMWARE_VERSION
#error "Build with PlatformIO to select a catalog version."
#endif

namespace lesson {

lab::StackChanDefaultMotion defaultMotion;
static constexpr const char* motionSafetyMode = lab::kCheckedDefaultSafetyMode;
lab::LocalMotionMenu menu(true);

void helloDisplay() {
    menu.draw(defaultMotion.controller, "Hello v" NETBOT_FIRMWARE_VERSION);
    M5.Display.setTextColor(0xFFFF, lab::LocalMotionMenu::background);
    M5.Display.drawString(motionSafetyMode, 160, 116);
}

void connectWifi(const char* ssid, const char* password) {
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.begin(ssid, password);
    // Check WiFi.status() from loop(), never wait here.
}

bool readAcceleration(float& x, float& y, float& z) {
    return M5.Imu.getAccel(&x, &y, &z);
}

// API demonstration only. Requires an initialized, calibrated motion adapter.
// Never called by this sketch or the lab; reviewed limits, calibration and explicit authorization are required.
void pitchApi(stackchan::motion::Motion& verifiedMotion, int validatedBspTarget) {
    verifiedMotion.movePitch(validatedBspTarget, 100);
}

}  // namespace lesson

void setup() {
    auto config = M5.config();
    config.internal_mic = false;
    M5.begin(config);
    M5.Display.setBrightness(lab::kDisplayBrightnessDefault);
    M5.Display.setRotation(1);
    Serial.begin(115200);
    lesson::defaultMotion.begin(millis());
    lesson::helloDisplay();
}

void loop() {
    M5.update();
    if (M5.Touch.getDetail().wasClicked()) {
        const auto touch = M5.Touch.getDetail();
        const auto event = lesson::menu.tap(millis(), touch.x, touch.y);
        lesson::menu.perform(event, lesson::defaultMotion.controller, millis());
        lesson::helloDisplay();
    }
    lesson::defaultMotion.controller.tick(millis());
    if (lesson::menu.changed(lesson::defaultMotion.controller)) lesson::helloDisplay();
    delay(1);
}
#endif

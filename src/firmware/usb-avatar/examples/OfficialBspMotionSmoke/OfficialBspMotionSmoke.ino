// The BSP enables servo rail power at startup, initializes each servo with
// torque off, and may write default zero positions to NVS. Motion.stop()
// schedules position commands on both axes; it is not verified torque-off.

#include <M5StackChan.h>

bool touchReleased = false;
bool testStarted = false;

void setup()
{
    Serial.begin(115200);
    M5StackChan.begin();
    auto& display = M5StackChan.Display();
    display.fillScreen(TFT_BLACK);
    display.setTextSize(2);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setCursor(0, 0);
    display.println("READY - TOUCH TO TEST");
    Serial.println("READY - TOUCH TO TEST");
}

void loop()
{
    M5StackChan.update();
    const auto touch = M5.Touch.getDetail();

    if (!touch.isPressed()) {
        touchReleased = true;
    } else if (touchReleased && !testStarted && touch.wasPressed()) {
        testStarted = true;
        M5StackChan.Motion.rotateX(300);
        delay(800);
        M5StackChan.Motion.stop();
        M5StackChan.Display().fillScreen(TFT_BLACK);
        M5StackChan.Display().setCursor(0, 0);
        M5StackChan.Display().println("TEST COMPLETE");
        Serial.println("TEST COMPLETE");
    }

    delay(10);
}

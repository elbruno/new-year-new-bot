// This entry point is compiled for the Wi-Fi lab, not the USB or lesson profiles.
#if defined(NETBOT_WIFI_LAB) && !defined(NETBOT_LESSON_EXAMPLES)
#include <M5Unified.h>
#include <M5StackChan.h>
#include <pgmspace.h>
#include <stdarg.h>
#include <cmath>
#include "AvatarComparison.h"
#include "DisplayBrightness.h"
#include "DemoSensors.h"
#include "DemoNfc.h"
#include "LabDevice.h"
#include "LabMenu.h"
#include "LabPlatform.h"
#include "LabUsbLog.h"
#include "StackChanMotion.h"
#include "StackChanDefaultMotion.h"
#include "BspMotionTest.h"
#if !defined(NETBOT_TEST_BUILD)
#include "AudioTestHardware.h"
#if !defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC) && !defined(NETBOT_TWO_AXIS_TEST) && !defined(NETBOT_YAW_BENCH)
#include "ImageDisplayHardware.h"
#endif
#endif
#ifndef NETBOT_TEST_BUILD
#include <esp_timer.h>
#include "BspYawWriteTrace.h"
extern lab::BspYawWriteTrace bspYawWriteTrace;
#endif
#if !defined(NETBOT_TEST_BUILD) && !defined(NETBOT_TWO_AXIS_TEST) && !defined(NETBOT_YAW_BENCH)
#include "StackChanManualUart.h"
#endif
#include "LabMotionTestConfig.h"
#include "StatusIcons.h"
#include "avatar_assets.h"

#if defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC) && \
    (defined(NETBOT_TWO_AXIS_TEST) || defined(NETBOT_YAW_BENCH) || defined(NETBOT_LESSON_EXAMPLES) || \
     defined(NETBOT_UNIT_MOTION_RECORD_PRESENT))
#error "Manual axis diagnostics cannot be combined with another motion profile or a unit record."
#endif

#ifndef NETBOT_FIRMWARE_VERSION
#error "Build with release_metadata.py or PlatformIO to select a catalog version."
#endif

constexpr char kFirmwareVersionLabel[] = "v" NETBOT_FIRMWARE_VERSION;
static_assert((sizeof(kFirmwareVersionLabel) - 1) * status_icons::kVersionGlyphWidth <=
              status_icons::kVersionRight - status_icons::kVersionLeft, "Firmware version is too wide.");

class Speaker : public lab::Audio {
public:
    lab::AudioTest* test = nullptr;
    bool available() const override { return M5.Speaker.isEnabled(); }
    bool play(uint32_t duration) override {
        return (!test || !test->resourceBusy()) && M5.Speaker.tone(880, duration);
    }
    bool playTone(uint32_t frequency, uint32_t duration) override {
        return (!test || !test->resourceBusy()) && frequency && duration && M5.Speaker.isEnabled() && M5.Speaker.isRunning() &&
            !M5.Speaker.isPlaying() && M5.Speaker.tone(frequency, duration, -1, false);
    }
    void stop() override {
        if (test) test->cancel(millis());
        M5.Speaker.stop();
    }
};

Speaker speaker;
#if !defined(NETBOT_TEST_BUILD)
lab::AudioTestHardware audioTestHardware;
lab::AudioTest audioTest(audioTestHardware);
#if !defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC) && !defined(NETBOT_TWO_AXIS_TEST) && !defined(NETBOT_YAW_BENCH)
lab::ImageDisplayHardware imageDisplayHardware;
lab::ImageDisplay imageDisplay(imageDisplayHardware);
lab::TemporaryText temporaryText;
#endif
#endif
#if defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC)
constexpr bool twoAxisTestBuild = false;
constexpr bool yawBenchBuild = false;
constexpr bool remoteYawBenchBuild = false;
constexpr char motionSafetyMode[] = "manual-axis-diagnostic";
#elif defined(NETBOT_TWO_AXIS_TEST)
constexpr bool twoAxisTestBuild = true;
constexpr bool yawBenchBuild = false;
constexpr bool remoteYawBenchBuild = false;
constexpr char motionSafetyMode[] = "two-axis-test-v1";
#elif defined(NETBOT_REMOTE_YAW_BENCH) && defined(NETBOT_YAW_BENCH) && !defined(NETBOT_LESSON_EXAMPLES)
constexpr bool twoAxisTestBuild = false;
constexpr bool yawBenchBuild = true;
constexpr bool remoteYawBenchBuild = true;
constexpr char motionSafetyMode[] = "remote-yaw-bench-v1";
#elif defined(NETBOT_YAW_BENCH) && !defined(NETBOT_LESSON_EXAMPLES)
constexpr bool twoAxisTestBuild = false;
constexpr bool yawBenchBuild = true;
constexpr bool remoteYawBenchBuild = false;
constexpr char motionSafetyMode[] = "local-yaw-bench-v1";
#else
constexpr bool twoAxisTestBuild = false;
constexpr bool yawBenchBuild = false;
constexpr bool remoteYawBenchBuild = false;
constexpr const char* motionSafetyMode = lab::kCheckedDefaultSafetyMode;
#endif
constexpr bool checkedDefaultBuild = !yawBenchBuild && !twoAxisTestBuild;
#if !defined(NETBOT_TWO_AXIS_TEST) && !defined(NETBOT_YAW_BENCH)
#if defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC)
lab::StackChanDefaultMotion defaultMotion(lab::ReviewedUnitMotion{});
#else
lab::StackChanDefaultMotion defaultMotion;
#endif
auto& motionController = defaultMotion.controller;
#else
const lab::MotionLimits motionLimits = twoAxisTestBuild ? lab::kMotionTestConfig.limits :
    lab::yawBenchLimits();
lab::StackChanScsBus motionBus(true);
lab::StackChanCalibration motionCalibration;
lab::ScsMotionDriver<lab::StackChanScsBus, lab::StackChanCalibration>
    motionDriver(motionBus, motionCalibration, motionLimits, yawBenchBuild, twoAxisTestBuild, checkedDefaultBuild);
lab::MotionController motionController(&motionDriver, motionLimits, yawBenchBuild, remoteYawBenchBuild,
    twoAxisTestBuild, twoAxisTestBuild ? lab::kMotionTestConfig.calibration : lab::Pose{},
    twoAxisTestBuild ? lab::kMotionTestConfig.home : lab::Pose{});
#endif
#if !defined(NETBOT_TEST_BUILD) && !defined(NETBOT_TWO_AXIS_TEST) && !defined(NETBOT_YAW_BENCH)
lab::ManualStopLatch manualStopLatch;
lab::StackChanManualUartApi manualUartApi;
lab::ManualUart<lab::StackChanManualUartApi> manualUart(manualUartApi, manualStopLatch);
lab::ManualScs<decltype(manualUart)> manualBus(manualUart);
lab::ManualSession<decltype(manualBus), decltype(manualUart),
    lab::StackChanScsBus, lab::StackChanCalibration> manualSession(
        manualBus, manualUart, defaultMotion.bus, defaultMotion.calibration, lab::kCheckedMotionBounds);
lab::Device labDevice(speaker, &motionController, &manualSession, &manualStopLatch,
#if defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC)
    true, true
#else
    false, false
#endif
);
#else
lab::Device labDevice(speaker, &motionController);
#endif
lab::Platform labPlatform(labDevice);
lab::BspMotionTest bspMotionTest(labDevice.bspYawStop);
lab::RemoteLog remoteLog;
lab::BspMotionTest::State drawnBspState = lab::BspMotionTest::State::Idle;
bool bspInitialized = false;
bool homeAvailable = false;
lab::HomeCaptureResult homeCaptureResult = lab::HomeCaptureResult::None;
std::atomic<lab::HomeCaptureResult> pendingHomeCaptureResult{lab::HomeCaptureResult::None};

void motionTestLog(uint32_t now, const char* format, ...) {
    char line[384];
    const int prefix = snprintf(line, sizeof(line), "[%lu] MOTION_TEST ", static_cast<unsigned long>(now));
    if (prefix < 0 || static_cast<size_t>(prefix) >= sizeof(line)) { remoteLog.lose(); return; }
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(line + prefix, sizeof(line) - prefix, format, args);
    va_end(args);
    if (length < 0 || static_cast<size_t>(length) >= sizeof(line) - prefix) { remoteLog.lose(); return; }
    remoteLog.append(line, static_cast<size_t>(prefix + length));
}

#ifndef NETBOT_TEST_BUILD
void traceYawWrites(bool start, uint32_t now, lab::MotionTestLog log) {
    if (start) {
        bspYawWriteTrace.start(esp_timer_get_time());
        return;
    }
    const auto s = bspYawWriteTrace.finish();
    if (!log) return;
    log(now, "YAW_WRITE_TIMING writes=%u first_us=%lu span_us=%lu min_gap_us=%lu max_gap_us=%lu "
        "max_gap_at_us=%lu prior_write_us=%lu gaps_over_25ms=%u max_write_us=%lu no_ack=%u",
        s.writes, static_cast<unsigned long>(s.firstUs), static_cast<unsigned long>(s.lastUs - s.firstUs),
        static_cast<unsigned long>(s.minGapUs), static_cast<unsigned long>(s.maxGapUs),
        static_cast<unsigned long>(s.maxGapAtUs), static_cast<unsigned long>(s.priorWriteAtMaxGapUs),
        s.gapsOver25ms, static_cast<unsigned long>(s.maxWriteUs), s.noAck);
    log(now, "YAW_WRITE_STEPS first_raw=%u last_raw=%u identical=%u max_step_raw=%u step_at_max_gap_raw=%u "
        "physical=unverified",
        s.firstRaw, s.lastRaw, s.identical, s.maxStepRaw, s.stepAtMaxGapRaw);
}
#else
constexpr auto traceYawWrites = nullptr;
#endif

const char* motionTestStatus() {
    using State = lab::BspMotionTest::State;
    if (labDevice.bspYawStop.load() && bspMotionTest.state() != State::Failed) return "STOP REQUESTED";
    switch (bspMotionTest.state()) {
        case State::Preparing: return "PREPARING (physical cutoff)";
        case State::Running: return "RUNNING";
        case State::Complete: return "COMPLETED (UNVERIFIED)";
        case State::Stopped: return "STOP REQUESTED";
        case State::Failed: return "FAILED";
        case State::Rejected: return "REJECTED";
        case State::Uncertain: return "UNCERTAIN";
        default: return "READY";
    }
}

const char* homeSetupStatus() {
    using Result = lab::HomeCaptureResult;
    switch (homeCaptureResult) {
        case Result::Verified: return "Home saved (NVS verified)";
        case Result::Failed: return "Capture failed: Home setup required";
        case Result::Inconclusive: return homeAvailable ?
            "Capture inconclusive" : "Capture inconclusive: setup required";
        default: return homeAvailable ? "Home references available" : "Home setup required";
    }
}

lab::HomeReferenceSnapshot readHomeReferences() {
    lab::Pose home{};
    lab::CalibrationObservation observation{};
    lab::StackChanCalibration{}.read(home, &observation);
    lab::HomeReferenceSnapshot reference;
    reference.openCode = observation.openCode;
    reference.yawCode = observation.yaw.code;
    reference.yawRead = observation.yaw.hasValue();
    reference.yawValid = observation.yaw.valid();
    reference.yawRaw = observation.yaw.raw;
    reference.pitchCode = observation.pitch.code;
    reference.pitchRead = observation.pitch.hasValue();
    reference.pitchValid = observation.pitch.valid();
    reference.pitchRaw = observation.pitch.raw;
    return reference;
}

const char* startBspMotionTest(lab::BspMotionTest::Action action, bool reserved = false,
                               bool explicitGrumpy = false) {
#if !defined(NETBOT_TWO_AXIS_TEST) && !defined(NETBOT_YAW_BENCH) && !defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC)
    if (action == lab::BspMotionTest::Action::GrumpyShake && !explicitGrumpy) {
        if (reserved) bspMotionTest.rejectPending("physical_safety_unavailable");
        return "physical_safety_unavailable";
    }
    if (!reserved && !bspMotionTest.reserve(action)) return "test_busy_failed_or_stopped";
    const uint32_t bootId = labDevice.runtime.snapshot().bootId;
    const bool alreadyInitialized = bspInitialized;
    motionTestLog(millis(), "START experiment=%s bootId=%lu version=%s "
        "bsp=fb5e9f5697803ca64e45fe7ad8eaea3e4100f766 implementation=original physical=unverified",
        lab::BspMotionTest::actionName(action), static_cast<unsigned long>(bootId),
        NETBOT_FIRMWARE_VERSION);
    motionTestLog(millis(), "BSP_INIT_START requested=%u already_initialized=%u",
        !alreadyInitialized, alreadyInitialized);
    if (labDevice.bspYawStop.load()) {
        bspMotionTest.cancelPending();
        return "test_stopped";
    }
    if (const char* error = bspMotionTest.directionReferenceError(action)) {
        bspMotionTest.rejectPending(error);
        return error;
    }
    if (lab::BspMotionTest::requiresHome(action)) {
        if (const char* error = bspMotionTest.motionHomeError(readHomeReferences())) {
            bspMotionTest.rejectPending(error);
            return error;
        }
    }
    if (!bspInitialized) {
        if (!defaultMotion.bus.reserveForOfficialBsp()) {
            bspMotionTest.fail("uart_ownership", 0, true);
            motionTestLog(millis(), "FAILED stage=uart_ownership code=0 physical=unverified");
            return "servo_uart_owned_or_stopped";
        }
        if (labDevice.bspYawStop.load()) {
            bspMotionTest.cancelPending();
            return "test_stopped";
        }
        labDevice.bspYawUnverified.store(true, std::memory_order_release);
        const uint32_t initStart = millis();
        M5StackChan.begin();
        bspInitialized = true;
        motionTestLog(millis(), "BSP_INIT_DONE elapsed_ms=%lu",
            static_cast<unsigned long>(millis() - initStart));
    } else {
        motionTestLog(millis(), "BSP_INIT_DONE elapsed_ms=0 already_initialized=1");
    }
    const auto created = xTaskCreate([](void* freshBspInit) {
        bspMotionTest.run(M5StackChan, [] { return millis(); },
            [](uint32_t ms) {
                const TickType_t ticks = pdMS_TO_TICKS(ms);
                vTaskDelay(ticks ? ticks : 1);
            }, motionTestLog, 0, true, readHomeReferences, [](lab::HomeCaptureResult result) {
                pendingHomeCaptureResult.store(result, std::memory_order_release);
            }, traceYawWrites, freshBspInit != nullptr);
        vTaskDelete(nullptr);
    }, "bsp_motion_test", 4096, alreadyInitialized ? nullptr : &bspMotionTest, 5, nullptr);
    if (created != pdPASS) {
        bspMotionTest.fail("task_creation", created);
        motionTestLog(millis(), "FAILED stage=task_creation code=%d", created);
        return "bsp_task_failed";
    }
    return nullptr;
#else
    (void)action;
    (void)reserved;
    (void)explicitGrumpy;
    return "bsp_test_requires_normal_wifi_build";
#endif
}
#if defined(NETBOT_REMOTE_YAW_BENCH) || defined(NETBOT_TWO_AXIS_TEST) || defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC)
lab::UsbLog<decltype(Serial)> usbLog(Serial, NETBOT_FIRMWARE_VERSION, &remoteLog);
#endif
lab::Menu labMenu;
uint32_t drawnAvatarComparisonRevision = UINT32_MAX;
uint32_t demoTouchRevision = UINT32_MAX;
int demoTouchX = 0, demoTouchY = 0;
lab::DemoSensors<decltype(M5.In_I2C)> demoSensors(M5.In_I2C);
#if !defined(NETBOT_TEST_BUILD)
lab::DemoNfc demoNfc([] { return !lab::cameraSccb.uncertain.load(); });
#else
lab::DemoNfc demoNfc;
#endif
uint32_t drawnNfcRevision = UINT32_MAX;
uint32_t drawnSensorsRevision = UINT32_MAX;
uint32_t observedStopEpoch = 0;
lab::StatusStripGesture statusStripGesture;
// Remember the last rendered state so unchanged display regions need not be redrawn.
Expression labDrawn = Expression::Sleep;
lab::Indicator drawnIndicator = lab::Indicator::None;
const char* drawnRuntimeError = nullptr;
bool drawnArmTorqueFault = false;
lab::Page drawnPage = lab::Page::Network;
bool drawnIdle = false;
uint8_t drawnMotionIndex = UINT8_MAX;
lab::IdleEyes drawnIdleEyes = lab::IdleEyes::Neutral;
int drawnStrip[status_icons::kSlotCount] = {
    status_icons::kUnset, status_icons::kUnset, status_icons::kUnset, status_icons::kUnset};
uint16_t statusPixels[status_icons::kPixels];
uint16_t avatarPreviewPixels[320 * 4];
lab::PettingAnimation drawnPettingAnimation = lab::PettingAnimation::Current;
uint8_t drawnPettingStage = 0;
int drawnGrumpyOffset = 0;
lab::BspMotionTest::Action motionExpression = lab::BspMotionTest::Action::None;
lab::BspMotionTest::Action drawnMotionExpression = lab::BspMotionTest::Action::None;
bool motionExpressionStarted = false;
uint32_t motionExpressionStartedAt = 0;
uint8_t drawnMotionExpressionFrame = UINT8_MAX;
lab::MenuChoice expressionPreview = lab::MenuChoice::None;
uint32_t expressionPreviewStartedAt = 0, expressionPreviewStopGeneration = 0;
int drawnExpressionPreviewFrame = INT32_MIN;
uint32_t lastPettingFrameAt = 0;
uint32_t lastNetworkDraw = 0;
uint32_t lastBatteryRead = 0;
int32_t batteryLevel = -1;
bool batteryCharging = false;
bool batteryRead = false;
bool drawnVersion = false;
bool drawnStripVisible = true;
bool drawnFaceStopVisible = true;
bool pettingEnabled = true, pettingSoundEnabled = true, topReleased = false;
bool topSettleStarted = false, topSettleComplete = false;
uint32_t topSettleAt = 0;
uint8_t topReleaseSamples = 0, topTouchSamples = 0;
const char* localError = nullptr;
bool shutdownPending = false;
bool localStopPending = false;
bool localManualStopPending = false;
uint32_t localManualStopId = 0;
uint32_t localStopGeneration = 0, localStopBoot = 0;
lab::InteractionOrigin localStopOrigin = lab::InteractionOrigin::Unknown;
bool shutdownIssued = false;
lab::DisplayBrightnessStatus drawnSettingsBrightness{};
bool drawnSettingsBrightnessAvailable = false;
bool drawnSettingsStatus = false;
const char* drawnSettingsError = nullptr;
lab::NotificationSnapshot drawnNotification{};

constexpr int kEyeX = 104, kEyeY = 106, kEyeWidth = 112, kEyeHeight = 44;
constexpr int kAntennaX = 140, kAntennaY = 20, kAntennaWidth = 41, kAntennaHeight = 33;
constexpr int kLedX = 147, kLedY = 158, kLedWidth = 26, kLedHeight = 20;
constexpr int kPettingBeltX = 88, kPettingBeltY = 155, kPettingBeltWidth = 144, kPettingBeltHeight = 26;
constexpr int kPettingAntennaHeight = 40;
constexpr int kGrumpyX = 78, kGrumpyY = 20, kGrumpyWidth = 164, kGrumpyHeight = 188;
constexpr int kGrumpyMaxOffset = 4;
static_assert(kGrumpyX + kGrumpyWidth + kGrumpyMaxOffset <= lab::faceButtons[0].x &&
    kGrumpyY + kGrumpyHeight <= status_icons::kStripY, "Grumpy must not overlap fixed UI");
constexpr uint32_t kBatteryPollMs = 30000;
constexpr uint16_t kFaultBannerBackground = 0x9000;
constexpr int kAvatarPreviewRows = 4;

uint32_t interpolateChannel(uint32_t p00, uint32_t p10, uint32_t p01, uint32_t p11,
                            uint32_t fractionX, uint32_t fractionY) {
    const uint32_t top = p00 * (256 - fractionX) + p10 * fractionX;
    const uint32_t bottom = p01 * (256 - fractionX) + p11 * fractionX;
    return (top * (256 - fractionY) + bottom * fractionY + 32768) >> 16;
}

uint16_t interpolate565(const uint16_t* image, float sourceX, float sourceY) {
    if (sourceX < 0.0f || sourceX > 319.0f || sourceY < 0.0f || sourceY > 239.0f) return 0;
    int x0 = static_cast<int>(floorf(sourceX));
    int y0 = static_cast<int>(floorf(sourceY));
    uint32_t fractionX = static_cast<uint32_t>((sourceX - x0) * 256.0f + 0.5f);
    uint32_t fractionY = static_cast<uint32_t>((sourceY - y0) * 256.0f + 0.5f);
    if (fractionX == 256) { ++x0; fractionX = 0; }
    if (fractionY == 256) { ++y0; fractionY = 0; }
    if (x0 > 319 || y0 > 239) return 0;
    const int x1 = x0 < 319 ? x0 + 1 : x0;
    const int y1 = y0 < 239 ? y0 + 1 : y0;
    const uint16_t p00 = pgm_read_word(image + y0 * 320 + x0);
    const uint16_t p10 = pgm_read_word(image + y0 * 320 + x1);
    const uint16_t p01 = pgm_read_word(image + y1 * 320 + x0);
    const uint16_t p11 = pgm_read_word(image + y1 * 320 + x1);
    const uint16_t red = static_cast<uint16_t>(interpolateChannel(
        (p00 >> 11) & 0x1F, (p10 >> 11) & 0x1F, (p01 >> 11) & 0x1F, (p11 >> 11) & 0x1F,
        fractionX, fractionY));
    const uint16_t green = static_cast<uint16_t>(interpolateChannel(
        (p00 >> 5) & 0x3F, (p10 >> 5) & 0x3F, (p01 >> 5) & 0x3F, (p11 >> 5) & 0x3F,
        fractionX, fractionY));
    const uint16_t blue = static_cast<uint16_t>(interpolateChannel(
        p00 & 0x1F, p10 & 0x1F, p01 & 0x1F, p11 & 0x1F, fractionX, fractionY));
    return static_cast<uint16_t>((red << 11) | (green << 5) | blue);
}

uint16_t blend565(uint16_t first, uint16_t second, uint16_t alpha) {
    const uint16_t red = static_cast<uint16_t>((((first >> 11) & 0x1F) * (256 - alpha) +
        ((second >> 11) & 0x1F) * alpha + 128) >> 8);
    const uint16_t green = static_cast<uint16_t>((((first >> 5) & 0x3F) * (256 - alpha) +
        ((second >> 5) & 0x3F) * alpha + 128) >> 8);
    const uint16_t blue = static_cast<uint16_t>(((first & 0x1F) * (256 - alpha) +
        (second & 0x1F) * alpha + 128) >> 8);
    return static_cast<uint16_t>((red << 11) | (green << 5) | blue);
}

void drawAvatarComparisonImage(const lab::AvatarComparisonPose& pose) {
    const uint16_t alpha = static_cast<uint16_t>(pose.petting * 256.0f + 0.5f);
    for (int top = 0; top < 240; top += kAvatarPreviewRows) {
        for (int row = 0; row < kAvatarPreviewRows; ++row) {
            const auto transform = lab::avatarComparisonRow(pose, static_cast<float>(top + row));
            for (int x = 0; x < 320; ++x) {
                uint16_t pixel = interpolate565(avatar_neutral,
                    transform.sourceX(static_cast<float>(x)), transform.sourceY);
                if (alpha) {
                    const uint16_t smile = interpolate565(avatar_smile,
                        transform.sourceX(static_cast<float>(x)), transform.sourceY);
                    pixel = blend565(pixel, smile, alpha);
                }
                avatarPreviewPixels[row * 320 + x] = pixel;
            }
        }
        M5.Display.pushImage(0, top, 320, kAvatarPreviewRows, avatarPreviewPixels);
    }
}

int grumpyShakeOffset(const lab::PettingAnimationState& petting, uint32_t now) {
    if (!petting.active || petting.sequenceStage != 3 ||
        petting.animation != lab::PettingAnimation::Grumpy) return 0;
    const uint32_t frame = (now - petting.startedAt) / avatar_grumpy_frame_ms;
    return frame < sizeof(avatar_grumpy_offsets) / sizeof(avatar_grumpy_offsets[0]) ?
        avatar_grumpy_offsets[frame] : 0;
}

const uint16_t* motionExpressionPixels(lab::BspMotionTest::Action action, uint8_t frame) {
    using Action = lab::BspMotionTest::Action;
    switch (action) {
        case Action::NodYes: return avatar_motion_nod_yes[frame];
        case Action::ShakeNo: return avatar_motion_shake_no[frame];
        case Action::Dance: return avatar_motion_dance[frame];
        default: return nullptr;
    }
}

void selectMotionExpression(lab::BspMotionTest::Action action) {
    motionExpression = action == lab::BspMotionTest::Action::GrumpyShake ||
        motionExpressionPixels(action, 0) ? action : lab::BspMotionTest::Action::None;
    motionExpressionStarted = false;
}

uint8_t motionExpressionFrame(uint32_t now, uint32_t startedAt) {
    const uint32_t step = (now - startedAt) / avatar_motion_frame_ms;
    return avatar_motion_sequence[step % (sizeof(avatar_motion_sequence) / sizeof(avatar_motion_sequence[0]))];
}

void drawGrumpyFrame(int offset) {
    const int left = kGrumpyMaxOffset + offset;
    const int right = kGrumpyMaxOffset - offset;
    if (left) M5.Display.fillRect(kGrumpyX - kGrumpyMaxOffset, kGrumpyY,
                                  left, kGrumpyHeight, 0);
    if (right) M5.Display.fillRect(kGrumpyX + offset + kGrumpyWidth, kGrumpyY,
                                   right, kGrumpyHeight, 0);
    M5.Display.pushImage(kGrumpyX + offset, kGrumpyY, kGrumpyWidth, kGrumpyHeight,
                         avatar_petting_grumpy_composed[0]);
}

void drawPettingExpression(uint8_t stage) {
    if (stage == 1) {
        M5.Display.pushImage(kEyeX, kEyeY, kEyeWidth, kEyeHeight, avatar_petting_happy_face[0]);
        M5.Display.pushImage(kPettingBeltX, kPettingBeltY, kPettingBeltWidth,
                             kPettingBeltHeight, avatar_petting_happy_belt[0]);
        M5.Display.pushImage(kAntennaX, kAntennaY, kAntennaWidth, kPettingAntennaHeight,
                             avatar_petting_happy_antenna[0]);
    } else if (stage == 2) {
        M5.Display.pushImage(kEyeX, kEyeY, kEyeWidth, kEyeHeight, avatar_petting_very_happy_face[0]);
        M5.Display.pushImage(kPettingBeltX, kPettingBeltY, kPettingBeltWidth,
                             kPettingBeltHeight, avatar_petting_very_happy_belt[0]);
        M5.Display.pushImage(kAntennaX, kAntennaY, kAntennaWidth, kPettingAntennaHeight,
                             avatar_petting_very_happy_antenna[0]);
    }
}

void drawAvatarPreview(uint32_t now) {
    lab::AvatarComparisonPose pose;
    uint32_t remainingMs;
    {
        std::lock_guard<std::mutex> lock(labPlatform.avatarComparisonMutex);
        pose = labPlatform.avatarComparison.pose(now);
        remainingMs = labPlatform.avatarComparison.remainingMs(now);
    }
    drawAvatarComparisonImage(pose);
    const auto style = M5.Display.getTextStyle();
    const auto* font = M5.Display.getFont();
    for (const auto& button : lab::avatarTestButtons) {
        const uint16_t color = lab::buttonColor(button.choice);
        const bool sampleButton = button.choice == lab::MenuChoice::AvatarSampleA ||
            button.choice == lab::MenuChoice::AvatarSampleB ||
            button.choice == lab::MenuChoice::AvatarSampleC ||
            button.choice == lab::MenuChoice::AvatarSampleD;
        M5.Display.fillRoundRect(button.x, button.y, button.w, button.h, 4, color);
        M5.Display.setFont(&fonts::Font0);
        M5.Display.setTextSize(sampleButton ? 2.0f : 1.0f);
        M5.Display.setTextDatum(textdatum_t::middle_center);
        M5.Display.setTextColor(0xFFFF, color);
        M5.Display.drawString(button.label, button.x + button.w / 2, button.y + button.h / 2);
    }
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextSize(1.0f);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(0xFFFF, 0x0000);
    char status[48];
    snprintf(status, sizeof(status), "%s  %lu ms", lab::avatarComparisonLabel(pose.sample),
        static_cast<unsigned long>(remainingMs));
    M5.Display.fillRect(0, 216, 320, 24, 0x0000);
    M5.Display.drawString(status, 160, 228);
    M5.Display.setFont(font);
    M5.Display.setTextStyle(style);
}

void invalidateStatusStrip() {
    for (int& key : drawnStrip) key = status_icons::kUnset;
    drawnVersion = false;
}

void drawFirmwareVersion() {
    if (drawnVersion) return;
    const auto style = M5.Display.getTextStyle();
    const auto* font = M5.Display.getFont();
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextSize(2.0f);
    M5.Display.setTextDatum(textdatum_t::middle_right);
    M5.Display.setTextColor(0xFFFF, 0);
    M5.Display.drawString(kFirmwareVersionLabel, status_icons::kVersionRight, status_icons::kVersionY);
    M5.Display.setFont(font);
    M5.Display.setTextStyle(style);
    drawnVersion = true;
}

template <typename Renderer>
void drawStatusIcon(int slot, int key, Renderer render) {
    if (key == drawnStrip[slot]) return;
    render(statusPixels);
    M5.Display.pushImage(status_icons::slotX(slot), status_icons::kStripY,
                         status_icons::kSize, status_icons::kSize, statusPixels);
    drawnStrip[slot] = key;
}

void updateBattery(uint32_t now) {
    if (batteryRead && now - lastBatteryRead < kBatteryPollMs) return;
    const int32_t level = M5.Power.getBatteryLevel();
    batteryLevel = level >= 0 && level <= 100 ? level : -1;
    batteryCharging = batteryLevel >= 0 && static_cast<int>(M5.Power.isCharging()) == 1;
    lastBatteryRead = now;
    batteryRead = true;
}

// Select the compiled image for the current runtime expression.
const uint16_t* labPixels(Expression expression) {
    switch (expression) {
        case Expression::Blink:
        case Expression::Sleep: return avatar_blink;
        case Expression::Wink: return avatar_wink;
        case Expression::Smile: return avatar_smile;
        case Expression::LookUp: return avatar_look_up;
        case Expression::LookLeft: return avatar_look_left;
        case Expression::LookRight: return avatar_look_right;
        case Expression::Sleepy: return avatar_sleepy;
        default: return avatar_neutral;
    }
}

// The strip lives on the avatar frame, so any full-frame push erases it and must invalidate it.
void drawStatusStrip(uint32_t now, bool visible, bool quiet) {
    if (!visible) {
        invalidateStatusStrip();
        return;
    }
    const auto& network = labPlatform.network;
    const status_icons::Wifi wifi = status_icons::wifiIcon(network.state, network.ap, network.error);
    drawStatusIcon(0, status_icons::key(wifi, now), [wifi, now](uint16_t* pixels) {
        status_icons::render(wifi, status_icons::phase(now), pixels);
    });
    drawStatusIcon(1, status_icons::quietKey(quiet), [quiet](uint16_t* pixels) {
        status_icons::renderQuiet(quiet, pixels);
    });
    drawStatusIcon(2, status_icons::batteryKey(batteryLevel, batteryCharging),
        [](uint16_t* pixels) { status_icons::renderBattery(batteryLevel, batteryCharging, pixels); });
    const bool companionPresent = labPlatform.companionPresent(now);
    drawStatusIcon(3, status_icons::companionKey(companionPresent), [companionPresent](uint16_t* pixels) {
        status_icons::renderCompanion(companionPresent, pixels);
    });
    drawFirmwareVersion();
}

void drawExpressionPreview(uint32_t now, bool quiet) {
    const bool first = drawnPage != lab::Page::ExpressionPreview;
    if (first) {
        M5.Display.pushImage(0, 0, 320, 240, avatar_neutral);
        invalidateStatusStrip();
        drawnExpressionPreviewFrame = INT32_MIN;
    }
    const bool grumpy = expressionPreview == lab::MenuChoice::PreviewGrumpy;
    const auto action = expressionPreview == lab::MenuChoice::PreviewNodYes ?
        lab::BspMotionTest::Action::NodYes :
        expressionPreview == lab::MenuChoice::PreviewShakeNo ? lab::BspMotionTest::Action::ShakeNo :
        lab::BspMotionTest::Action::Dance;
    const int frame = grumpy ?
        grumpyShakeOffset({true, lab::PettingAnimation::Grumpy, expressionPreviewStartedAt, 3}, now) :
        motionExpressionFrame(now, expressionPreviewStartedAt);
    if (first || frame != drawnExpressionPreviewFrame) {
        if (grumpy) drawGrumpyFrame(frame);
        else M5.Display.pushImage(kGrumpyX, kGrumpyY, kGrumpyWidth, kGrumpyHeight,
                                  motionExpressionPixels(action, static_cast<uint8_t>(frame)));
        drawnExpressionPreviewFrame = frame;
    }
    if (first) {
        const auto style = M5.Display.getTextStyle();
        const auto* font = M5.Display.getFont();
        for (unsigned index = 0; index < 2; ++index) {
            const auto& button = lab::expressionPreviewButtons[index];
            const uint16_t color = lab::buttonColor(button.choice);
            M5.Display.fillRoundRect(button.x, button.y, button.w, button.h, 4, color);
            M5.Display.setFont(&fonts::Font0);
            M5.Display.setTextSize(1.0f);
            M5.Display.setTextDatum(textdatum_t::middle_center);
            M5.Display.setTextColor(0xFFFF, color);
            M5.Display.drawString(button.label, button.x + button.w / 2, button.y + button.h / 2);
        }
        M5.Display.setFont(font);
        M5.Display.setTextStyle(style);
    }
    drawStatusStrip(now, labMenu.statusStripVisible, quiet);
}

bool drawIdle(const lab::IdleFrame& idle) {
    const bool redrawBase = !drawnIdle || drawnPage != lab::Page::Face;
    if (redrawBase) {
        M5.Display.pushImage(0, 0, 320, 240, avatar_neutral);
        invalidateStatusStrip();
    }
    if (redrawBase || !drawnIdle || drawnIdleEyes != idle.eyes) {
        M5.Display.pushImage(kEyeX, kEyeY, kEyeWidth, kEyeHeight,
                             avatar_idle_eyes[static_cast<uint8_t>(idle.eyes)]);
    }
    if (redrawBase || !drawnIdle || drawnMotionIndex != idle.motionIndex) {
        M5.Display.pushImage(kAntennaX, kAntennaY,
                             kAntennaWidth, kAntennaHeight,
                             avatar_idle_antenna[idle.motionIndex]);
        M5.Display.pushImage(kLedX, kLedY, kLedWidth, kLedHeight,
                             avatar_idle_led[idle.motionIndex]);
    }
    drawnIdle = true;
    drawnMotionIndex = idle.motionIndex;
    drawnIdleEyes = idle.eyes;
    return redrawBase;
}

void drawMotionHeader(const lab::Snapshot& snapshot, bool force) {
    const auto& motion = snapshot.motion;
    constexpr size_t detailCapacity = sizeof(lab::kCheckedDefaultSafetyMode) + sizeof(" / explicit Enable") - 1;
    static char previousTitle[64]{}, previousDetail[detailCapacity]{};
    char title[64], detail[detailCapacity];
    if (motion.hasPosition)
        snprintf(title, sizeof(title), "%s | %s Y%d P%d", lab::findScreen(labMenu.page)->title,
                 lab::motionStateName(motion.state),
                 motion.position.yaw, motion.position.pitch);
    else snprintf(title, sizeof(title), "%s | %s position unknown",
        lab::findScreen(labMenu.page)->title, lab::motionStateName(motion.state));
    const char* error = localError ? localError : motion.error ? motion.error :
        snapshot.error ? snapshot.error : labDevice.runtime.wifiSoundError();
    if (error) snprintf(detail, sizeof(detail), "%s", error);
    else if (localStopPending) snprintf(detail, sizeof(detail), "Stop pending");
    else if (snapshot.state != lab::CommandState::None)
        snprintf(detail, sizeof(detail), "%s: %s", lab::actionName(snapshot.command.action), lab::stateName(snapshot.state));
    else if (motion.twoAxisTest)
        snprintf(detail, sizeof(detail), "%s / explicit Enable", motionSafetyMode);
    else if (motion.localYawBench)
        snprintf(detail, sizeof(detail), "%s / rail on ARM", motionSafetyMode);
    else if (motion.checkedDefault)
        snprintf(detail, sizeof(detail), "%s", motionSafetyMode);
    else snprintf(detail, sizeof(detail), "Home = calibrated center");
    if (!force && !strcmp(title, previousTitle) && !strcmp(detail, previousDetail)) return;
    const auto style = M5.Display.getTextStyle();
    const auto* font = M5.Display.getFont();
    M5.Display.fillRoundRect(0, 0, 320, 24, 0, 0x1083);
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextSize(1.0f);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(0xFFFF, 0x1083);
    M5.Display.drawString(title, 160, 6);
    M5.Display.drawString(detail, 160, 18);
    M5.Display.setFont(font);
    M5.Display.setTextStyle(style);
    snprintf(previousTitle, sizeof(previousTitle), "%s", title);
    snprintf(previousDetail, sizeof(previousDetail), "%s", detail);
}

bool isUnconfirmedArmTorqueFault(const lab::Snapshot& snapshot, const char* runtimeError) {
    const auto& diagnostic = snapshot.motion.diagnostic;
    return runtimeError && !strcmp(runtimeError, "motion_stop_unconfirmed") &&
        snapshot.command.action == lab::Action::Arm &&
        diagnostic.stage && !strcmp(diagnostic.stage, "torque_before_goal") &&
        diagnostic.cause && !strcmp(diagnostic.cause, "torque_mismatch");
}

bool sameText(const char* left, const char* right) {
    return (!left && !right) || (left && right && !strcmp(left, right));
}

bool sameBrightnessStatus(const lab::DisplayBrightnessStatus& left,
                          const lab::DisplayBrightnessStatus& right) {
    return left.brightness == right.brightness &&
        left.requestedBrightness == right.requestedBrightness &&
        left.pending == right.pending && sameText(left.error, right.error);
}

void drawEmailAvatar() {
    uint16_t row[40];
    for (int y = 0; y < 30; ++y) {
        for (int x = 0; x < 40; ++x) row[x] = avatar_neutral[(y * 8) * 320 + x * 8];
        M5.Display.pushImage(8, 4 + y, 40, 1, row);
    }
}

void drawNotification(const lab::NotificationSnapshot& notification, bool detail) {
    const auto style = M5.Display.getTextStyle();
    const auto* font = M5.Display.getFont();
    constexpr uint16_t background = 0x1083;
    const bool email = !strcmp(notification.templateId, "demo_important_email");
    const bool build = !strcmp(notification.templateId, "demo_github_action_failed");
    const uint16_t accent = build ? 0xFD20 : 0x07FF;
    const char* heading = email ? "Email | Simulation" :
        build ? "GitHub build | Simulation" : "NET Bot | Simulation";
    if (email) {
        drawEmailAvatar();
        M5.Display.fillRect(58, 8, 24, 16, accent);
        M5.Display.fillRect(60, 10, 20, 12, background);
        for (int step = 0; step < 10; ++step) {
            M5.Display.fillRect(60 + step, 10 + step / 2, 1, 2, accent);
            M5.Display.fillRect(79 - step, 10 + step / 2, 1, 2, accent);
        }
    } else if (build) {
        M5.Display.fillRect(10, 11, 28, 3, accent);
        for (int x : {10, 21, 32}) M5.Display.fillRect(x, 6, 6, 13, accent);
        M5.Display.fillRect(21, 4, 6, 17, background);
        for (int step = 0; step < 8; ++step) {
            M5.Display.fillRect(20 + step, 8 + step, 1, 2, accent);
            M5.Display.fillRect(27 - step, 8 + step, 1, 2, accent);
        }
    }
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(1.0f);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(accent, background);
    M5.Display.drawString(heading, email ? 194 : build ? 180 : 160, 14);
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextSize(2.0f);
    const int titleWidth = M5.Display.textWidth(notification.title);
    if (titleWidth > 304) M5.Display.setTextSize(2.0f * 304 / titleWidth);
    M5.Display.setTextColor(0xFFFF, background);
    M5.Display.drawString(notification.title, 160, email ? 42 : 34);
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextSize(1.0f);
    const char* text = detail ? notification.detail : "Open to review this demo item";
    for (int line = 0; *text && line < 2; ++line) {
        char buffer[241]{};
        size_t length = 0, lastSpace = 0;
        while (text[length] && length < sizeof(buffer) - 1) {
            buffer[length] = text[length];
            if (M5.Display.textWidth(buffer) > 304) {
                buffer[length] = '\0';
                break;
            }
            if (text[length] == ' ') lastSpace = length;
            ++length;
        }
        if (text[length] && lastSpace) length = lastSpace;
        buffer[length] = '\0';
        M5.Display.drawString(buffer, 160, (email ? 63 : 50) + line * 16);
        text += length;
        while (*text == ' ') ++text;
    }
    M5.Display.setFont(font);
    M5.Display.setTextStyle(style);
}

// Render the active page from snapshots shared with the HTTP and device runtime.
void drawLab(uint32_t now) {
    lab::NotificationSnapshot notification;
    {
        std::lock_guard<std::mutex> lock(labDevice.mutex);
        notification = labDevice.notification.snapshot(now);
    }
    if (notification.identity.revision != drawnNotification.identity.revision &&
        (labMenu.page == lab::Page::Face || lab::notificationPage(labMenu.page))) {
        drawnPage = lab::Page::Network;
        drawnIdle = false;
    }
    if (labMenu.page == lab::Page::SetupKey && !labPlatform.setupPassword()) {
        labMenu.show(lab::Page::Network, now);
        // A successful connection closes the setup network on purpose; that is not an error.
        if (labPlatform.network.state != lab::NetworkState::Connected) localError = "setup_key_unavailable";
    }
    Expression expression;
    lab::Indicator indicator;
    const char* runtimeError;
    lab::IdleFrame idle;
    lab::Snapshot snapshot;
    bool quiet;
    lab::NotificationCue pettingCue;
    lab::PettingAnimationState pettingAnimation;
    uint32_t stopGeneration;
    lab::DisplayBrightnessStatus brightness{};
    bool brightnessAvailable = false;
    {
        std::lock_guard<std::mutex> lock(labDevice.mutex);
        expression = labDevice.runtime.expression();
        indicator = labDevice.runtime.overlay();
        snapshot = labDevice.runtime.snapshot();
        runtimeError = snapshot.error;
        idle = labDevice.runtime.idle();
        quiet = labDevice.runtime.isQuiet();
        pettingCue = labDevice.runtime.pettingSound();
        pettingAnimation = labDevice.runtime.pettingAnimationState();
        stopGeneration = labDevice.runtime.stopGeneration();
    }
    if (labMenu.page == lab::Page::Settings || labMenu.page == lab::Page::DisplayStatus)
        brightnessAvailable = labPlatform.displayBrightnessStatus(brightness);
    bool avatarPreviewActive;
    bool avatarFrameDue = false;
    {
        std::lock_guard<std::mutex> lock(labPlatform.avatarComparisonMutex);
        avatarPreviewActive = labPlatform.avatarComparison.snapshot(now).active;
        if (avatarPreviewActive) avatarFrameDue = labPlatform.avatarComparison.frameDue(now);
    }
    if (labMenu.page != lab::Page::Face) {
        const auto state = bspMotionTest.state();
        if (state != lab::BspMotionTest::State::Preparing &&
            state != lab::BspMotionTest::State::Running)
            motionExpression = lab::BspMotionTest::Action::None;
        drawnMotionExpression = lab::BspMotionTest::Action::None;
    }
    if (expressionPreview != lab::MenuChoice::None &&
        (labMenu.page != lab::Page::ExpressionPreview ||
         now - expressionPreviewStartedAt >= 2000 ||
         stopGeneration != expressionPreviewStopGeneration)) {
        expressionPreview = lab::MenuChoice::None;
        if (labMenu.page == lab::Page::ExpressionPreview) drawnPage = lab::Page::Network;
    }
    if (labMenu.page == lab::Page::ExpressionPreview && expressionPreview != lab::MenuChoice::None) {
        drawExpressionPreview(now, quiet);
    } else if (labMenu.page == lab::Page::AvatarTest && avatarPreviewActive) {
        if (drawnPage != lab::Page::AvatarTest ||
            drawnAvatarComparisonRevision != labMenu.revision || avatarFrameDue) {
            if (drawnPage != lab::Page::AvatarTest) {
                drawnIdle = false;
                invalidateStatusStrip();
            }
            drawAvatarPreview(now);
            drawnAvatarComparisonRevision = labMenu.revision;
        }
    } else if (labMenu.page == lab::Page::Face) {
        bool faceReplaced = false;
        // The banner covers the strip area, so the strip shows only while no banner does.
        const bool banner = runtimeError || indicator != lab::Indicator::None;
        labMenu.faceProtected = banner || localStopPending || !strcmp(snapshot.motion.stopResult, "pending") ||
            shutdownPending || shutdownIssued || notification.active ||
            notification.restoreRequested || notification.receipt != lab::NotificationReceipt::None;
        const bool faceStopVisible = labMenu.faceStopVisible();
        const bool armTorqueFault = isUnconfirmedArmTorqueFault(snapshot, runtimeError);
        if (drawnStripVisible != labMenu.statusStripVisible || drawnFaceStopVisible != faceStopVisible) {
            drawnPage = lab::Page::Network;
            drawnIdle = false;
            invalidateStatusStrip();
        }
        const bool grumpyActive = pettingAnimation.active &&
            pettingAnimation.sequenceStage == 3 &&
            pettingAnimation.animation == lab::PettingAnimation::Grumpy;
        const auto bspState = bspMotionTest.state();
        const bool motionVisualBlocked = banner || localStopPending || shutdownPending ||
            shutdownIssued || notification.active || notification.restoreRequested ||
            notification.receipt != lab::NotificationReceipt::None || pettingAnimation.active ||
            snapshot.state == lab::CommandState::Accepted ||
            labDevice.bspYawStop.load(std::memory_order_acquire);
        if (motionVisualBlocked ||
            (bspState != lab::BspMotionTest::State::Preparing &&
             bspState != lab::BspMotionTest::State::Running))
            motionExpression = lab::BspMotionTest::Action::None;
        const bool motionVisualActive = motionExpression != lab::BspMotionTest::Action::None &&
            bspState == lab::BspMotionTest::State::Running && bspMotionTest.gestureSubmitted();
        if (!motionVisualActive && drawnMotionExpression != lab::BspMotionTest::Action::None) {
            drawnPage = lab::Page::Network;
            drawnMotionExpression = lab::BspMotionTest::Action::None;
        }
        if (motionVisualActive) {
            if (!motionExpressionStarted) {
                motionExpressionStartedAt = now;
                motionExpressionStarted = true;
            }
            const bool grumpyMotion = motionExpression == lab::BspMotionTest::Action::GrumpyShake;
            const uint32_t grumpyStep = (now - motionExpressionStartedAt) / avatar_grumpy_frame_ms;
            const uint8_t frame = grumpyMotion ?
                static_cast<uint8_t>(grumpyStep < sizeof(avatar_grumpy_offsets) ?
                    grumpyStep : sizeof(avatar_grumpy_offsets)) :
                motionExpressionFrame(now, motionExpressionStartedAt);
            if (drawnPage != lab::Page::Face || drawnMotionExpression != motionExpression ||
                drawnMotionExpressionFrame != frame) {
                if (drawnPage != lab::Page::Face || drawnPettingStage != 0 ||
                    drawnIndicator != indicator || drawnRuntimeError != runtimeError) {
                    M5.Display.pushImage(0, 0, 320, 240, avatar_neutral);
                    faceReplaced = true;
                    invalidateStatusStrip();
                }
                if (grumpyMotion) {
                    drawGrumpyFrame(grumpyShakeOffset(
                        {true, lab::PettingAnimation::Grumpy, motionExpressionStartedAt, 3}, now));
                } else {
                    M5.Display.pushImage(kGrumpyX, kGrumpyY, kGrumpyWidth, kGrumpyHeight,
                                         motionExpressionPixels(motionExpression, frame));
                }
                drawnMotionExpression = motionExpression;
                drawnMotionExpressionFrame = frame;
                drawnPettingAnimation = lab::PettingAnimation::Current;
                drawnPettingStage = 0;
                drawnIdle = false;
            }
        } else if (idle.active && !banner && !grumpyActive) {
            faceReplaced = drawIdle(idle);
            drawnPettingAnimation = lab::PettingAnimation::Current;
            drawnPettingStage = 0;
        } else if (pettingAnimation.active && pettingAnimation.sequenceStage != 0 && !banner) {
            const bool animated = pettingAnimation.animation != lab::PettingAnimation::Current &&
                pettingAnimation.sequenceStage != 3;
            const int grumpyOffset = grumpyShakeOffset(pettingAnimation, now);
            if (drawnIdle || drawnPage != lab::Page::Face ||
                drawnPettingAnimation != pettingAnimation.animation ||
                drawnPettingStage != pettingAnimation.sequenceStage ||
                drawnIndicator != indicator || drawnRuntimeError != runtimeError ||
                (pettingAnimation.sequenceStage == 3 && drawnGrumpyOffset != grumpyOffset) ||
                (animated &&
                 now - lastPettingFrameAt >= lab::AvatarComparison::kFrameIntervalMs)) {
                if (pettingAnimation.sequenceStage == 3) {
                    if (drawnPage != lab::Page::Face ||
                        drawnIndicator != indicator || drawnRuntimeError != runtimeError) {
                        M5.Display.pushImage(0, 0, 320, 240, avatar_neutral);
                        faceReplaced = true;
                        invalidateStatusStrip();
                    }
                    drawGrumpyFrame(grumpyOffset);
                } else if (pettingAnimation.animation == lab::PettingAnimation::Current) {
                    M5.Display.pushImage(0, 0, 320, 240, labPixels(expression));
                } else {
                    const auto sample = pettingAnimation.animation == lab::PettingAnimation::PetA ?
                        lab::AvatarComparisonSample::PettingC : lab::AvatarComparisonSample::PettingD;
                    drawAvatarComparisonImage({sample, 0.0f,
                        lab::avatarComparisonPetEnvelope(now - pettingAnimation.startedAt)});
                }
                if (pettingAnimation.sequenceStage != 3) {
                    drawPettingExpression(pettingAnimation.sequenceStage);
                    faceReplaced = true;
                    invalidateStatusStrip();
                }
                drawnPettingAnimation = pettingAnimation.animation;
                drawnPettingStage = pettingAnimation.sequenceStage;
                drawnGrumpyOffset = grumpyOffset;
                lastPettingFrameAt = now;
                drawnIdle = false;
            }
        } else if (drawnIdle || drawnPage != lab::Page::Face || labDrawn != expression ||
                   drawnIndicator != indicator || drawnRuntimeError != runtimeError ||
                   drawnArmTorqueFault != armTorqueFault ||
                   drawnPettingAnimation != lab::PettingAnimation::Current) {
            M5.Display.pushImage(0, 0, 320, 240, labPixels(expression));
            faceReplaced = true;
            invalidateStatusStrip();
            drawnPettingAnimation = lab::PettingAnimation::Current;
            drawnPettingStage = 0;
            if (banner) {
                const bool warning = !runtimeError && indicator == lab::Indicator::Warning;
                const uint16_t color = warning ? 0xFFE0 : kFaultBannerBackground;
                const int y = armTorqueFault ? 94 : 112;
                const int height = armTorqueFault ? 48 : 28;
                const auto style = M5.Display.getTextStyle();
                const auto* font = M5.Display.getFont();
                M5.Display.fillRoundRect(5, y, 310, height, 5, color);
                M5.Display.setTextColor(warning ? 0 : 0xFFFF, color);
                if (armTorqueFault) {
                    M5.Display.setFont(&fonts::Font0);
                    M5.Display.setTextSize(1.5f);
                    M5.Display.setTextDatum(textdatum_t::middle_center);
                    M5.Display.drawString("ARM BLOCKED: TORQUE REPORTED ON", 160, 106);
                    M5.Display.drawString("STOP STATUS NOT CONFIRMED", 160, 130);
                } else {
                    const char* label = runtimeError ? runtimeError :
                        warning ? "! WARNING !" : "* IMPORTANT *";
                    M5.Display.drawString(label, 160, 126);
                }
                M5.Display.setFont(font);
                M5.Display.setTextStyle(style);
            }
            drawnIdle = false;
        }
        labDrawn = expression;
        drawnIndicator = indicator;
        drawnRuntimeError = runtimeError;
        drawnArmTorqueFault = armTorqueFault;
        drawStatusStrip(now, labMenu.statusStripVisible && !banner &&
            notification.receipt == lab::NotificationReceipt::None, quiet);
        drawnStripVisible = labMenu.statusStripVisible;
        drawnFaceStopVisible = faceStopVisible;
        if (!banner && notification.receipt != lab::NotificationReceipt::None) {
            const char* label = notification.receipt == lab::NotificationReceipt::Pending ? "Companion pending" :
                notification.receipt == lab::NotificationReceipt::Completed ? "Opened in companion" :
                notification.receipt == lab::NotificationReceipt::Unknown ? "Companion result unknown" :
                "Companion unavailable";
            M5.Display.fillRoundRect(5, 112, 310, 28, 5, 0x1083);
            M5.Display.setTextColor(0xFFFF, 0x1083);
            M5.Display.drawString(label, 160, 126);
        }
        if (faceReplaced && faceStopVisible) {
            const auto& stop = lab::faceButtons[0];
            const auto style = M5.Display.getTextStyle();
            const auto* font = M5.Display.getFont();
            M5.Display.fillRoundRect(stop.x, stop.y, stop.w, stop.h, 6, lab::buttonColor(stop.choice));
            M5.Display.setFont(&fonts::Font0);
            M5.Display.setTextSize(2);
            M5.Display.setTextDatum(textdatum_t::middle_center);
            M5.Display.setTextColor(0xFFFF, lab::buttonColor(stop.choice));
            M5.Display.drawString(stop.label, stop.x + stop.w / 2, stop.y + stop.h / 2);
            M5.Display.setFont(font);
            M5.Display.setTextStyle(style);
        }
    } else {
        const bool settingsChanged = (labMenu.page == lab::Page::Settings ||
            labMenu.page == lab::Page::DisplayStatus) &&
            (!drawnSettingsStatus || brightnessAvailable != drawnSettingsBrightnessAvailable ||
             (brightnessAvailable && !sameBrightnessStatus(brightness, drawnSettingsBrightness)) ||
             !sameText(localError, drawnSettingsError));
        const bool sensorsChanged = (labMenu.page == lab::Page::Sensors ||
            labMenu.page == lab::Page::SensorReadings || labMenu.page == lab::Page::Rgb ||
            labMenu.page == lab::Page::Petting) &&
            drawnSensorsRevision != demoSensors.revision;
        const bool nfcChanged = (labMenu.page == lab::Page::Nfc || labMenu.page == lab::Page::NfcResult) &&
            drawnNfcRevision != demoNfc.revision;
        const bool bspYawChanged = labMenu.page == lab::Page::BspYaw &&
            drawnBspState != bspMotionTest.state();
        if (drawnPage == labMenu.page && !settingsChanged && !sensorsChanged && !nfcChanged && !bspYawChanged &&
            (labMenu.page != lab::Page::NetworkStatus || now - lastNetworkDraw < 1000)) {
            if (lab::motionPage(labMenu.page)) drawMotionHeader(snapshot, false);
            drawStatusStrip(now, labMenu.statusStripVisible, quiet);
            return;
        }
        lastNetworkDraw = now;
        const uint16_t background = labMenu.page == lab::Page::SetupKey ? 0x0841 : 0x1083;
        M5.Display.fillScreen(background);
        invalidateStatusStrip();
        M5.Display.setTextSize(1.0f);
        M5.Display.setTextColor(0xFFFF, background);
        char demoTitle[40] = "Demo: tap screen";
        if (demoTouchRevision == labMenu.revision)
            snprintf(demoTitle, sizeof(demoTitle), "Screen tap: %d,%d", demoTouchX, demoTouchY);
        const char* title = labMenu.page == lab::Page::DemoTouch ? (localError ? localError : demoTitle) :
            localError ? localError : lab::findScreen(labMenu.page)->title;
        if (lab::motionPage(labMenu.page)) drawMotionHeader(snapshot, true);
        else if (labMenu.page != lab::Page::SetupKey && !lab::notificationPage(labMenu.page))
            M5.Display.drawString(title, 160, 14);
        else if (labMenu.page == lab::Page::NotificationActions)
            M5.Display.drawString(title, 160, 12);
        if (labMenu.page == lab::Page::Notification || labMenu.page == lab::Page::NotificationDetail) {
            drawNotification(notification, labMenu.page == lab::Page::NotificationDetail);
        }
        if (labMenu.page == lab::Page::NetworkStatus) {
            char ip[20], url[48];
            labPlatform.address(ip, sizeof(ip));
            snprintf(url, sizeof(url), "http://%s%s", ip,
                     labPlatform.network.ap ? "/setup" : "/");
            M5.Display.drawString(lab::networkName(labPlatform.network.state), 110, 50);
            M5.Display.drawString(ip, 110, 78);
            M5.Display.drawString(labPlatform.network.ap ? "Setup: NETBOT-SETUP" : "Setup closed", 110, 106);
            const char* error = localError ? localError :
                labPlatform.serverError ? labPlatform.serverError : labPlatform.network.error;
            if (error) M5.Display.drawString(error, 110, 130);
            // Version 2 fits every IPv4 URL with 3 px modules and a 4-module quiet zone.
            if (strcmp(ip, "0.0.0.0")) M5.Display.qrcode(url, 216, 34, 99, 2);
        }
        if (labMenu.page == lab::Page::DisplayStatus) {
            M5.Display.drawString(labMenu.statusStripVisible ? "Status bar: shown" : "Status bar: hidden", 160, 32);
            char range[48];
            snprintf(range, sizeof(range), "Range %u-%u, default %u", lab::kDisplayBrightnessMinimum,
                lab::kDisplayBrightnessMaximum, lab::kDisplayBrightnessDefault);
            M5.Display.drawString(range, 160, 48);
            M5.Display.drawString("RAM only; resets after reboot", 160, 66);
            if (brightnessAvailable) {
                char cached[32], state[48];
                snprintf(cached, sizeof(cached), "Cached driver setting: %u", brightness.brightness);
                if (brightness.pending)
                    snprintf(state, sizeof(state), "Pending: %u", brightness.requestedBrightness);
                else {
                    const char* error = localError && strcmp(localError, "brightness_busy") ?
                        localError : brightness.error;
                    snprintf(state, sizeof(state), "%s", error ? error : "No pending change");
                }
                M5.Display.drawString(cached, 160, 84);
                M5.Display.drawString(state, 160, 100);
            } else {
                M5.Display.drawString("Brightness status unavailable", 160, 84);
            }
            M5.Display.drawString("Hardware write unverified", 160, 118);
        }
        if (labMenu.page == lab::Page::BspYaw) {
            drawnBspState = bspMotionTest.state();
            M5.Display.drawString(motionTestStatus(), 160, 30);
            if (drawnBspState == lab::BspMotionTest::State::Failed) {
                char detail[64];
                snprintf(detail, sizeof(detail), "stage=%s code=%d",
                    bspMotionTest.failure(), bspMotionTest.failureCode());
                M5.Display.drawString(detail, 160, 46);
            } else if (lab::kLocalHomeSetup) {
                M5.Display.drawString(localError ? localError : homeSetupStatus(), 160, 46);
            }
        }
        if (labMenu.page == lab::Page::SetupKey) {
            const char* password = labPlatform.setupPassword();
            char joinQr[64];
            snprintf(joinQr, sizeof(joinQr), "WIFI:T:WPA;S:%s;P:%s;;", lab::kSetupSsid, password);
            M5.Display.fillRoundRect(8, 2, 304, 24, 6, menu_colors::navigation);
            M5.Display.setTextColor(0xFFFF, menu_colors::navigation);
            M5.Display.setTextSize(1.2f);
            M5.Display.drawString("JOIN TEMPORARY WI-FI", 160, 14);

            M5.Display.fillRoundRect(10, 32, 166, 112, 10, 0x2104);
            M5.Display.setTextColor(0xAD55, 0x2104);
            M5.Display.setTextSize(1.0f);
            M5.Display.drawString("NETWORK", 93, 45);
            M5.Display.setTextColor(0xFFFF, 0x2104);
            M5.Display.setTextSize(1.2f);
            M5.Display.drawString(lab::kSetupSsid, 93, 65);
            M5.Display.setTextColor(0xAD55, 0x2104);
            M5.Display.setTextSize(1.0f);
            M5.Display.drawString("SETUP KEY", 93, 88);
            char groupedKey[10];
            snprintf(groupedKey, sizeof(groupedKey), "%.4s %.4s", password, password + 4);
            M5.Display.setTextColor(0xFFFF, 0x2104);
            M5.Display.setTextSize(2.0f);
            M5.Display.drawString(groupedKey, 93, 118);

            M5.Display.fillRoundRect(187, 32, 123, 112, 10, 0xFFFF);
            M5.Display.qrcode(joinQr, 193, 33, 111, 3, true);
        }
        size_t count;
        const lab::Button* buttons = labMenu.buttons(count);
        for (size_t i = 0; i < count; ++i) {
            const auto& button = buttons[i];
            const bool bench = lab::motionPage(labMenu.page) && snapshot.motion.localYawBench;
            const bool blocked = bench && button.choice == lab::MenuChoice::Action &&
                button.action != lab::Action::MoveLeft && button.action != lab::Action::MoveRight;
            const bool brightnessControl = button.choice == lab::MenuChoice::BrightnessDown ||
                button.choice == lab::MenuChoice::BrightnessUp;
            const bool brightnessBusy = !brightnessAvailable || brightness.pending;
            const bool brightnessAtLimit = brightnessAvailable && !brightness.pending &&
                ((button.choice == lab::MenuChoice::BrightnessDown &&
                  brightness.brightness <= lab::kDisplayBrightnessMinimum) ||
                 (button.choice == lab::MenuChoice::BrightnessUp &&
                  brightness.brightness >= lab::kDisplayBrightnessMaximum));
            const bool brightnessDisabled = brightnessControl && (brightnessBusy || brightnessAtLimit);
            const uint16_t color = blocked || brightnessDisabled ? 0x4208 : lab::buttonColor(button.choice);
            M5.Display.fillRoundRect(button.x, button.y, button.w, button.h, 6, color);
            M5.Display.setTextColor(0xFFFF, color);
            M5.Display.setTextSize(1.0f);
            const char* label = blocked ? "Locked" : brightnessDisabled ?
                brightnessBusy ? "Busy" : button.choice == lab::MenuChoice::BrightnessDown ?
                    "Minimum" : "Maximum" :
                bench && button.choice == lab::MenuChoice::Arm ? "Rail + ARM" : button.label;
            M5.Display.drawString(label, button.x + button.w / 2, button.y + button.h / 2);
        }
        if (labMenu.page == lab::Page::Shutdown && (localError || shutdownPending)) {
            M5.Display.setTextColor(0xFFFF, background);
            M5.Display.drawString(localError ? localError : "Waiting for measured Stop", 160, 122);
        }
        if (labMenu.page == lab::Page::NfcResult) {
            const auto* font = M5.Display.getFont();
            M5.Display.setFont(&fonts::Font0);
            M5.Display.drawString(demoNfc.state, 160, 34);
            if (!strcmp(demoNfc.state, "nfc_cleanup_unknown"))
                M5.Display.drawString(demoNfc.cleanupReason, 160, 50);
            const char* cursor = demoNfc.text;
            // Preserve complete UTF-8 characters while wrapping the retained text.
            for (int line = 0; line < 6 && *cursor; ++line) {
                char row[256]{};
                size_t used = 0;
                const char* end = cursor;
                while (*end) {
                    const uint8_t first = static_cast<uint8_t>(*end);
                    const size_t width = first < 0x80 ? 1 : first < 0xE0 ? 2 : first < 0xF0 ? 3 : 4;
                    memcpy(row + used, end, width);
                    row[used + width] = '\0';
                    if (M5.Display.textWidth(row) > 300) { row[used] = '\0'; break; }
                    used += width;
                    end += width;
                }
                if (!used) break;
                M5.Display.drawString(row, 160, 50 + line * 14);
                cursor = end;
            }
            M5.Display.setFont(font);
            drawnNfcRevision = demoNfc.revision;
        }
        if (labMenu.page == lab::Page::Nfc) drawnNfcRevision = demoNfc.revision;
        if (labMenu.page == lab::Page::SensorReadings) {
            char text[64];
            if (demoSensors.topError) snprintf(text, sizeof(text), "Top: %s", demoSensors.topError);
            else snprintf(text, sizeof(text), "Top F/M/B: %u/%u/%u",
                demoSensors.top[0], demoSensors.top[1], demoSensors.top[2]);
            M5.Display.drawString(text, 160, 42);
            if (demoSensors.lightError) {
                snprintf(text, sizeof(text), "Light/PS: %s", demoSensors.lightError);
                M5.Display.drawString(text, 160, 70);
            } else {
                snprintf(text, sizeof(text), "Proximity raw: %u", demoSensors.proximity);
                M5.Display.drawString(text, 160, 66);
                snprintf(text, sizeof(text), "ALS raw CH0/CH1: %u/%u", demoSensors.light0, demoSensors.light1);
                M5.Display.drawString(text, 160, 90);
            }
            if (demoSensors.rgbError) snprintf(text, sizeof(text), "RGB: %s", demoSensors.rgbError);
            else if (demoSensors.rgbOn) snprintf(text, sizeof(text), "RGB: requested (%lu seconds)",
                static_cast<unsigned long>(demoSensors.rgbDurationMs / 1000));
            else snprintf(text, sizeof(text), "RGB: off requested");
            M5.Display.drawString(text, 160, 120);
            drawnSensorsRevision = demoSensors.revision;
        }
        if (labMenu.page == lab::Page::Sensors) drawnSensorsRevision = demoSensors.revision;
        if (labMenu.page == lab::Page::Rgb) drawnSensorsRevision = demoSensors.revision;
        if (labMenu.page == lab::Page::Petting) {
            M5.Display.setFont(&fonts::Font0);
            M5.Display.drawString(pettingEnabled ? "Petting: on" : "Petting: off", 160, 100);
            M5.Display.drawString(pettingSoundEnabled ? "Sound: on (Quiet respected)" : "Sound: off", 160, 115);
            const char* status = demoSensors.topError ? demoSensors.topError :
                lab::notificationCueName(pettingCue);
            M5.Display.drawString(status, 160, 130);
            M5.Display.setFont(&fonts::Font2);
            drawnSensorsRevision = demoSensors.revision;
        }
        if (labMenu.page == lab::Page::Settings || labMenu.page == lab::Page::DisplayStatus) {
            drawnSettingsBrightness = brightness;
            drawnSettingsBrightnessAvailable = brightnessAvailable;
            drawnSettingsError = localError;
            drawnSettingsStatus = true;
        } else {
            drawnSettingsStatus = false;
        }
        if (labMenu.page == lab::Page::DemoTouch) {
            M5.Display.fillRoundRect(8, 24, 304, 120, 6, 0x2104);
            M5.Display.setTextColor(0xFFFF, 0x2104);
            M5.Display.drawString("Tap here", 160, 84);
        }
        drawStatusStrip(now, labMenu.statusStripVisible, quiet);
    }
    drawnPage = labMenu.page;
    {
        std::lock_guard<std::mutex> lock(labDevice.mutex);
        drawnNotification = notification;
        if (labDevice.notification.matches(notification.identity)) {
            if (labMenu.page == lab::Page::Notification &&
                notification.state == lab::NotificationState::Received) {
                const uint32_t presentedAt = millis();
                labDevice.notification.presented(notification.identity, presentedAt,
                    labDevice.runtime.notificationSound(presentedAt));
            }
            if (labMenu.page == lab::Page::Face) labDevice.notification.restored(now);
            drawnNotification = labDevice.notification.snapshot(now);
        }
    }
}

bool cameraPowerOffReady() {
    const char* error = labPlatform.camera ? labPlatform.camera->preparePowerOff() : nullptr;
    if (localError != error) drawnPage = lab::Page::Face;
    localError = error;
    return localError == nullptr;
}

void powerOffOnce() {
    if (shutdownIssued) return;
    if (!cameraPowerOffReady()) {
        shutdownPending = true;
        return;
    }
    shutdownPending = false;
    shutdownIssued = true;
    M5.Display.fillScreen(0);
    M5.Display.setTextColor(0xFFFF, 0);
    M5.Display.drawString("Shutting down...", 160, 120);
    M5.Power.powerOff();
}

void pollLocalStop() {
    if (!localStopPending) return;
    if (localManualStopPending) {
        const auto snapshot = labDevice.stopStatus();
        if (!snapshot.stopReceipt.outcome || snapshot.stopReceipt.bootId != localStopBoot ||
            snapshot.stopReceipt.stopId != localManualStopId) {
            localStopPending = localManualStopPending = false;
            localError = "stop_outcome_unknown";
        } else if (strcmp(snapshot.stopReceipt.outcome, "pending")) {
            localStopPending = localManualStopPending = false;
            if (strcmp(snapshot.stopReceipt.outcome, "confirmed")) localError = "motion_stop_unconfirmed";
        }
        return;
    }
    lab::Snapshot snapshot{};
    {
        std::lock_guard<std::mutex> lock(labDevice.mutex);
        snapshot = labDevice.runtime.snapshot();
        if (snapshot.bootId != localStopBoot ||
            labDevice.runtime.stopGeneration() != localStopGeneration) {
            localStopPending = false;
            localError = "stop_outcome_unknown";
            drawnPage = lab::Page::Network;
            return;
        }
    }
    if (!strcmp(snapshot.motion.stopResult, "pending")) return;
    localStopPending = false;
    const bool confirmed = !strcmp(snapshot.motion.stopResult, "confirmed");
    if (!confirmed) localError = "motion_stop_unconfirmed";
    labPlatform.recordOperationalEvent(millis(), lab::OperationalEventKind::Stop,
        confirmed ? lab::OperationalStatus::Completed : lab::OperationalStatus::Failed,
        lab::OperationalAction::Stop, confirmed ? lab::OperationalError::None : lab::OperationalError::Failed,
        0, 0, 0, {localStopOrigin});
    drawnPage = lab::Page::Network;
}

void pollShutdown() {
    if (!shutdownPending) return;
    const auto snapshot = labDevice.snapshot();
    if (!strcmp(snapshot.motion.stopResult, "pending")) return;
    if (!strcmp(snapshot.motion.stopResult, "confirmed")) {
        if (!cameraPowerOffReady()) return;
        shutdownPending = false;
        labPlatform.recordOperationalEvent(millis(), lab::OperationalEventKind::Stop,
            lab::OperationalStatus::Completed, lab::OperationalAction::PowerOff, lab::OperationalError::None);
        powerOffOnce();
    }
    else {
        shutdownPending = false;
        labPlatform.recordOperationalEvent(millis(), lab::OperationalEventKind::Stop,
            lab::OperationalStatus::Failed, lab::OperationalAction::PowerOff,
            lab::OperationalError::Failed);
        localError = "motion_stop_unconfirmed";
        drawnPage = lab::Page::Face;
    }
}

lab::OperationalAction operationalUiAction(const lab::Button& button) {
    if (button.choice == lab::MenuChoice::Action) return lab::operationalActionFor(button.action);
    switch (button.choice) {
        case lab::MenuChoice::Back: return lab::OperationalAction::Back;
        case lab::MenuChoice::Demo: return lab::OperationalAction::Demo;
        case lab::MenuChoice::Motion: return lab::OperationalAction::Motion;
        case lab::MenuChoice::Arm: return lab::OperationalAction::Arm;
        case lab::MenuChoice::Disarm: return lab::OperationalAction::Disarm;
        case lab::MenuChoice::Network: return lab::OperationalAction::Network;
        case lab::MenuChoice::Setup: return lab::OperationalAction::Setup;
        case lab::MenuChoice::ShowSetupKey: return lab::OperationalAction::ShowSetupKey;
        case lab::MenuChoice::CancelSetup: return lab::OperationalAction::CancelSetup;
        case lab::MenuChoice::Quiet: return lab::OperationalAction::Quiet;
        case lab::MenuChoice::Settings: return lab::OperationalAction::Settings;
        case lab::MenuChoice::BrightnessDown: return lab::OperationalAction::BrightnessDown;
        case lab::MenuChoice::BrightnessUp: return lab::OperationalAction::BrightnessUp;
        case lab::MenuChoice::Shutdown: return lab::OperationalAction::Shutdown;
        case lab::MenuChoice::PowerOff: return lab::OperationalAction::PowerOff;
        case lab::MenuChoice::Stop: return lab::OperationalAction::Stop;
        case lab::MenuChoice::NotificationOpen: return lab::OperationalAction::Open;
        case lab::MenuChoice::NotificationDiscard: return lab::OperationalAction::Discard;
        case lab::MenuChoice::NotificationAcknowledge: return lab::OperationalAction::Acknowledge;
        case lab::MenuChoice::NotificationAction: return lab::OperationalAction::OpenInCompanion;
        case lab::MenuChoice::StatusStrip: return lab::OperationalAction::StatusStrip;
        default: return lab::OperationalAction::None;
    }
}

void recordOperationalUiChoice(const lab::Button& button, uint32_t now, const char* error, bool pending,
                              lab::InteractionOrigin origin) {
    if (button.choice == lab::MenuChoice::None) return;
    const bool stop = button.choice == lab::MenuChoice::Stop || button.choice == lab::MenuChoice::PowerOff;
    const bool brightness = button.choice == lab::MenuChoice::BrightnessDown ||
        button.choice == lab::MenuChoice::BrightnessUp;
    const bool action = button.choice == lab::MenuChoice::Action || button.choice == lab::MenuChoice::Arm ||
        button.choice == lab::MenuChoice::Disarm || button.choice == lab::MenuChoice::Quiet;
    const auto event = stop ? lab::OperationalEventKind::Stop :
        brightness ? lab::OperationalEventKind::Brightness :
        action ? lab::OperationalEventKind::Action : lab::OperationalEventKind::Ui;
    const auto status = error ? lab::OperationalStatus::Failed :
        pending ? lab::OperationalStatus::Pending :
        stop ? lab::OperationalStatus::Completed :
        action ? lab::OperationalStatus::Requested :
        brightness ? lab::OperationalStatus::Requested : lab::OperationalStatus::Changed;
    labPlatform.recordOperationalEvent(now, event, status, operationalUiAction(button),
        lab::operationalErrorFor(error), 0, 0, 0, {origin});
}

void syncNotificationPage(uint32_t now) {
    lab::NotificationSnapshot notification;
    bool protectedStatus;
    {
        std::lock_guard<std::mutex> lock(labDevice.mutex);
        labDevice.notification.advance(now, labPlatform.companionPresent(now));
        notification = labDevice.notification.snapshot(now);
        protectedStatus = labDevice.runtime.snapshot().error ||
            labDevice.runtime.overlay() == lab::Indicator::Warning;
        if (notification.restoreRequested) labDevice.runtime.cancelNotificationSound();
    }
    if (shutdownPending || shutdownIssued) return;
    if (protectedStatus) {
        if (lab::notificationPage(labMenu.page)) {
            std::lock_guard<std::mutex> lock(labDevice.mutex);
            labDevice.runtime.cancelNotificationSound();
            labMenu.show(lab::Page::Face, now);
        }
        return;
    }
    if (notification.restoreRequested) {
        if (labMenu.page != lab::Page::Face && !lab::motionPage(labMenu.page) &&
            labMenu.page != lab::Page::Shutdown && labMenu.page != lab::Page::SetupKey)
            labMenu.show(lab::Page::Face, now);
    } else if (notification.active && !lab::motionPage(labMenu.page) &&
               labMenu.page != lab::Page::Shutdown && labMenu.page != lab::Page::SetupKey) {
        const auto page = notification.state == lab::NotificationState::Opened ?
            lab::Page::NotificationDetail : lab::Page::Notification;
        if (labMenu.page != page && !(page == lab::Page::NotificationDetail &&
            labMenu.page == lab::Page::NotificationActions)) labMenu.show(page, now);
    }
}

void publishFaceControls(uint32_t now) {
    std::lock_guard<std::mutex> lock(labDevice.mutex);
    const auto notification = labDevice.notification.snapshot(now);
    labMenu.faceProtected = localStopPending || labDevice.stopRequested || shutdownPending || shutdownIssued || notification.active ||
        notification.restoreRequested || notification.receipt != lab::NotificationReceipt::None ||
        labDevice.runtime.snapshot().error || labDevice.runtime.overlay() != lab::Indicator::None;
    labDevice.uiFaceStopVisible = labMenu.faceStopVisible();
    labDevice.uiPettingEnabled = pettingEnabled;
    labDevice.uiPettingSoundEnabled = pettingSoundEnabled;
}

// Caller holds the device mutex, independently of Face control visibility.
bool pettingFaceEligible(uint32_t now) {
    const auto notification = labDevice.notification.snapshot(now);
    const auto value = labDevice.runtime.snapshot();
    return labMenu.page == lab::Page::Face &&
        motionExpression == lab::BspMotionTest::Action::None &&
        !localStopPending && !labDevice.stopRequested &&
        !shutdownPending && !shutdownIssued && !notification.active && !notification.restoreRequested &&
        notification.receipt == lab::NotificationReceipt::None &&
        !value.error && value.motion.state != lab::MotionState::Fault &&
        labDevice.runtime.overlay() == lab::Indicator::None;
}

void resetPettingDetection() {
    topReleased = false;
    topReleaseSamples = topTouchSamples = 0;
    if (!topSettleComplete) topSettleStarted = false;
}

// Only fresh, eligible top-sensor samples can start a petting reaction.
void updatePetting(uint32_t now, bool eligible, bool stopped) {
    std::lock_guard<std::mutex> lock(labDevice.mutex);
    if (!pettingEnabled || !eligible || !pettingFaceEligible(now) || stopped || demoSensors.topError) {
        resetPettingDetection();
        return;
    }
    if (!demoSensors.topSampleFresh()) return;
    // Let the sensor settle at startup before accepting a release and new touch.
    if (!topSettleComplete) {
        if (!topSettleStarted) { topSettleStarted = true; topSettleAt = now; }
        if (now - topSettleAt < 500) return;
        topSettleComplete = true;
    }
    // Require two released samples followed by two touched samples to debounce input.
    const bool touched = demoSensors.top[0] || demoSensors.top[1] || demoSensors.top[2];
    if (!touched) {
        topTouchSamples = 0;
        if (!topReleased && ++topReleaseSamples >= 2) topReleased = true;
        return;
    }
    topReleaseSamples = 0;
    if (!topReleased) return;
    if (++topTouchSamples < 2) return;
    topReleased = false;
    topTouchSamples = 0;
    // The runtime selects the reaction; the sensor helper pulses the corresponding effect.
    if (labDevice.runtime.petSequence(now, pettingSoundEnabled, esp_random()).execute)
        demoSensors.pulsePetting(now, labDevice.runtime.isQuiet(),
            labDevice.runtime.pettingAnimationState().animation == lab::PettingAnimation::Grumpy);
}

bool labChoice(const lab::Button& button, uint32_t now,
               const lab::NotificationIdentity* remoteIdentity = nullptr,
               lab::InteractionOrigin origin = lab::InteractionOrigin::Unknown) {
    if (shutdownIssued) return false;
    const bool bspYawChoice = button.choice == lab::MenuChoice::BspYawPositive ||
        button.choice == lab::MenuChoice::BspYawNegative ||
        button.choice == lab::MenuChoice::BspPitchUp || button.choice == lab::MenuChoice::BspPitchDown;
    const bool setHome = button.choice == lab::MenuChoice::BspSetHome;
    const auto axis = button.choice == lab::MenuChoice::BspPitchUp ||
        button.choice == lab::MenuChoice::BspPitchDown ? lab::BspMotionTest::Axis::Pitch : lab::BspMotionTest::Axis::Yaw;
    const int direction = button.choice == lab::MenuChoice::BspYawPositive ||
        button.choice == lab::MenuChoice::BspPitchUp ? 1 : -1;
    if (bspYawChoice)
        motionTestLog(now, "TOUCH origin=%s axis=%s direction=%+d state=%d",
            origin == lab::InteractionOrigin::Physical ? "physical" : "other",
            axis == lab::BspMotionTest::Axis::Yaw ? "yaw" : "pitch", direction,
            static_cast<int>(bspMotionTest.state()));
    if (button.choice == lab::MenuChoice::Stop) {
        if (expressionPreview != lab::MenuChoice::None) {
            expressionPreview = lab::MenuChoice::None;
            drawnPage = lab::Page::Network;
        }
        motionTestLog(now, "STOP_REQUESTED state=%d", static_cast<int>(bspMotionTest.state()));
#if defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC)
        const auto boot = labDevice.diagnosticStopBoot();
        const auto admission = labDevice.requestDiagnosticStop(boot);
        const lab::StopReceipt identity{boot, 1, "pending"};
#else
        const auto boot = labDevice.snapshot().bootId;
        lab::StopReceipt identity{};
        const auto admission = labDevice.requestStop(boot, &identity);
#endif
        if (admission.status == 202) {
            motionTestLog(now, "STOP_LATCHED experimental=%u", labDevice.bspYawStop.load());
            if (labMenu.page == lab::Page::BspYaw) drawnPage = lab::Page::Face;
            localStopPending = localManualStopPending = true;
            localManualStopId = identity.stopId;
            localStopBoot = identity.bootId;
            localError = nullptr;
        } else localError = "motion_stop_unconfirmed";
        recordOperationalUiChoice(button, now, localError, localStopPending, origin);
        return true;
    }
    if (labDevice.manualActiveSafe()) {
        if (button.choice == lab::MenuChoice::PowerOff || button.choice == lab::MenuChoice::Setup ||
            button.choice == lab::MenuChoice::Arm || button.choice == lab::MenuChoice::Disarm ||
            button.choice == lab::MenuChoice::Action || bspYawChoice || setHome) {
            localError = "manual_session_active";
            if (bspYawChoice) motionTestLog(now, "REJECTED reason=manual_session_active");
            return true;
        }
    }
    if (button.choice == lab::MenuChoice::PowerOff && labPlatform.camera)
        labPlatform.camera->preparePowerOff();
    if (button.choice == lab::MenuChoice::PowerOff && demoNfc.isBusy()) {
        demoNfc.tick(now, false, true);
        { std::lock_guard<std::mutex> lock(labDevice.mutex); labDevice.runtime.stop(now); }
        localError = "nfc_stopping";
        return false;
    }
    if (shutdownPending && button.choice != lab::MenuChoice::Back &&
        button.choice != lab::MenuChoice::Stop) return false;
    if (button.choice == lab::MenuChoice::PhysicalNodYes ||
        button.choice == lab::MenuChoice::PhysicalShakeNo ||
        button.choice == lab::MenuChoice::PhysicalDance) {
        if (remoteIdentity || origin != lab::InteractionOrigin::Physical ||
            labMenu.page != lab::Page::PhysicalExpressions)
            return false;
        using Action = lab::BspMotionTest::Action;
        const auto action = button.choice == lab::MenuChoice::PhysicalNodYes ? Action::NodYes :
            button.choice == lab::MenuChoice::PhysicalShakeNo ? Action::ShakeNo : Action::Dance;
        localError = nullptr;
        if (demoNfc.isBusy() || !demoNfc.busKnown() ||
            (labPlatform.camera && labPlatform.camera->isBusy())) {
            localError = "peripheral_busy";
        } else {
#if !defined(NETBOT_TWO_AXIS_TEST) && !defined(NETBOT_YAW_BENCH) && !defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC)
            {
                std::lock_guard<std::mutex> lock(labDevice.mutex);
                if (labDevice.stopRequested || localStopPending)
                    localError = "stopping";
                else if (labDevice.manualActive())
                    localError = "manual_session_active";
                else if (labDevice.pendingMotionTest != Action::None || !bspMotionTest.reserve(action))
                    localError = "test_busy_failed_or_stopped";
            }
            if (!localError) {
                localError = startBspMotionTest(action, true);
                if (!localError) {
                    labMenu.show(lab::Page::Face, now);
                    selectMotionExpression(action);
                }
            }
#else
            localError = "bsp_test_requires_normal_wifi_build";
#endif
        }
        if (localError)
            motionTestLog(now, "REJECTED source=local_test experiment=%s reason=%s",
                lab::BspMotionTest::actionName(action), localError);
        drawnPage = lab::Page::Face;
        return true;
    }
    if (bspYawChoice || setHome) {
        if (remoteIdentity || origin != lab::InteractionOrigin::Physical)
            localError = "bsp_test_local_touch_required";
        else if (demoNfc.isBusy() || !demoNfc.busKnown() ||
                 (labPlatform.camera && labPlatform.camera->isBusy()))
            localError = "peripheral_busy";
        else localError = startBspMotionTest(setHome ? lab::BspMotionTest::Action::HomeCapture :
            lab::Device::localMotionTestAction(axis, direction));
        if (localError)
            motionTestLog(now, "REJECTED reason=%s state=%d", localError, static_cast<int>(bspMotionTest.state()));
        drawnPage = lab::Page::Face;
        return true;
    }
    if (button.choice == lab::MenuChoice::Back) {
        shutdownPending = false;
        if (labPlatform.camera) labPlatform.camera->cancelPowerOff();
    }
    localError = nullptr;
    if (lab::notificationPage(labMenu.page) &&
        (button.choice == lab::MenuChoice::Navigate || (remoteIdentity && button.choice == lab::MenuChoice::Quiet))) {
        std::lock_guard<std::mutex> lock(labDevice.mutex);
        const auto identity = remoteIdentity ? *remoteIdentity : drawnNotification.identity;
        if (!labDevice.notification.matches(identity) ||
            (button.choice == lab::MenuChoice::Navigate &&
             labDevice.notification.snapshot(now).state != lab::NotificationState::Opened)) {
            localError = "stale_notification";
            return false;
        }
    }
    if (button.choice == lab::MenuChoice::NotificationOpen ||
        button.choice == lab::MenuChoice::NotificationDiscard ||
        button.choice == lab::MenuChoice::NotificationAction ||
        button.choice == lab::MenuChoice::NotificationAcknowledge) {
        const auto choice = button.choice == lab::MenuChoice::NotificationOpen ? lab::NotificationChoice::Open :
            button.choice == lab::MenuChoice::NotificationDiscard ? lab::NotificationChoice::Discard :
            button.choice == lab::MenuChoice::NotificationAcknowledge ? lab::NotificationChoice::Acknowledge :
            lab::NotificationChoice::OpenInCompanion;
        bool accepted;
        {
            std::lock_guard<std::mutex> lock(labDevice.mutex);
            const auto result = labDevice.notification.choose(
                remoteIdentity ? *remoteIdentity : drawnNotification.identity, choice, now,
                labPlatform.companionPresent(now), origin);
            if (result.status >= 400) localError = result.code;
            accepted = result.status < 400;
        }
        syncNotificationPage(now);
        return accepted;
    }
    bool shutdownReady = true;
    switch (button.choice) {
        case lab::MenuChoice::Back:
            if (labMenu.page == lab::Page::ExpressionPreview &&
                expressionPreview != lab::MenuChoice::None) {
                expressionPreview = lab::MenuChoice::None;
                drawnPage = lab::Page::Network;
                break;
            }
            if (labMenu.page == lab::Page::AvatarTest) {
                std::lock_guard<std::mutex> lock(labPlatform.avatarComparisonMutex);
                labPlatform.avatarComparison.cancel();
            }
            labMenu.show(lab::parentPage(labMenu.page), now);
            break;
        case lab::MenuChoice::Navigate: labMenu.show(button.target, now); break;
        case lab::MenuChoice::AvatarTest:
            labMenu.show(lab::Page::AvatarTest, now);
            break;
        case lab::MenuChoice::ExpressionPreview:
            if (origin != lab::InteractionOrigin::Physical || labMenu.page != lab::Page::AvatarTest)
                return false;
            {
                std::lock_guard<std::mutex> lock(labPlatform.avatarComparisonMutex);
                labPlatform.avatarComparison.cancel();
            }
            labMenu.show(lab::Page::ExpressionPreview, now);
            break;
        case lab::MenuChoice::PreviewGrumpy:
        case lab::MenuChoice::PreviewNodYes:
        case lab::MenuChoice::PreviewShakeNo:
        case lab::MenuChoice::PreviewDance:
            if (origin != lab::InteractionOrigin::Physical ||
                labMenu.page != lab::Page::ExpressionPreview ||
                expressionPreview != lab::MenuChoice::None) return false;
            expressionPreview = button.choice;
            expressionPreviewStartedAt = now;
            {
                std::lock_guard<std::mutex> lock(labDevice.mutex);
                expressionPreviewStopGeneration = labDevice.runtime.stopGeneration();
            }
            drawnPage = lab::Page::Network;
            break;
        case lab::MenuChoice::AvatarSampleA:
        case lab::MenuChoice::AvatarSampleB:
        case lab::MenuChoice::AvatarSampleC:
        case lab::MenuChoice::AvatarSampleD: {
            const auto sample = button.choice == lab::MenuChoice::AvatarSampleA ?
                lab::AvatarComparisonSample::BreathingA :
                button.choice == lab::MenuChoice::AvatarSampleB ?
                    lab::AvatarComparisonSample::BreathingB :
                button.choice == lab::MenuChoice::AvatarSampleC ?
                    lab::AvatarComparisonSample::PettingC : lab::AvatarComparisonSample::PettingD;
            bool started;
            {
                std::lock_guard<std::mutex> lock(labPlatform.avatarComparisonMutex);
                started = labPlatform.avatarComparison.start(sample, now);
            }
            if (!started) {
                localError = "avatar_sample_invalid";
                return false;
            }
            drawnAvatarComparisonRevision = UINT32_MAX;
            break;
        }
        case lab::MenuChoice::Nfc:
            labMenu.show(lab::Page::Nfc, now);
            break;
        case lab::MenuChoice::NfcRead:
            if (!demoNfc.read(now)) localError = demoNfc.state;
            break;
        case lab::MenuChoice::Sensors:
            labMenu.show(lab::Page::Sensors, now);
            break;
        case lab::MenuChoice::Rgb: {
            bool quiet;
            { std::lock_guard<std::mutex> lock(labDevice.mutex); quiet = labDevice.runtime.isQuiet(); }
            if (!demoSensors.pulse(now, quiet, lab::rgbDuration(button))) localError = demoSensors.rgbError;
            break;
        }
        case lab::MenuChoice::Petting: {
            pettingEnabled = !pettingEnabled;
            resetPettingDetection();
            if (!pettingEnabled) {
                std::lock_guard<std::mutex> lock(labDevice.mutex);
                labDevice.runtime.cancelPetting(now);
                labDevice.runtime.resetPettingSequence();
            }
            break;
        }
        case lab::MenuChoice::PettingSound: {
            pettingSoundEnabled = !pettingSoundEnabled;
            if (!pettingSoundEnabled) {
                std::lock_guard<std::mutex> lock(labDevice.mutex);
                labDevice.runtime.cancelPetting(now, true);
            }
            break;
        }
        case lab::MenuChoice::Demo: labMenu.show(lab::Page::Demo, now); break;
        case lab::MenuChoice::Motion: labMenu.show(lab::Page::Motion, now); break;
        case lab::MenuChoice::Network: labMenu.show(lab::Page::Network, now); break;
        case lab::MenuChoice::Settings: labMenu.show(lab::Page::Settings, now); break;
        case lab::MenuChoice::Shutdown: labMenu.show(lab::Page::Shutdown, now); break;
        case lab::MenuChoice::StatusStrip:
            labMenu.statusStripVisible = !labMenu.statusStripVisible;
            break;
        case lab::MenuChoice::ShowSetupKey:
            if (labPlatform.setupPassword()) labMenu.show(lab::Page::SetupKey, now);
            else localError = "setup_key_unavailable";
            break;
        case lab::MenuChoice::Setup: {
            std::lock_guard<std::mutex> lock(labDevice.mutex);
            if (!labDevice.runtime.stop(now)) {
                localError = "motion_stop_unconfirmed";
                drawnPage = lab::Page::Face;
                return true;
            }
            break;
        }
        default: break;
    }
    if (button.choice == lab::MenuChoice::BrightnessDown || button.choice == lab::MenuChoice::BrightnessUp) {
        lab::DisplayBrightnessStatus brightness;
        if (!labPlatform.displayBrightnessStatus(brightness)) localError = "brightness_unavailable";
        else if (brightness.pending) localError = "brightness_busy";
        else if (button.choice == lab::MenuChoice::BrightnessDown &&
                 brightness.brightness <= lab::kDisplayBrightnessMinimum)
            localError = "brightness_at_minimum";
        else if (button.choice == lab::MenuChoice::BrightnessUp &&
                 brightness.brightness >= lab::kDisplayBrightnessMaximum)
            localError = "brightness_at_maximum";
        else {
            const int adjustment = button.choice == lab::MenuChoice::BrightnessDown ?
                -lab::kDisplayBrightnessStep : lab::kDisplayBrightnessStep;
            const auto requested = static_cast<uint8_t>(brightness.brightness + adjustment);
            switch (labPlatform.requestDisplayBrightness(requested, origin)) {
                case lab::DisplayBrightnessQueueResult::Accepted: break;
                case lab::DisplayBrightnessQueueResult::Unavailable: localError = "brightness_unavailable"; break;
                case lab::DisplayBrightnessQueueResult::Busy: localError = "brightness_busy"; break;
                case lab::DisplayBrightnessQueueResult::OutOfRange: localError = "brightness_out_of_range"; break;
            }
        }
    }
    if (button.choice == lab::MenuChoice::Setup) labPlatform.setup(now);
    if (button.choice == lab::MenuChoice::CancelSetup) labPlatform.cancelSetup();
    {
        std::lock_guard<std::mutex> lock(labDevice.mutex);
        if (button.choice == lab::MenuChoice::Arm || button.choice == lab::MenuChoice::Disarm) {
            const bool ok = button.choice == lab::MenuChoice::Arm ?
                labDevice.runtime.armMotion(now) : labDevice.runtime.disarmMotion(now);
            if (!ok) {
                const auto snapshot = labDevice.runtime.snapshot();
                localError = button.choice == lab::MenuChoice::Arm && snapshot.configRevision ?
                    "config_companion_enable_required" :
                    snapshot.motion.error ? snapshot.motion.error : "motion_not_ready";
            }
        }
        if (button.choice == lab::MenuChoice::Action) {
            const auto result = labDevice.stopRequested ? lab::Admission{409, "stopping", false} :
                labDevice.runtime.local(button.action, lab::requiresMotion(button.action) ? 5000 : 1000);
            if (result.status >= 400) localError = result.code;
            else if (!lab::motionPage(labMenu.page)) labMenu.show(lab::Page::Face, now);
        }
        if (button.choice == lab::MenuChoice::PowerOff) {
            const bool confirmed = labDevice.runtime.stop(now);
            const bool pending = !confirmed && !strcmp(labDevice.runtime.snapshot().motion.stopResult, "pending");
            if (!confirmed) {
                if (!pending) localError = "motion_stop_unconfirmed";
                shutdownReady = false;
                if (button.choice == lab::MenuChoice::PowerOff) {
                    shutdownPending = pending;
                    labMenu.show(lab::Page::Shutdown, now);
                } else labMenu.show(lab::Page::Motion, now);
            }
        }
        if (button.choice == lab::MenuChoice::Quiet)
            labDevice.runtime.setQuiet(!labDevice.runtime.isQuiet());
    }
    if (button.choice == lab::MenuChoice::PowerOff && shutdownReady && !cameraPowerOffReady()) {
        shutdownReady = false;
        shutdownPending = true;
        labMenu.show(lab::Page::Shutdown, now);
    }
    recordOperationalUiChoice(button, now, localError,
        shutdownPending || (button.choice == lab::MenuChoice::Stop && localStopPending), origin);
    if (button.choice == lab::MenuChoice::PowerOff && shutdownReady) powerOffOnce();
    drawnPage = lab::Page::Shutdown;  // invalidate after any button, including Quiet
    if (labMenu.page == lab::Page::Shutdown) drawnPage = lab::Page::Face;
    return true;
}

void setup() {
    // Initialize the device, shared runtime, display and local HTTP platform.
    // Ordinary startup leaves BSP motion untouched; only the explicit experiment initializes it.
    auto config = M5.config();
    config.internal_mic = false;
    M5.begin(config);
#if !defined(NETBOT_TEST_BUILD)
    audioTestHardware.initialize();
    speaker.test = &audioTest;
    labDevice.audioTest = &audioTest;
    labDevice.runtime.setPettingAudio(&audioTest);
    labDevice.rgbController = &demoSensors;
#if !defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC) && !defined(NETBOT_TWO_AXIS_TEST) && !defined(NETBOT_YAW_BENCH)
    labDevice.imageDisplay = &imageDisplay;
    labDevice.temporaryText = &temporaryText;
    audioTestHardware.otherResourceBusy = [] { return imageDisplay.resourceBusy(); };
#endif
#endif
    M5.Speaker.setVolume(40);
    labDevice.runtime.resetBoot(lab::newBootId(), millis());
    localError = labDevice.runtime.startupSound(millis());
    const uint32_t startupStopEpoch = labDevice.runtime.stopEpoch();
    (void)startupStopEpoch;
    labMenu.statusStripVisible = true;
    labMenu.faceProtected = false;
    pettingEnabled = pettingSoundEnabled = true;
    topSettleStarted = topSettleComplete = false;
    resetPettingDetection();
    drawnPettingAnimation = lab::PettingAnimation::Current;
    drawnPettingStage = 0;
    lastPettingFrameAt = 0;
    statusStripGesture = lab::StatusStripGesture{};
    labPlatform.initializeDisplayBrightness();
#if defined(NETBOT_REMOTE_YAW_BENCH) || defined(NETBOT_TWO_AXIS_TEST) || defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC)
    Serial.setTxBufferSize(1024);
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0); // Backpressure must never wait for the USB host.
#endif
    remoteLog.reset(labDevice.runtime.snapshot().bootId);
    labPlatform.remoteLog = &remoteLog;
#if !defined(NETBOT_TEST_BUILD)
    audioTest.log = &remoteLog;
#endif
#if defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC) && !defined(NETBOT_TEST_BUILD)
    labDevice.initializeDiagnosticStop(labDevice.runtime.snapshot().bootId, &defaultMotion.bus,
        [](void* context) {
            static_cast<lab::StackChanScsBus*>(context)->requestDiagnosticRailCut();
        },
        [](void* context) -> uint32_t {
            auto& bus = *static_cast<lab::StackChanScsBus*>(context);
            const auto state = bus.diagnosticRailStatus();
            return static_cast<uint32_t>(state) | (bus.diagnosticRailEvidence() << 8);
        });
#endif
#if defined(NETBOT_REMOTE_YAW_BENCH) || defined(NETBOT_TWO_AXIS_TEST) || defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC)
    lab::RuntimeLogEvent bootEvent;
    if (labDevice.runtime.popLog(bootEvent)) usbLog.event(bootEvent, labDevice.runtime.lostLogs());
#if defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC) && !defined(NETBOT_TEST_BUILD)
    static_assert(ARDUINO_USB_MODE == 1 && ARDUINO_USB_CDC_ON_BOOT == 1,
        "Manual diagnostics require CoreS3 USB hardware CDC, not a servo UART.");
    manualSession.setDiagnostic([](const char* event, const char* detail) {
        uint32_t boot, command;
        {
            std::lock_guard<std::mutex> lock(labDevice.mutex);
            boot = labDevice.runtime.snapshot().bootId;
            command = labDevice.manualLastCommandId();
        }
        usbLog.diagnostic(millis(), boot, command, event, detail);
    });
#endif
#endif
    M5.Display.setRotation(1);
    M5.Display.setSwapBytes(true);
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    updateBattery(millis());
    drawLab(millis());
    labPlatform.remoteUi = true;
#if defined(NETBOT_WIFI_LAB) && !defined(NETBOT_LESSON_EXAMPLES) && \
    !defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC) && !defined(NETBOT_TWO_AXIS_TEST) && !defined(NETBOT_YAW_BENCH)
    labDevice.motionTest = &bspMotionTest;
#endif
#if !defined(NETBOT_TEST_BUILD)
    lab::cameraNfcBusKnown = [] { return demoNfc.busKnown(); };
#endif
    labPlatform.begin(millis());
    if (lab::kLocalHomeSetup) {
        homeAvailable = readHomeReferences().valid();
        std::lock_guard<std::mutex> lock(labDevice.mutex);
        if (homeAvailable) labDevice.runtime.homeReady();
    }
#if !defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC)
    // An early HTTP Stop must not be undone by the boot configuration reset.
    {
        std::lock_guard<std::mutex> lock(labDevice.mutex);
        if (!labDevice.bspYawStop.load() && labDevice.runtime.stopEpoch() == startupStopEpoch) {
            motionController.inspectCalibration(millis());
#if !defined(NETBOT_TWO_AXIS_TEST) && !defined(NETBOT_YAW_BENCH)
            defaultMotion.begin(millis());
#else
            motionController.startup(millis());
#endif
        }
    }
#endif
}

void loop() {
    // Advance hardware and shared state, process touch, and render changes each iteration.
    if (shutdownIssued) { resetPettingDetection(); delay(1); return; }
    M5.update();
    const uint32_t now = millis();
    updateBattery(now);
    labDevice.tick(now);
#if !defined(NETBOT_TEST_BUILD)
    {
        std::lock_guard<std::mutex> lock(labDevice.mutex);
        if (labDevice.stopRequested) audioTest.cancel(now);
        audioTest.tick(now);
    }
#endif
    pollLocalStop();
    syncNotificationPage(now);
    pollShutdown();
    if (shutdownIssued) { resetPettingDetection(); return; }
#if defined(NETBOT_WIFI_LAB) && !defined(NETBOT_LESSON_EXAMPLES) && \
    !defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC) && !defined(NETBOT_TWO_AXIS_TEST) && !defined(NETBOT_YAW_BENCH)
    using MotionTestAction = lab::Device::MotionTestAction;
    const bool peripheralsReady = !shutdownPending && !demoNfc.isBusy() && demoNfc.busKnown() &&
        (!labPlatform.camera || !labPlatform.camera->isBusy());
    MotionTestAction testAction;
    {
        std::lock_guard<std::mutex> lock(labDevice.mutex);
        labDevice.motionTestReady = peripheralsReady && !labDevice.manualActive();
        testAction = labDevice.pendingMotionTest;
        labDevice.pendingMotionTest = MotionTestAction::None;
    }
    if (testAction != MotionTestAction::None) {
        const char* error = nullptr;
        const bool hasGestureFace = testAction == MotionTestAction::NodYes ||
            testAction == MotionTestAction::ShakeNo || testAction == MotionTestAction::Dance ||
            testAction == MotionTestAction::GrumpyShake;
        bool gestureVisualReady = true;
        if (hasGestureFace) {
            std::lock_guard<std::mutex> lock(labDevice.mutex);
            gestureVisualReady = pettingFaceEligible(now) &&
                !labDevice.runtime.pettingAnimationState().active &&
                labDevice.runtime.snapshot().state != lab::CommandState::Accepted;
        }
        if (labDevice.bspYawStop.load()) {
            bspMotionTest.cancelPending();
            error = "test_stopped";
        } else if (!peripheralsReady || labDevice.manualActiveSafe() || !labPlatform.motionTestNetworkReady()) {
            bspMotionTest.fail("dispatch_unavailable", 0, true);
            error = "dispatch_unavailable";
        } else if (!gestureVisualReady) {
            error = testAction == MotionTestAction::GrumpyShake ?
                "grumpy_face_unavailable" : "gesture_face_unavailable";
            bspMotionTest.rejectPending(error);
        } else {
            error = startBspMotionTest(testAction, true, testAction == MotionTestAction::GrumpyShake);
            if (!error) selectMotionExpression(testAction);
        }
        if (error) motionTestLog(now, "REJECTED source=http_test experiment=%s reason=%s",
            lab::BspMotionTest::actionName(testAction), error);
    }
#endif
    publishFaceControls(now);
    lab::UiRequest remote;
    {
        std::lock_guard<std::mutex> lock(labDevice.mutex);
        labDevice.uiPage = labMenu.page;
        remote = labDevice.ui;
        labDevice.ui = {};
        labDevice.uiInProgress = true;
    }
    lab::NotificationSnapshot notification;
    {
        std::lock_guard<std::mutex> lock(labDevice.mutex);
        notification = labDevice.notification.snapshot(now);
    }
    if (!shutdownPending && !notification.active && !notification.restoreRequested &&
        !lab::remoteScreenBlocked(remote.page) &&
        remote.kind == lab::UiRequest::Kind::Screen) {
        labMenu.show(remote.page, now);
    } else if (!shutdownPending && remote.kind == lab::UiRequest::Kind::Touch && remote.page == labMenu.page) {
        size_t count;
        const lab::Button* items = lab::pageButtons(labMenu.page, count);
        if (remote.element < count && lab::visibleButton(labMenu.page, labMenu.faceStopVisible())) {
            labMenu.touch(now);
            if (labChoice(items[remote.element], now, &remote.notification, lab::InteractionOrigin::Remote))
                labPlatform.recordOperationalEvent(now, lab::OperationalEventKind::Touch,
                    lab::OperationalStatus::Observed, operationalUiAction(items[remote.element]),
                    lab::OperationalError::None, 0, 0, 0, {lab::InteractionOrigin::Remote});
        }
    }
    bool ordinaryFace;
    publishFaceControls(now);
    {
        std::lock_guard<std::mutex> lock(labDevice.mutex);
        notification = labDevice.notification.snapshot(now);
        ordinaryFace = labMenu.page == lab::Page::Face && !shutdownPending && !shutdownIssued &&
            !notification.active && !notification.restoreRequested &&
            notification.receipt == lab::NotificationReceipt::None &&
            !labDevice.runtime.snapshot().error && labDevice.runtime.overlay() == lab::Indicator::None;
    }
    const auto touch = M5.Touch.getDetail();
    bool toggleStrip;
    const bool consumed = statusStripGesture.update(now, ordinaryFace, labMenu.revision,
        notification.identity.revision, M5.Touch.getCount(), touch.id, touch.x, touch.y,
        touch.wasPressed(), touch.isPressed(), touch.wasReleased(), toggleStrip);
    if (toggleStrip) {
        labMenu.statusStripVisible = !labMenu.statusStripVisible;
        labPlatform.recordOperationalEvent(now, lab::OperationalEventKind::Touch,
            lab::OperationalStatus::Observed, lab::OperationalAction::StatusStrip,
            lab::OperationalError::None, 0, 0, 0, {lab::InteractionOrigin::Physical});
    }
    if (!consumed && touch.wasClicked()) {
        const auto previousPage = labMenu.page;
        const lab::Button* button = labMenu.tap(now, touch.x, touch.y);
        const auto action = button ? operationalUiAction(*button) :
            previousPage == lab::Page::Face && labMenu.page == lab::Page::Main ?
                lab::OperationalAction::Menu : lab::OperationalAction::None;
        if (button) labChoice(*button, now, nullptr, lab::InteractionOrigin::Physical);
        if (previousPage == lab::Page::DemoTouch && labMenu.page == previousPage &&
            touch.x >= 8 && touch.x < 312 && touch.y >= 24 && touch.y < 144) {
            demoTouchX = touch.x;
            demoTouchY = touch.y;
            demoTouchRevision = labMenu.revision;
            drawnPage = lab::Page::Face;
        }
        labPlatform.recordOperationalEvent(now, lab::OperationalEventKind::Touch,
            lab::OperationalStatus::Observed, action, lab::OperationalError::None,
            0, 0, 0, {lab::InteractionOrigin::Physical});
    }
    {
        std::lock_guard<std::mutex> lock(labPlatform.avatarComparisonMutex);
        if (labPlatform.avatarComparison.expired(now)) {
            labPlatform.avatarComparison.cancel();
            drawnPage = lab::Page::Network;
        } else if (labPlatform.avatarComparison.active() && labMenu.page != lab::Page::AvatarTest) {
            labPlatform.avatarComparison.cancel();
        }
    }
    {
        std::lock_guard<std::mutex> lock(labDevice.mutex);
        labDevice.uiInProgress = false;
        const auto result = pendingHomeCaptureResult.exchange(lab::HomeCaptureResult::None);
        if (result != lab::HomeCaptureResult::None && !labDevice.bspYawStop.load()) {
            homeCaptureResult = result;
            if (result == lab::HomeCaptureResult::Verified) {
                homeAvailable = true;
                labDevice.runtime.homeCaptured();
            } else if (result == lab::HomeCaptureResult::Failed) {
                homeAvailable = false;
            }
            if (labMenu.page == lab::Page::BspYaw) drawnPage = lab::Page::Face;
        }
    }
    // Keep the test controls and STOP visible until the operator leaves the page.
    if (!lab::motionPage(labMenu.page) && !shutdownPending) labMenu.tick(now);
    if (shutdownIssued) { resetPettingDetection(); return; }
    bool quiet, pettingFace;
    uint32_t stopEpoch;
    {
        std::lock_guard<std::mutex> lock(labDevice.mutex);
        quiet = labDevice.runtime.isQuiet();
        stopEpoch = labDevice.runtime.stopEpoch();
        pettingFace = pettingFaceEligible(now);
    }
    bool topBusAvailable = !demoNfc.isBusy() && demoNfc.busKnown() &&
        (!labPlatform.camera || !labPlatform.camera->isBusy());
#if !defined(NETBOT_TEST_BUILD)
    topBusAvailable = topBusAvailable && !lab::cameraSccb.uncertain.load();
#endif
    const bool stopped = stopEpoch != observedStopEpoch;
#if !defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC) && !defined(NETBOT_TWO_AXIS_TEST) && !defined(NETBOT_YAW_BENCH)
    const bool sampleSensorsOnFace = labMenu.page == lab::Page::Face;
#else
    const bool sampleSensorsOnFace = false;
#endif
    demoSensors.tick(now, labMenu.page == lab::Page::Sensors || labMenu.page == lab::Page::SensorReadings ||
        labMenu.page == lab::Page::Rgb, quiet, stopped, pettingEnabled && pettingFace, topBusAvailable,
        sampleSensorsOnFace, sampleSensorsOnFace);
    if (labDevice.rgbController) {
        std::lock_guard<std::mutex> lock(labDevice.mutex);
        labDevice.rgbEffect.tick(now, *labDevice.rgbController, stopped, quiet, topBusAvailable);
    }
    updatePetting(now, pettingFace && topBusAvailable, stopped);
    static uint32_t imuPollAt = 0;
    static bool imuPolled = false;
    const bool imuAvailable = M5.Imu.isEnabled();
    int imuMask = 0;
    m5::IMU_Class::imu_data_t imuData{};
    if (imuAvailable && topBusAvailable && (!imuPolled || now - imuPollAt >= 250)) {
        imuPolled = true;
        imuPollAt = now;
        imuMask = M5.Imu.update();
        if (imuMask) M5.Imu.getImuData(&imuData);
    }
    {
        std::lock_guard<std::mutex> lock(labDevice.mutex);
        auto& s = labDevice.sensors;
        s.published = true;
        s.topInitialized = demoSensors.topIsInitialized();
        s.topAvailable = demoSensors.topIsAvailable();
        s.topValid = demoSensors.topHasSample();
        s.front = demoSensors.top[0]; s.middle = demoSensors.top[1]; s.back = demoSensors.top[2];
        s.topAt = demoSensors.topSampleAt(); s.topError = demoSensors.topError;
        s.lightInitialized = demoSensors.lightIsInitialized();
        s.lightAvailable = demoSensors.lightIsAvailable();
        s.lightValid = demoSensors.lightHasSample();
        s.proximity = demoSensors.proximity; s.light0 = demoSensors.light0; s.light1 = demoSensors.light1;
        s.lightAt = demoSensors.lightSampleAt(); s.lightError = demoSensors.lightError;
        s.imuAvailable = imuAvailable;
        s.imuError = !imuAvailable ? "imu_unavailable" : !topBusAvailable ? "bus_unavailable" : nullptr;
        const auto recordVector = [&](lab::ImuVector& target, const m5::IMU_Class::imu_3d_t& value, int flag) {
            if (!(imuMask & flag)) return;
            if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z)) {
                s.imuError = "imu_invalid_sample";
                return;
            }
            target = {true, value.x, value.y, value.z, now};
        };
        recordVector(s.acceleration, imuData.accel, m5::IMU_Class::sensor_mask_accel);
        recordVector(s.gyroscope, imuData.gyro, m5::IMU_Class::sensor_mask_gyro);
        s.rgbActive = demoSensors.rgbOn; s.rgbDurationMs = demoSensors.rgbDurationMs;
        s.rgbError = demoSensors.rgbError;
        s.pettingEnabled = pettingEnabled; s.soundEnabled = pettingSoundEnabled;
        s.quiet = labDevice.runtime.isQuiet();
        const bool faceEligible = pettingFaceEligible(now);
        s.ineligibleReason = !pettingEnabled ? "disabled" :
            !faceEligible ? "face_unavailable" :
            !topBusAvailable ? "top_bus_unavailable" :
            stopped ? "stopped" : demoSensors.topError ? demoSensors.topError : nullptr;
        s.eligible = s.ineligibleReason == nullptr;
        s.settled = topSettleComplete; s.released = topReleased;
        s.touchCount = topTouchSamples; s.releaseCount = topReleaseSamples;
        const auto reaction = labDevice.runtime.pettingAnimationState();
        s.pettingActive = reaction.active; s.stage = reaction.active ? reaction.sequenceStage : 0;
        s.soundState = lab::notificationCueName(labDevice.runtime.pettingSound());
    }
    demoNfc.tick(now, labMenu.page == lab::Page::Nfc || labMenu.page == lab::Page::NfcResult,
        stopEpoch != observedStopEpoch);
    observedStopEpoch = stopEpoch;
#if !defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC) && !defined(NETBOT_TWO_AXIS_TEST) && !defined(NETBOT_YAW_BENCH)
    bool textOwnsDisplay = false;
    bool restoreText = false;
    if (labDevice.temporaryText) {
        auto& text = *labDevice.temporaryText;
        bool textChanged = false;
        {
            std::lock_guard<std::mutex> lock(labDevice.mutex);
            const auto currentNotification = labDevice.notification.snapshot(now);
            const auto currentStatus = labDevice.runtime.snapshot();
            labDevice.imageUiReady = labMenu.page == lab::Page::Face && !shutdownPending && !shutdownIssued &&
                !localStopPending && !labDevice.stopRequested && !currentNotification.active &&
                !currentNotification.restoreRequested &&
                currentNotification.receipt == lab::NotificationReceipt::None &&
                !currentStatus.error && currentStatus.state != lab::CommandState::Accepted &&
                !labDevice.runtime.pettingAnimationState().active &&
                labDevice.runtime.overlay() == lab::Indicator::None &&
                (!labDevice.imageDisplay || !labDevice.imageDisplay->ownsDisplay());
            textChanged = text.tick(now, labDevice.imageUiReady, labDevice.runtime.stopEpoch());
            textOwnsDisplay = text.ownsDisplay();
            restoreText = text.takeRestore();
        }
        if (textChanged && textOwnsDisplay) {
            M5.Display.fillScreen(0);
            M5.Display.setTextDatum(textdatum_t::middle_center);
            M5.Display.setTextColor(0xFFFF, 0);
            M5.Display.setFont(&fonts::Font2);
            char rows[8][lab::kTemporaryTextLimit + 1]{};
            int rowCount = 0;
            float size = 1.7f;
            for (;;) {
                M5.Display.setTextSize(size);
                const char* source = text.text();
                rowCount = 0;
                while (*source && rowCount < 8) {
                    char* line = rows[rowCount];
                    size_t used = 0;
                    while (*source) {
                        const unsigned char first = static_cast<unsigned char>(*source);
                        size_t count = first < 0x80 ? 1 : first < 0xE0 ? 2 : first < 0xF0 ? 3 : 4;
                        const size_t remaining = strlen(source);
                        if (count > remaining) count = remaining;
                        memcpy(line + used, source, count);
                        line[used + count] = '\0';
                        if (M5.Display.textWidth(line) > 300 && used) {
                            line[used] = '\0';
                            break;
                        }
                        used += count;
                        source += count;
                    }
                    ++rowCount;
                }
                if (!*source || size <= 0.9f) break;
                size -= 0.2f;
            }
            const int spacing = static_cast<int>(16 * size + 2);
            for (int row = 0; row < rowCount; ++row)
                M5.Display.drawString(rows[row], 160, 120 - (rowCount - 1) * spacing / 2 + row * spacing);
            M5.Display.setFont(&fonts::Font2);
            M5.Display.setTextSize(1.0f);
        }
    }
    if (restoreText) {
        drawnPage = lab::Page::Network;
        drawnIdle = false;
        invalidateStatusStrip();
    }
    if (!textOwnsDisplay && labDevice.imageDisplay) {
        auto& image = *labDevice.imageDisplay;
        bool renderImage = false, imageOwnsDisplay = false, restoreImage = false;
        {
            std::lock_guard<std::mutex> lock(labDevice.mutex);
            const auto currentNotification = labDevice.notification.snapshot(now);
            const auto currentStatus = labDevice.runtime.snapshot();
            labDevice.imageUiReady = labMenu.page == lab::Page::Face && !shutdownPending && !shutdownIssued &&
                !localStopPending && !labDevice.stopRequested && !currentNotification.active &&
                !currentNotification.restoreRequested && currentNotification.receipt == lab::NotificationReceipt::None &&
                !currentStatus.error && currentStatus.state != lab::CommandState::Accepted &&
                !labDevice.runtime.pettingAnimationState().active && labDevice.runtime.overlay() == lab::Indicator::None;
            renderImage = image.tick(now, labDevice.imageUiReady, labDevice.runtime.stopEpoch());
            imageOwnsDisplay = image.ownsDisplay();
            restoreImage = image.takeRestore();
        }
        if (renderImage) {
            const bool rendered = image.present();
            std::lock_guard<std::mutex> lock(labDevice.mutex);
            const auto currentNotification = labDevice.notification.snapshot(millis());
            const auto currentStatus = labDevice.runtime.snapshot();
            const bool eligible = labDevice.imageUiReady && !labDevice.stopRequested &&
                !currentNotification.active && !currentNotification.restoreRequested &&
                currentNotification.receipt == lab::NotificationReceipt::None && !currentStatus.error &&
                currentStatus.state != lab::CommandState::Accepted &&
                !labDevice.runtime.pettingAnimationState().active && labDevice.runtime.overlay() == lab::Indicator::None;
            image.presented(millis(), rendered, eligible, labDevice.runtime.stopEpoch());
            imageOwnsDisplay = image.ownsDisplay();
            restoreImage = image.takeRestore() || restoreImage;
        }
        if (restoreImage) {
            drawnPage = lab::Page::Network;
            drawnIdle = false;
            invalidateStatusStrip();
        }
        if (!imageOwnsDisplay) drawLab(now);
    } else if (!textOwnsDisplay) {
        drawLab(now);
    }
#else
    drawLab(now);
#endif
    publishFaceControls(now);
    {
        std::lock_guard<std::mutex> lock(labDevice.mutex);
        labDevice.uiPage = labMenu.page;
        labDevice.uiNotification = drawnNotification.identity;
    }
    labPlatform.tick(now);
#if defined(NETBOT_REMOTE_YAW_BENCH) || defined(NETBOT_TWO_AXIS_TEST) || defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC)
    lab::Snapshot status;
    lab::MotionDebugSnapshot debug;
    lab::RuntimeLogEvent event;
    bool hasEvent;
    uint32_t queueLoss;
#if defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC)
    lab::ManualActionReceipt manualReceipt;
    lab::ManualSessionState manualState;
    const char* manualError;
#endif
    {
        std::lock_guard<std::mutex> lock(labDevice.mutex);
        status = labDevice.runtime.snapshot();
        labDevice.runtime.debugSnapshot(debug);
        hasEvent = labDevice.runtime.popLog(event);
        queueLoss = labDevice.runtime.lostLogs();
#if defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC)
        manualReceipt = labDevice.manualActionReceipt();
        manualState = labDevice.manualSnapshot();
        manualError = labDevice.manualError();
#endif
    }
    if (hasEvent) usbLog.event(event, queueLoss);
    usbLog.observe(now, status, debug, lab::networkCode(labPlatform.network.state), labPlatform.companionPresent(now));
#if defined(NETBOT_MANUAL_AXIS_DIAGNOSTIC)
    static uint32_t previousCommand = 0;
    static auto previousState = lab::ManualSessionState::Idle;
    static const char* previousOutcome = nullptr;
    if (previousCommand != manualReceipt.commandId || previousState != manualState ||
        previousOutcome != manualReceipt.outcome) {
        char detail[256];
        snprintf(detail, sizeof(detail), "action=%s_state=%s_outcome=%s_error=%s",
            manualReceipt.action ? manualReceipt.action : "none", lab::manualSessionStateName(manualState),
            manualReceipt.outcome ? manualReceipt.outcome : "none", manualError ? manualError : "none");
        usbLog.diagnostic(now, status.bootId, manualReceipt.commandId, "manual_transition", detail);
        previousCommand = manualReceipt.commandId;
        previousState = manualState;
        previousOutcome = manualReceipt.outcome;
    }
#endif
#endif
    delay(1);
}
#endif

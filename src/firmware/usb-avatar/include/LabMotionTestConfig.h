// Goal: Define reference values for a specific checked motion test.
// Responsibilities: Group test limits, calibration, and home references.
// How it works:
// 1. The test selects the configured motion envelope.
// 2. Calibration and home values are supplied as trial references.
// 3. Test code uses the configuration with its own admission checks.
#pragma once

#include "LabCheckedMotionConfig.h"

namespace lab {

struct MotionTestConfig {
    MotionLimits limits{};
    Pose calibration{};
    Pose home{};
};

// M5Stack/StackChan eff0d6b5109663faec8f1d98848b4b0e5246bbfc, hal_servo.cpp:
// raw = zero + angle * 16 / 5 / 10 (angle in tenths of a degree).
// Bruno selected these references for the Enable-only trial, not measured unit calibration.
inline constexpr MotionTestConfig kMotionTestConfig{
    kCheckedMotionBounds,
    {460, 620},
    {460, 629}
};

} // namespace lab

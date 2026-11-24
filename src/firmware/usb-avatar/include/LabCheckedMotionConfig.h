// Goal: Select checked motion limits from reviewed unit configuration.
// Responsibilities: Define the outer envelope and validate the unit record.
// How it works:
// 1. A private build input may supply the reviewed unit record.
// 2. Metadata and motion configuration are checked against the outer bounds.
// 3. Validated defaults or an explicit error are returned to motion setup.
#pragma once

#include "LabMotion.h"

namespace lab {

// Existing operator-selected outer envelope, not measured unit calibration.
inline constexpr MotionLimits kCheckedMotionBounds{
    true, 51, 869, 629, 898, kYawBenchStep, kYawBenchStep, 2,
    200, 1000, 100, 200, 20, 100, 4000
};

inline constexpr char kCheckedDefaultSafetyMode[] = "checked-servo-default-v1";

struct ReviewedUnitMotion {
    const char* reviewReference = nullptr;
    const char* hardwareApplicability = nullptr;
    const char* reviewedOn = nullptr; // YYYY-MM-DD, not a claim that review was independently verified.
    MotionConfiguration configuration{};
};

#ifdef NETBOT_UNIT_MOTION_RECORD_PRESENT
#include "unit_motion_record.h"
#else
// Without the explicit private build input, checked motion stays unconfigured.
inline constexpr ReviewedUnitMotion kReviewedUnitMotion{};
#endif

struct CheckedMotionDefaults {
    MotionLimits limits;
    const MotionConfiguration* configuration;
    const char* error;
};

inline CheckedMotionDefaults checkedMotionDefaults(const ReviewedUnitMotion& unit = kReviewedUnitMotion) {
    const auto& c = unit.configuration;
    const bool absent = !unit.reviewReference && !unit.hardwareApplicability && !unit.reviewedOn &&
        !c.expectedYawRaw && !c.expectedPitchRaw && !c.homeYawRaw && !c.homePitchRaw &&
        !c.yawMinRaw && !c.yawMaxRaw && !c.pitchMinRaw && !c.pitchMaxRaw &&
        !c.stepRaw && !c.nodStepRaw && !c.travelMs;
    if (absent) return {kCheckedMotionBounds, nullptr, nullptr};
    if (!unit.reviewReference || !*unit.reviewReference ||
        !unit.hardwareApplicability || !*unit.hardwareApplicability ||
        !unit.reviewedOn || strlen(unit.reviewedOn) != 10 ||
        unit.reviewedOn[4] != '-' || unit.reviewedOn[7] != '-')
        return {kCheckedMotionBounds, nullptr, "motion_config_review_required"};
    for (int i = 0; i < 10; ++i)
        if (i != 4 && i != 7 && (unit.reviewedOn[i] < '0' || unit.reviewedOn[i] > '9'))
            return {kCheckedMotionBounds, nullptr, "motion_config_review_required"};
    const int year = (unit.reviewedOn[0] - '0') * 1000 + (unit.reviewedOn[1] - '0') * 100 +
        (unit.reviewedOn[2] - '0') * 10 + unit.reviewedOn[3] - '0';
    const int month = (unit.reviewedOn[5] - '0') * 10 + unit.reviewedOn[6] - '0';
    const int day = (unit.reviewedOn[8] - '0') * 10 + unit.reviewedOn[9] - '0';
    constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (!year || month < 1 || month > 12 || day < 1 ||
        day > days[month - 1] + (month == 2 && leap ? 1 : 0))
        return {kCheckedMotionBounds, nullptr, "motion_config_review_required"};
    if (configurationError(unit.configuration, kCheckedMotionBounds))
        return {kCheckedMotionBounds, nullptr, "invalid_motion_configuration"};
    return {configurationLimits(unit.configuration, kCheckedMotionBounds), &unit.configuration, nullptr};
}

} // namespace lab

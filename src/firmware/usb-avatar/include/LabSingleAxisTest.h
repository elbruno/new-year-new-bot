#pragma once

#include "LabMotion.h"

namespace lab {

struct SingleAxisTestFeedback {
    bool calibrationValid = false, positionValid = false, stationary = false;
    Pose calibration{}, position{};
    uint32_t observedAtMs = 0;
};

// Pure planning only: the caller must independently verify provenance, bind the
// sample to the current boot and supply an isolated driver and independent Stop.
inline const char* planSingleAxisTest(const SingleAxisTestBounds& bounds,
                                      const SingleAxisTestFeedback& feedback,
                                      uint32_t now, int delta, Pose& target) {
    if (const char* error = singleAxisBoundsError(bounds)) return error;
    if (!feedback.calibrationValid ||
        feedback.calibration.yaw < 0 || feedback.calibration.yaw > 1000 ||
        feedback.calibration.pitch < 0 || feedback.calibration.pitch > 1000 ||
        feedback.calibration.yaw != bounds.expectedCalibration.yaw ||
        feedback.calibration.pitch != bounds.expectedCalibration.pitch)
        return "single_axis_calibration_unconfirmed";
    if (!feedback.positionValid || !feedback.stationary ||
        feedback.position.yaw < 0 || feedback.position.yaw > 1000 ||
        feedback.position.pitch < 0 || feedback.position.pitch > 1000 ||
        now < feedback.observedAtMs || now - feedback.observedAtMs > bounds.maximumFeedbackAgeMs)
        return "single_axis_feedback_unconfirmed";
    if (!delta || delta < -bounds.maximumStep || delta > bounds.maximumStep)
        return "single_axis_step_invalid";
    const int position = bounds.axis == SingleAxis::Yaw ? feedback.position.yaw : feedback.position.pitch;
    if (position < bounds.minimum || position > bounds.maximum ||
        position + delta < bounds.minimum || position + delta > bounds.maximum)
        return "single_axis_out_of_bounds";
    Pose result = feedback.position;
    (bounds.axis == SingleAxis::Yaw ? result.yaw : result.pitch) += delta;
    target = result;
    return nullptr;
}

} // namespace lab

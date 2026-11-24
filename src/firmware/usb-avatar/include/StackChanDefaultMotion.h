#pragma once

#include "LabCheckedMotionConfig.h"
#include "LabScsMotion.h"
#include "StackChanMotion.h"

namespace lab {

class StackChanDefaultMotion {
public:
    explicit StackChanDefaultMotion(const ReviewedUnitMotion& unit = kReviewedUnitMotion)
        : bus(true), driver(bus, calibration, checkedMotionDefaults(unit).limits, false, false, true),
          controller(&driver, kCheckedMotionBounds, checkedMotionDefaults(unit).configuration,
              checkedMotionDefaults(unit).error) {}

    bool begin(uint32_t now) {
        controller.resetConfigurationForBoot();
        controller.inspectCalibration(now);
        return controller.startup(now);
    }

    StackChanScsBus bus;
    StackChanCalibration calibration;
    ScsMotionDriver<StackChanScsBus, StackChanCalibration> driver;
    MotionController controller;
};

} // namespace lab

// Goal: Sample the robot's touch and light sensors and control its RGB effects.
// Responsibilities: Perform checked bus operations and expose sensor state.
// How it works:
// 1. Periodic ticks initialize and sample requested sensors.
// 2. Fresh values, errors, and revisions describe the latest observations.
// 3. RGB effects advance or stop according to their duration and device state.
#pragma once

#include <stdint.h>
#include <string.h>
#include "LabRgbControl.h"

namespace lab {

// Checked register access is needed because the pinned BSP touch/RGB wrappers
// discard I2C failures, and its whole-board begin() also initializes servos.
template<class Bus>
class DemoSensors : public RgbController {
public:
    explicit DemoSensors(Bus& bus) : bus(bus) {}
    uint8_t top[3]{};
    uint16_t proximity = 0, light0 = 0, light1 = 0;
    const char* topError = "not_started";
    const char* lightError = "not_started";
    const char* rgbError = "not_started";
    bool rgbOn = false;
    uint32_t rgbDurationMs = 1000;
    uint32_t revision = 0;

    void tick(uint32_t now, bool visible, bool quiet, bool stopped,
              bool watchTop = false, bool topBusAvailable = true, bool watchLight = false,
              bool sampleTop = false) {
        freshTop = false;
        topAvailable = topBusAvailable;
        if (rgbOn && ((!rgbRemote && !visible && (!rgbPetting || !watchTop || !topBusAvailable)) ||
            quiet || stopped || now - rgbAt >= rgbDurationMs)) off();
        if (rgbOn && !rgbRemote && !rgbSecond && now - rgbAt >= rgbDurationMs / 2) {
            rgbSecond = true;
            if (!color(rgbGrumpy ? 0x8000 : 0x0884)) {
                rgbOn = false;
                rgbPetting = false;
                uncertainRgb();
            }
        }
        if (visible != active) {
            active = visible;
            ++revision;
            sampledTop = sampledLight = false;
        }
        const bool wantTop = visible || watchTop || sampleTop;
        if (!topBusAvailable) {
            if (topError != busUnavailable) ++revision;
            topError = busUnavailable;
            sampledTop = false;
        } else {
            if (wantTop && !topInitialized) initializeTop();
            if (wantTop != topActive) {
                topActive = wantTop;
                if (topReady && !topUnknown && !checked(0x68, 0x09, wantTop ? 0x03 : 0x07))
                    topUnknown = true;
                sampledTop = false;
            }
        }
        if (topBusAvailable && wantTop && topReady && !topUnknown &&
            (!sampledTop || now - topAt >= 100)) {
            sampledTop = true;
            topAt = now;
            uint8_t value = 0;
            const bool ok = read(0x68, 0x10, &value, 1);
            uint8_t result[] = {static_cast<uint8_t>((value >> 4) & 3),
                static_cast<uint8_t>((value >> 2) & 3), static_cast<uint8_t>(value & 3)};
            const char* error = ok ? nullptr : "top_i2c_failed";
            if (topError != error || (ok && memcmp(top, result, sizeof(top)))) ++revision;
            topError = error;
            if (ok) { memcpy(top, result, sizeof(top)); freshTop = true; topValid = true; topValidAt = now; }
        }
        if (topBusAvailable && topUnknown) topError = "top_write_unknown";
        const bool wantLight = visible || watchLight;
        if (topBusAvailable && wantLight != lightActive) {
            if (wantLight) initializeLight();
            lightActive = wantLight;
            if (lightReady && !lightUnknown &&
                (!bits(0x23, 0x80, 0x01, wantLight ? 0x01 : 0) ||
                 !bits(0x23, 0x81, 0x02, wantLight ? 0x02 : 0)))
                lightUnknown = true;
            if (lightUnknown) lightError = "light_write_unknown";
            sampledLight = false;
        }
        if (topBusAvailable && wantLight && lightReady && !lightUnknown &&
            (!sampledLight || now - lightAt >= 500)) {
            sampledLight = true;
            lightAt = now;
            uint8_t status = 0, als[4]{}, ps[2]{};
            const char* error = nullptr;
            // Read status before data, which acknowledges the new-data flags.
            if (!read(0x23, 0x8C, &status, 1)) error = "light_i2c_failed";
            else if ((status & 0x85) != 0x05) error = "light_not_ready";
            else if (!read(0x23, 0x88, als, 4) || !read(0x23, 0x8D, ps, 2))
                error = "light_i2c_failed";
            else if (ps[1] & 0x80) error = "proximity_invalid";
            const uint16_t p = static_cast<uint16_t>(((ps[1] & 7) << 8) | ps[0]);
            const uint16_t a0 = static_cast<uint16_t>((als[3] << 8) | als[2]);
            const uint16_t a1 = static_cast<uint16_t>((als[1] << 8) | als[0]);
            if (lightError != error || (!error && (p != proximity || a0 != light0 || a1 != light1)))
                ++revision;
            lightError = error;
            if (!error) { proximity = p; light0 = a0; light1 = a1; lightValid = true; lightValidAt = now; }
        }
    }

    bool topSampleFresh() const { return freshTop; }
    bool topIsInitialized() const { return topInitialized; }
    bool topIsAvailable() const { return topReady && !topUnknown && topAvailable; }
    bool topHasSample() const { return topValid; }
    uint32_t topSampleAt() const { return topValidAt; }
    bool lightIsInitialized() const { return lightInitialized; }
    bool lightIsAvailable() const { return lightReady && !lightUnknown && topAvailable; }
    bool lightHasSample() const { return lightValid; }
    uint32_t lightSampleAt() const { return lightValidAt; }

    bool pulse(uint32_t now, bool quiet, uint32_t durationMs = 1000) {
        return startPulse(now, quiet, durationMs, active, false, false);
    }

    bool pulseRgb(uint32_t now, bool quiet, uint16_t colorValue, uint32_t durationMs,
                  bool available) override {
        if (rgbUnknown) return false;
        if (durationMs < kRgbMinimumDurationMs || durationMs > kRgbMaximumDurationMs)
            return false;
        if (!available || quiet || rgbOn) {
            rgbError = quiet ? "quiet" : rgbOn ? "rgb_busy" : "open_sensors";
            ++revision;
            return false;
        }
        if (!rgbReady) {
            uint8_t version = 0;
            if (!read(0x6F, 0x02, &version, 1) || version == 0 || version == 0xFF) {
                rgbError = "rgb_unavailable";
                ++revision;
                return false;
            }
            if (!bits(0x6F, 0x04, 0x20, 0x20) || !bits(0x6F, 0x0C, 0x20, 0) ||
                !bits(0x6F, 0x0A, 0x20, 0x20) || !bits(0x6F, 0x14, 0x20, 0) ||
                !checked(0x6F, 0x24, 12)) return uncertainRgb();
            rgbReady = true;
        }
        if (!color(colorValue)) return uncertainRgb();
        rgbOn = true;
        rgbSecond = false;
        rgbPetting = false;
        rgbGrumpy = false;
        rgbRemote = true;
        rgbError = nullptr;
        rgbAt = now;
        rgbDurationMs = durationMs;
        ++revision;
        return true;
    }

    bool rgbActive() const override { return rgbOn; }
    bool rgbAvailable() const override { return active && !rgbUnknown; }

    bool pulsePetting(uint32_t now, bool quiet, bool grumpy = false) {
        return startPulse(now, quiet, grumpy ? 2000 : 1000, topActive && topAvailable, true, grumpy);
    }

private:
    Bus& bus;
    static constexpr const char* busUnavailable = "top_bus_unavailable";
    bool active = false, topActive = false, topAvailable = true, topInitialized = false, lightInitialized = false;
    bool topReady = false, lightReady = false;
    bool topUnknown = false, lightUnknown = false, rgbReady = false, rgbUnknown = false;
    bool rgbSecond = false, rgbPetting = false, rgbGrumpy = false, rgbRemote = false;
    bool sampledTop = false, sampledLight = false, freshTop = false;
    bool topValid = false, lightValid = false, lightActive = false;
    uint32_t topValidAt = 0, lightValidAt = 0;
    uint32_t topAt = 0, lightAt = 0, rgbAt = 0;

    bool startPulse(uint32_t now, bool quiet, uint32_t durationMs, bool available, bool petting, bool grumpy) {
        if (rgbUnknown) return false;
        if (durationMs != 1000 && !(petting && grumpy && durationMs == 2000) &&
            durationMs != 5000 && durationMs != 10000) {
            rgbError = "rgb_duration_invalid";
            ++revision;
            return false;
        }
        if (!available || quiet || rgbOn) {
            rgbError = quiet ? "quiet" : rgbOn ? "rgb_busy" : petting ? "petting_unavailable" : "open_sensors";
            ++revision;
            return false;
        }
        if (!rgbReady) {
            uint8_t version = 0;
            if (!read(0x6F, 0x02, &version, 1) || version == 0 || version == 0xFF) {
                rgbError = "rgb_unavailable";
                ++revision;
                return false;
            }
            // Only pin 13's upper-port bit is touched. VM/servo pin 0 and
            // every other shared power/GPIO bit remain owned by their callers.
            if (!bits(0x6F, 0x04, 0x20, 0x20) || !bits(0x6F, 0x0C, 0x20, 0) ||
                !bits(0x6F, 0x0A, 0x20, 0x20) || !bits(0x6F, 0x14, 0x20, 0) ||
                !checked(0x6F, 0x24, 12)) return uncertainRgb();
            rgbReady = true;
        }
        if (!color(grumpy ? 0xF800 : 0x2081)) return uncertainRgb();
        rgbOn = true;
        rgbSecond = false;
        rgbPetting = petting;
        rgbGrumpy = grumpy;
        rgbRemote = false;
        rgbError = nullptr;
        rgbAt = now;
        rgbDurationMs = durationMs;
        ++revision;
        return true;
    }

    bool read(uint8_t address, uint8_t reg, uint8_t* data, uint8_t size) {
        return bus.readRegister(address, reg, data, size, 100000);
    }
    bool write(uint8_t address, uint8_t reg, uint8_t* data, uint8_t size) {
        return bus.writeRegister(address, reg, data, size, 100000);
    }
    bool checked(uint8_t address, uint8_t reg, uint8_t value) {
        uint8_t observed = 0;
        return write(address, reg, &value, 1) && read(address, reg, &observed, 1) && observed == value;
    }
    bool bits(uint8_t address, uint8_t reg, uint8_t mask, uint8_t value) {
        uint8_t before = 0;
        if (!read(address, reg, &before, 1)) return false;
        const uint8_t after = static_cast<uint8_t>((before & ~mask) | value);
        return before == after || checked(address, reg, after);
    }
    void initializeTop() {
        topInitialized = true;
        uint8_t probe = 0;
        if (!read(0x68, 0x08, &probe, 1)) topError = "top_unavailable";
        else {
            topReady = true;
            for (uint8_t reg = 0x0A; reg <= 0x0F && topReady; ++reg)
                topReady = checked(0x68, reg, 0);
            uint8_t reset = 0x0F;
            topReady = topReady && write(0x68, 0x09, &reset, 1) &&
                checked(0x68, 0x09, 0x07) && checked(0x68, 0x08, 0x22);
            for (uint8_t reg = 0x02; reg <= 0x06 && topReady; ++reg)
                topReady = checked(0x68, reg, 0x33);
            topUnknown = !topReady;
            topError = topReady ? "sampling" : "top_write_unknown";
        }
    }
    void initializeLight() {
        if (lightInitialized) return;
        lightInitialized = true;
        uint8_t part = 0, manufacturer = 0;
        if (!read(0x23, 0x86, &part, 1) || !read(0x23, 0x87, &manufacturer, 1))
            lightError = "light_unavailable";
        else if (part != 0x92 || manufacturer != 0x05) lightError = "light_identity_mismatch";
        else {
            // LTR553: 5 mA IR peak, one pulse, 100 ms PS, 100 ms ALS
            // integration / 500 ms measurement. Values stay raw, not lux/cm.
            lightReady = checked(0x23, 0x82, 0x78) && checked(0x23, 0x83, 1) &&
                checked(0x23, 0x84, 2) && checked(0x23, 0x85, 3);
            lightUnknown = !lightReady;
            lightError = lightReady ? "sampling" : "light_write_unknown";
        }
    }
    bool color(uint16_t value) {
        uint8_t data[24]{};
        for (unsigned i = 0; i < sizeof(data); i += 2) {
            data[i] = static_cast<uint8_t>(value);
            data[i + 1] = static_cast<uint8_t>(value >> 8);
        }
        uint8_t refresh = 12 | 0x40;
        return write(0x6F, 0x30, data, sizeof(data)) && write(0x6F, 0x24, &refresh, 1);
    }
    bool uncertainRgb() {
        rgbUnknown = true;
        rgbError = "rgb_write_unknown";
        ++revision;
        return false;
    }
    void off() {
        rgbOn = false;
        rgbPetting = false;
        rgbRemote = false;
        if (!color(0)) { uncertainRgb(); return; }
        rgbError = nullptr;
        ++revision;
    }
};
} // namespace lab

// Goal: Render compact status icons over the avatar.
// Responsibilities: Represent Wi-Fi, quiet, battery, and companion states.
// How it works:
// 1. Device status selects each icon's visual state.
// 2. Pixel helpers paint the icon into an RGB565 buffer.
// 3. The display uses the rendered buffers in its status strip.
#pragma once

#include <stdint.h>
#include "LabNetwork.h"

// Status icons for the strip under the avatar.
//
// The renderer has no M5 dependency and uses integer math only, so the robot and the host tests
// draw identical pixels. Pixels are plain numeric RGB565, the same encoding as the generated avatar
// frames (the display runs with setSwapBytes(true)). Every icon is a 24x24 image on black.
//
// Shape carries the meaning as well as color, so the strip stays readable without telling colors apart:
//   fan, 0 to 3 arcs growing   amber  connecting (animated)
//   fan                        green  connected
//   fan with a "!" badge       red    failed
//   dot with side arcs         blue   setup network open
//   fan with a slash           gray   off
// app window with a green dot      companion present; blank reserved slot when absent
namespace status_icons {

constexpr int kSize = 24;
constexpr int kPixels = kSize * kSize;

// The strip occupies the last 24 rows below the touch footer.
constexpr int kStripY = 216;
constexpr int kSlotPitch = 36;
constexpr int kSlotCount = 4;
constexpr int kStripLeftMargin = 8;
constexpr int slotX(int slot) {
    return kStripLeftMargin + slot * kSlotPitch;
}
constexpr int kVersionLeft = slotX(kSlotCount - 1) + kSize + 8;
constexpr int kVersionRight = 320 - 8;
constexpr int kVersionY = kStripY + kSize / 2;
constexpr int kVersionGlyphWidth = 12;  // Font0's 6 px cells at size 2.
constexpr int kVersionHeight = 16;
constexpr int kUnset = -1;

constexpr uint32_t kPhaseMs = 350;
constexpr int kPhases = 4;

enum class Wifi : uint8_t { Off, Connecting, Connected, Setup, Failed };

// A failed attempt stays red until the next attempt starts, even while the setup network is open.
inline Wifi wifiIcon(lab::NetworkState state, bool setupOpen, const char* error) {
    using lab::NetworkState;
    if (state == NetworkState::Connecting) return Wifi::Connecting;
    if (state == NetworkState::Error || (state == NetworkState::Offline && error)) return Wifi::Failed;
    if (setupOpen) return Wifi::Setup;
    return state == NetworkState::Connected ? Wifi::Connected : Wifi::Off;
}

inline int phase(uint32_t now) { return static_cast<int>(now / kPhaseMs % kPhases); }

// Two draws with the same key are the same image; only the connecting icon animates.
inline int key(Wifi icon, uint32_t now) {
    return static_cast<int>(icon) * kPhases + (icon == Wifi::Connecting ? phase(now) : 0);
}

inline int quietKey(bool quiet) { return quiet ? 1 : 0; }

inline int batterySegments(int32_t level) {
    if (level < 0 || level > 100) return -1;
    if (level == 0) return 0;
    if (level <= 25) return 1;
    if (level <= 60) return 2;
    return 3;
}

inline int batteryKey(int32_t level, bool charging) {
    const int segments = batterySegments(level);
    return (segments + 1) * 2 + (segments >= 0 && charging ? 1 : 0);
}

inline int companionKey(bool present) { return present ? 1 : 0; }

// Geometry is measured in twelfths of a pixel, sampled 3x3 per pixel for smooth edges.
constexpr int kUnit = 12;
constexpr int kSamples = 3;
constexpr int kSampleStep = kUnit / kSamples;
constexpr int kSampleOffset = kSampleStep / 2;
constexpr int kBox = kSize * kUnit;

// Dot radius 2.5 px; arcs 2 px thick with 2 px gaps.
constexpr int kDotRadius = 30;
constexpr int kFanArcs[3][2] = {{54, 78}, {102, 126}, {150, 174}};
constexpr int kFanX = kBox / 2, kFanY = 216;
constexpr int kHotspotArcs[2][2] = {{54, 78}, {102, 126}};
// The failed icon moves the fan left to make room for the badge at the lower right.
constexpr int kFailedFanX = 126, kFailedFanY = 216;
constexpr int kBadgeX = 228, kBadgeY = 228, kBadgeRadius = 60, kBadgeClearRadius = 72;
// The "!" is pixel aligned: a 2x5 px bar and a 2x2 px dot, centered in the badge.
constexpr int kBarHalfWidth = 12, kBarTop = 180, kBarBottom = 240, kBarDotTop = 252, kBarDotBottom = 276;
// The slash runs corner to corner; distances are measured along the two diagonals.
constexpr int kSlashHalfWidth = 15, kSlashClearWidth = 33, kSlashLength = 216;

enum Paint : uint8_t { kBlack, kGreen, kAmber, kRed, kBlue, kGray, kWhite };
constexpr uint32_t kPalette[] = {0x000000, 0x22C55E, 0xFBBF24, 0xF04444, 0x4DA3FF, 0x8B8B96, 0xFFFFFF};

inline Paint batteryColor(int segments) {
    if (segments < 0) return kGray;
    if (segments <= 1) return kRed;
    if (segments == 2) return kAmber;
    return kGreen;
}

constexpr int square(int value) { return value * value; }
constexpr int magnitude(int value) { return value < 0 ? -value : value; }
constexpr bool ring(int distanceSquared, const int (&radii)[2]) {
    return distanceSquared >= square(radii[0]) && distanceSquared <= square(radii[1]);
}

// The Wi-Fi fan: a dot with up to three arcs above it, inside a 90 degree wedge.
inline bool fan(int x, int y, int originX, int originY, int arcs) {
    const int dx = x - originX, dy = originY - y, distance = square(dx) + square(dy);
    if (distance <= square(kDotRadius)) return true;
    if (dy <= 0 || magnitude(dx) > dy) return false;
    for (int i = 0; i < arcs; ++i)
        if (ring(distance, kFanArcs[i])) return true;
    return false;
}

// The setup network: a dot with two arcs on each side.
inline bool hotspot(int x, int y) {
    const int dx = x - kBox / 2, dy = y - kBox / 2, distance = square(dx) + square(dy);
    if (distance <= square(kDotRadius)) return true;
    if (magnitude(dy) > magnitude(dx)) return false;
    for (const auto& arc : kHotspotArcs)
        if (ring(distance, arc)) return true;
    return false;
}

inline bool exclamation(int x, int y) {
    return magnitude(x - kBadgeX) <= kBarHalfWidth &&
        ((y >= kBarTop && y < kBarBottom) || (y >= kBarDotTop && y < kBarDotBottom));
}

inline Paint paint(Wifi icon, int animationPhase, int x, int y) {
    switch (icon) {
        case Wifi::Connected: return fan(x, y, kFanX, kFanY, 3) ? kGreen : kBlack;
        case Wifi::Connecting: return fan(x, y, kFanX, kFanY, animationPhase) ? kAmber : kBlack;
        case Wifi::Setup: return hotspot(x, y) ? kBlue : kBlack;
        case Wifi::Failed: {
            const int distance = square(x - kBadgeX) + square(y - kBadgeY);
            if (distance <= square(kBadgeRadius)) return exclamation(x, y) ? kWhite : kRed;
            if (distance <= square(kBadgeClearRadius)) return kBlack;
            return fan(x, y, kFailedFanX, kFailedFanY, 3) ? kRed : kBlack;
        }
        default: {
            const int across = magnitude(x - y), along = magnitude(x + y - kBox);
            if (along <= kSlashLength && across <= kSlashHalfWidth) return kGray;
            if (along <= kSlashLength && across <= kSlashClearWidth) return kBlack;
            return fan(x, y, kFanX, kFanY, 3) ? kGray : kBlack;
        }
    }
}

inline bool speaker(int x, int y) {
    if (x >= 3 && x <= 6 && y >= 9 && y <= 14) return true;
    if (x < 7 || x > 13) return false;
    return magnitude(y - 12) <= x - 6;
}

inline bool soundArc(int x, int y, int radius) {
    const int dx = x - 9, dy = y - 12;
    const int distance = square(dx) + square(dy);
    return x >= 14 && dx > magnitude(dy) &&
        distance >= square(radius - 1) && distance <= square(radius + 1);
}

inline Paint quietPaint(bool quiet, int x, int y) {
    if (quiet && x >= 14 && x <= 22 &&
        magnitude((y - 6) * 7 - (x - 14) * 12) <= 7) return kAmber;
    if (speaker(x, y)) return kWhite;
    if (!quiet && (soundArc(x, y, 7) || soundArc(x, y, 12))) return kGreen;
    return kBlack;
}

inline bool batteryBorder(int x, int y) {
    if (x >= 9 && x <= 14 && y >= 3 && y <= 5) return true;
    return x >= 3 && x <= 20 && y >= 6 && y <= 20 &&
        (x == 3 || x == 20 || y == 6 || y == 20);
}

inline bool batteryBar(int x, int y, int segments) {
    if (y < 9 || y > 17) return false;
    for (int bar = 0; bar < segments; ++bar) {
        const int left = 5 + bar * 4;
        if (x >= left && x < left + 3) return true;
    }
    return false;
}

inline bool chargingBolt(int x, int y) {
    return (x >= 12 && x <= 15 && y >= 8 && y <= 10) ||
        (x >= 10 && x <= 13 && y >= 11 && y <= 13) ||
        (x >= 12 && x <= 15 && y >= 14 && y <= 16);
}

inline Paint batteryPaint(int32_t level, bool charging, int x, int y) {
    const int segments = batterySegments(level);
    if (charging && segments >= 0 && chargingBolt(x, y)) return kWhite;
    if (batteryBar(x, y, segments)) return batteryColor(segments);
    if (batteryBorder(x, y)) return segments < 0 ? kGray : kWhite;
    return kBlack;
}

inline Paint companionPaint(bool present, int x, int y) {
    if (!present) return kBlack;
    const bool dot = square(x - 17) + square(y - 16) <= square(2);
    if (dot) return kGreen;
    const bool window = x >= 3 && x <= 20 && y >= 4 && y <= 19 &&
        (x == 3 || x == 20 || y == 4 || y == 7 || y == 19);
    const bool controls = (y == 6 && x >= 6 && x <= 12 && (x % 3 == 0)) ||
        (x >= 6 && x <= 13 && (y == 11 || y == 14));
    return window || controls ? kBlue : kBlack;
}

inline uint16_t rgb565(Paint color) {
    const uint32_t value = kPalette[color];
    return static_cast<uint16_t>(((value >> 19 & 0x1F) << 11) |
        ((value >> 10 & 0x3F) << 5) | (value >> 3 & 0x1F));
}

template <typename Painter>
inline void renderStatic(Painter painter, uint16_t* pixels) {
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x)
            pixels[y * kSize + x] = rgb565(painter(x, y));
}

inline void renderQuiet(bool quiet, uint16_t* pixels) {
    renderStatic([quiet](int x, int y) { return quietPaint(quiet, x, y); }, pixels);
}

inline void renderBattery(int32_t level, bool charging, uint16_t* pixels) {
    renderStatic([level, charging](int x, int y) { return batteryPaint(level, charging, x, y); }, pixels);
}

inline void renderCompanion(bool present, uint16_t* pixels) {
    renderStatic([present](int x, int y) { return companionPaint(present, x, y); }, pixels);
}

// Renders one icon into pixels[kPixels]. animationPhase only matters for the connecting icon.
inline void render(Wifi icon, int animationPhase, uint16_t* pixels) {
    constexpr int kCount = kSamples * kSamples;
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            int red = 0, green = 0, blue = 0;
            for (int row = 0; row < kSamples; ++row) {
                for (int column = 0; column < kSamples; ++column) {
                    const uint32_t color = kPalette[paint(icon, animationPhase,
                        x * kUnit + kSampleOffset + column * kSampleStep,
                        y * kUnit + kSampleOffset + row * kSampleStep)];
                    red += static_cast<int>(color >> 16 & 0xFF);
                    green += static_cast<int>(color >> 8 & 0xFF);
                    blue += static_cast<int>(color & 0xFF);
                }
            }
            red = (red + kCount / 2) / kCount;
            green = (green + kCount / 2) / kCount;
            blue = (blue + kCount / 2) / kCount;
            pixels[y * kSize + x] = static_cast<uint16_t>(((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3));
        }
    }
}

}  // namespace status_icons
